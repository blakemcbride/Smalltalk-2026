/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The bytecode interpreter, following Blue Book Chapters 27 and 28.
 *
 *  The Blue Book keeps four values in registers -- method, receiver,
 *  instruction pointer and stack pointer -- and writes them back into the
 *  active context only when control moves.  We do the same, because the
 *  alternative is an object-table indirection on every stack push.
 *
 *  Two representation details that are easy to get wrong, both because the
 *  image stores them one-relative while the registers are zero-relative:
 *
 *      instructionPointer  stored as a byte index one past where it points
 *      stackPointer        stored as a count of occupied slots, register is
 *                          the absolute field index of the top of stack
 */

#include "interp.h"
#include "prim.h"
#include "census.h"
#include "st_sched.h"
#include "gfx.h"
#include "worker.h"
#include "st_atomic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Thread_local st_interp     st_vm;

/*
 *  Every running interpreter, so the collector can walk them all.  Written
 *  when a thread joins or leaves and read only at a safepoint, where by
 *  construction nothing is joining or leaving.
 */
#define MAX_INTERPRETERS    64

/*
 *  Lock-free on purpose.  The collector reads this table while every mutator
 *  is parked, but a thread on its way out of the pool is not parked -- it has
 *  stopped polling and is unregistering.  Guarding the table with a mutex
 *  would let the collector hold that mutex while waiting for a thread that
 *  is blocked on it, which is a deadlock.  Atomic slots avoid the question.
 */
static st_atomic_ptr    interpreters[MAX_INTERPRETERS];

void
ST_interp_register(void)
{
    unsigned    i;

    for (i = 0; i < MAX_INTERPRETERS; ++i) {
        uintptr_t   empty = 0;

        if (ST_cas_strong(&interpreters[i], &empty, (uintptr_t) &st_vm))
            return;
    }
}

void
ST_interp_unregister(void)
{
    unsigned    i;

    for (i = 0; i < MAX_INTERPRETERS; ++i) {
        uintptr_t   mine = (uintptr_t) &st_vm;

        if (ST_cas_strong(&interpreters[i], &mine, 0))
            return;
    }
}

/*  ----------  Small helpers  ----------  */

static st_oop
fetch_integer(uint32_t field, st_oop object)
{
    st_oop  value = OM_fetch_pointer(field, object);

    return OM_is_int(value) ? value : ST_NIL;
}

st_oop
ST_stack_value(uint32_t from_top)
{
    return OM_fetch_pointer(st_vm.stack_pointer - from_top,
                            st_vm.active_context);
}

void
ST_stack_put(uint32_t from_top, st_oop value)
{
    OM_store_pointer(st_vm.stack_pointer - from_top, st_vm.active_context,
                     value);
}

st_oop
ST_stack_top(void)
{
    return OM_fetch_pointer(st_vm.stack_pointer, st_vm.active_context);
}

void
ST_push(st_oop value)
{
    ++st_vm.stack_pointer;
    OM_store_pointer(st_vm.stack_pointer, st_vm.active_context, value);
}

st_oop
ST_pop(void)
{
    st_oop  value = OM_fetch_pointer(st_vm.stack_pointer,
                                     st_vm.active_context);

    --st_vm.stack_pointer;
    return value;
}

void
ST_pop_n(uint32_t n)
{
    st_vm.stack_pointer -= n;
}

void
ST_unpop(uint32_t n)
{
    st_vm.stack_pointer += n;
}

/*  ----------  Method access  ----------  */

static st_oop
method_header(st_oop method)
{
    return OM_fetch_pointer(ST_METHOD_HEADER_INDEX, method);
}

static st_oop
method_literal(uint32_t index, st_oop method)
{
    return OM_fetch_pointer(ST_METHOD_LITERAL_START + index, method);
}

static uint32_t
method_initial_ip(st_oop method)
{
    /*
     *  Bytecodes begin after the header word and the literal frame.
     *
     *  The stride is the object memory's pointer size, not two.  A hardcoded
     *  two is right for the Blue Book's 16-bit words and silently wrong for
     *  the 64-bit memory, where it starts execution four words early --
     *  inside the literal frame, interpreting object pointers as bytecodes.
     *  Every expression that reached a real method lookup crashed; the ones
     *  answered by a primitive or an inlined conditional worked, which is
     *  what made it look like a problem with sends.
     */
    return (ST_header_literal_count(method_header(method))
            + ST_METHOD_LITERAL_START) * (uint32_t) sizeof(st_oop);
}

/*
 *  A method's primitive index, or 0.
 *
 *  Exported because two things outside the dispatch path need it: the trace
 *  test, which asks the 1983 image whether it uses 198 or 199 for anything
 *  (they are about to mean "unwind" and "handler"), and the non-local
 *  return, which finds an unwind-protected frame by exactly that number.
 */
unsigned
ST_method_primitive_index(st_oop method)
{
    st_oop      header = method_header(method);
    unsigned    literals;

    if (ST_header_flag_value(header) != 7)
        return 0;
    literals = ST_header_literal_count(header);
    if (literals < 2)
        return 0;
    /*  The extension is the next-to-last literal.  */
    return ST_extension_primitive_index(method_literal(literals - 2, method));
}

static unsigned
method_argument_count(st_oop method)
{
    st_oop      header = method_header(method);
    unsigned    flag   = ST_header_flag_value(header);
    unsigned    literals;

    if (flag <= 4)
        return flag;
    if (flag != 7)
        return 0;                       /*  quick returns take no arguments  */
    literals = ST_header_literal_count(header);
    if (literals < 2)
        return 0;
    return ST_extension_argument_count(method_literal(literals - 2, method));
}

static unsigned
method_temporary_count(st_oop method)
{
    return ST_header_temporary_count(method_header(method));
}

/*  ----------  Instruction fetch  ----------  */

static uint8_t
next_byte(void)
{
    uint8_t b = OM_fetch_byte(st_vm.instruction_pointer, st_vm.method);

    ++st_vm.instruction_pointer;
    return b;
}

/*  ----------  Context registers  ----------  */

static void
fetch_context_registers(void)
{
    st_oop  ctx = st_vm.active_context;

    if (OM_fetch_class(ctx) == ST_CLASS_BLOCK_CONTEXT)
        st_vm.home_context = OM_fetch_pointer(ST_CTX_HOME, ctx);
    else
        st_vm.home_context = ctx;

    st_vm.receiver = OM_fetch_pointer(ST_CTX_RECEIVER, st_vm.home_context);
    st_vm.method   = OM_fetch_pointer(ST_CTX_METHOD, st_vm.home_context);
    st_vm.instruction_pointer =
        (uint32_t) OM_int_value(fetch_integer(ST_CTX_IP, ctx)) - 1;
    st_vm.stack_pointer =
        (uint32_t) OM_int_value(fetch_integer(ST_CTX_SP, ctx))
        + ST_CTX_TEMP_FRAME_START - 1;
}

static void
store_context_registers(void)
{
    st_oop  ctx = st_vm.active_context;

    if (!OM_is_object(ctx))
        return;
    OM_store_pointer(ST_CTX_IP, ctx,
                     OM_int_oop((st_int) st_vm.instruction_pointer + 1));
    OM_store_pointer(ST_CTX_SP, ctx,
                     OM_int_oop((st_int) st_vm.stack_pointer
                                - ST_CTX_TEMP_FRAME_START + 1));
}

static void
set_active_context(st_oop ctx)
{
    /*
     *  The new context is referenced by the interpreter itself, so it must
     *  be counted before the old one is released or a self-transition could
     *  free the context we are switching to.
     */
    OM_increase_ref(ctx);
    OM_decrease_ref(st_vm.active_context);
    st_vm.active_context = ctx;
    fetch_context_registers();
}

/*
 *  Roots the object memory cannot see.
 *
 *  This set must mirror the references the VM actually COUNTS, not merely
 *  the ones it can reach.  The collector rebuilds every count from the walk,
 *  so a root visited here that the interpreter never counted leaves the
 *  object one too high afterwards -- and an object whose count can no longer
 *  fall to zero is never freed again.  Visiting the whole register file that
 *  way inflated every context by up to five per collection and leaked them
 *  wholesale.
 *
 *  The interpreter counts exactly one reference: the active context, taken
 *  in set_active_context.  home_context, method and receiver are uncounted
 *  registers, all reachable through it -- a block context points at its
 *  home, a context at its method and receiver -- so reachability is covered
 *  without touching their counts.  new_method and message_selector are
 *  likewise reachable from the method dictionary that yielded them.
 *
 *  The display form and the input semaphore are genuinely held by C, and
 *  their setters count them, so they belong here.
 */
static om_root_provider extra_roots;

static void
provide_roots(om_visit_fn visit)
{
    unsigned    i;

    /*
     *  Every thread's active context, not just this one's.  A collection
     *  runs with all of them parked, so reading the table here is safe --
     *  and missing an entry would free another thread's entire stack.
     */
    for (i = 0; i < MAX_INTERPRETERS; ++i) {
        st_interp  *vm = (st_interp *) ST_load_acquire(&interpreters[i]);

        if (vm)
            visit(vm->active_context);
    }
    /*
     *  And this thread's, registered or not.
     *
     *  Registration happens in ST_interp_init, which the -run path calls
     *  and the -eval path does not -- so a collection during a doIt walked
     *  a table with nothing in it and freed the doIt's own context and
     *  method, and the interpreter carried on reading bytecodes out of
     *  memory that had been handed back.  It stayed hidden because nothing
     *  could ASK for a collection from Smalltalk until weak references
     *  needed one, and an automatic collection only happens when the table
     *  fills.
     *
     *  Registering in the right places is the other half and is done; this
     *  is the half that cannot be forgotten by a future caller, because the
     *  thread doing the collecting is always the thread that is running.
     */
    visit(st_vm.active_context);
    visit(GFX_display_form());
    visit(SCHED_input_semaphore());
    visit(SCHED_pending_process());
    visit(st_om_vm_state[ST_VM_INPUT_SEMAPHORE]);
    visit(st_om_vm_state[ST_VM_DISPLAY]);
    visit(st_om_vm_state[ST_VM_CLASS_BLOCK_CLOSURE]);
    visit(st_om_vm_state[ST_VM_SELECTOR_ABOUT_TO_RETURN]);
    if (extra_roots)
        extra_roots(visit);
}

void
ST_interp_install_roots(om_root_provider extra)
{
    extra_roots = extra;
    OM_set_root_provider(provide_roots);
}

void
ST_store_active_context(void)
{
    store_context_registers();
}

void
ST_set_active_context(st_oop ctx)
{
    set_active_context(ctx);
}

/*  ----------  Method lookup  ----------  */

/*
 *  Walk the superclass chain looking for the selector.  The method
 *  dictionary layout was validated against Xerox's method.oops dump before
 *  the interpreter was written, so this traverses known-good structure.
 */
static st_oop
lookup_method(st_oop selector, st_oop start_class, st_oop *found_class)
{
    st_oop  cls = start_class;

    /*  A nil superclass is the top of the chain, so the walk stops there.  */
    while (OM_is_present(cls)) {
        st_oop      dict = OM_fetch_pointer(ST_CLASS_METHOD_DICT, cls);
        uint32_t    capacity = OM_method_dict_capacity(dict);
        uint32_t    slot;

        for (slot = 0; slot < capacity; ++slot) {
            if (OM_method_dict_key(dict, slot) == selector) {
                if (found_class)
                    *found_class = cls;
                return OM_method_dict_value(dict, slot);
            }
        }
        cls = OM_fetch_pointer(ST_CLASS_SUPERCLASS, cls);
    }
    if (found_class)
        *found_class = ST_NIL;
    return ST_NIL;
}

/*
 *  Name a method, by finding it again in the class that holds it.
 *
 *  A CompiledMethod does not know its own selector: the selector is the KEY
 *  the method dictionary filed it under, and nothing points back.  So the
 *  only way to name one is to look for it, which is what this does -- up the
 *  chain from the receiver's class, because the method that is running may
 *  well be inherited.
 *
 *  It is a linear scan of every dictionary on the way up, which would be
 *  indefensible anywhere but here.  This runs once, when something has
 *  already gone wrong and the run is over.
 */
static int
name_method(st_oop receiver, st_oop method, char *out, size_t len)
{
    st_oop  cls = OM_fetch_class(receiver);

    while (OM_is_present(cls)) {
        st_oop      dict = OM_fetch_pointer(ST_CLASS_METHOD_DICT, cls);
        uint32_t    capacity = OM_method_dict_capacity(dict);
        uint32_t    slot;

        for (slot = 0; slot < capacity; ++slot) {
            if (OM_method_dict_value(dict, slot) == method) {
                char        cname[64];
                char        sel[128];
                st_oop      key = OM_method_dict_key(dict, slot);
                uint32_t    n = OM_is_present(key)
                                    ? OM_fetch_byte_length(key) : 0;
                uint32_t    k;

                if (n > sizeof sel - 1)
                    n = sizeof sel - 1;
                for (k = 0; k < n; ++k)
                    sel[k] = (char) OM_fetch_byte(k, key);
                sel[n] = '\0';
                if (!OM_class_name_of(cls, cname, sizeof cname))
                    snprintf(cname, sizeof cname, "?");
                snprintf(out, len, "%s>>%s", cname, n ? sel : "?");
                return 1;
            }
        }
        cls = OM_fetch_pointer(ST_CLASS_SUPERCLASS, cls);
    }
    return 0;
}

/*
 *  Where the failing send came from, as far back as it goes.
 *
 *  One line naming the running method\'s class was what this used to print,
 *  and it was not enough to act on: half the errors raised during a
 *  bootstrap are raised by the error REPORTING path -- Object>>error: draws
 *  its message at Sensor cursorPoint, and asks a nil Sensor -- so the class
 *  named was the reporter rather than anything to do with the fault.  The
 *  chain distinguishes the two immediately.
 */
#define BACKTRACE_LIMIT     24

int         ST_quit_requested;

static int  errors_reported = 1;

void
ST_set_error_reporting(int on)
{
    errors_reported = on;
}

int
ST_errors_reported(void)
{
    return errors_reported;
}

void
ST_report_backtrace(void)
{
    st_oop      ctx = st_vm.active_context;
    unsigned    depth = 0;

    if (!errors_reported)
        return;
    while (OM_is_present(ctx) && depth < BACKTRACE_LIMIT) {
        st_oop  method = OM_fetch_pointer(ST_CTX_METHOD, ctx);
        st_oop  receiver = OM_fetch_pointer(ST_CTX_RECEIVER, ctx);
        char    name[200];

        /*
         *  A block context holds its home in the method field, so it is
         *  named by the method the block was written in.
         */
        if (!OM_is_present(method) || !name_method(receiver, method,
                                                  name, sizeof name)) {
            char    cname[64];

            if (!OM_class_name_of(OM_fetch_class(receiver), cname,
                                  sizeof cname))
                snprintf(cname, sizeof cname, "?");
            snprintf(name, sizeof name, "a method of %s", cname);
        }
        fprintf(stderr, "       %s %s\n", depth ? "from" : "sent from", name);
        ctx = OM_fetch_pointer(ST_CTX_SENDER, ctx);
        ++depth;
    }
    if (OM_is_present(ctx))
        fprintf(stderr, "       ... and further\n");
}

/*  ----------  Activation and return  ----------  */

static void
activate_new_method(void)
{
    st_oop      header = method_header(st_vm.new_method);
    uint32_t    slots  = ST_header_large_context(header)
                            ? ST_LARGE_CONTEXT_SLOTS : ST_SMALL_CONTEXT_SLOTS;
    st_oop      ctx;
    uint32_t    temps  = method_temporary_count(st_vm.new_method);
    uint32_t    i;

    ctx = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, slots);
    if (!OM_is_object(ctx)) {
        fprintf(stderr, "st80: out of memory activating a method: "
                        "%u words and %u object table entries free\n",
                OM_core_left(), OM_oops_left());
        st_vm.running = 0;
        return;
    }
    OM_store_pointer(ST_CTX_SENDER, ctx, st_vm.active_context);
    OM_store_pointer(ST_CTX_IP, ctx,
                     OM_int_oop((st_int) method_initial_ip(st_vm.new_method) + 1));
    OM_store_pointer(ST_CTX_SP, ctx, OM_int_oop((st_int) temps));
    OM_store_pointer(ST_CTX_METHOD, ctx, st_vm.new_method);

    /*
     *  Arguments sit above the receiver on the sender's stack.  Move them
     *  into the new context's temporary frame, receiver first.
     */
    for (i = 0; i < st_vm.argument_count; ++i) {
        st_oop  arg = ST_stack_value(st_vm.argument_count - 1 - i);

        OM_store_pointer(ST_CTX_TEMP_FRAME_START + i, ctx, arg);
    }
    OM_store_pointer(ST_CTX_RECEIVER, ctx,
                     ST_stack_value(st_vm.argument_count));
    ST_pop_n(st_vm.argument_count + 1);

    store_context_registers();
    ++st_vm.call_depth;
    /*
     *  Instantiation hands back a count of zero; making the context active
     *  is what gives it its one reference.  Nothing else refers to it -- the
     *  sender link points outward, not in -- so there is no second reference
     *  to release here.
     */
    set_active_context(ctx);
}

/*
 *  Activating a block context.  Lives here rather than in prim.c because it
 *  is a context switch and the registers are here.
 */
int
ST_activate_block(st_oop block, uint32_t argc)
{
    st_oop      initial_ip;
    uint32_t    i;

    for (i = 0; i < argc; ++i)
        OM_store_pointer(ST_CTX_TEMP_FRAME_START + i, block,
                         ST_stack_value(argc - 1 - i));
    ST_pop_n(argc + 1);

    initial_ip = OM_fetch_pointer(ST_CTX_INITIAL_IP, block);
    OM_store_pointer(ST_CTX_IP, block, initial_ip);
    OM_store_pointer(ST_CTX_SP, block, OM_int_oop((st_int) argc));
    OM_store_pointer(ST_CTX_CALLER, block, st_vm.active_context);

    store_context_registers();
    ++st_vm.call_depth;
    set_active_context(block);
    return 1;
}

/*
 *  Is this a BlockClosure?
 *
 *  Through the VM-state slot rather than a guaranteed pointer, so that a
 *  build with no BlockClosure loaded -- the Blue Book one, and the bb build
 *  reading the 1983 image -- answers no to everything and never reaches any
 *  of the code below.  That is the whole of how the two block mechanisms
 *  coexist.
 */
int
ST_is_block_closure(st_oop p)
{
    st_oop  closure_class = st_om_vm_state[ST_VM_CLASS_BLOCK_CLOSURE];

    return OM_is_present(closure_class) && OM_is_object(p)
        && OM_fetch_class(p) == closure_class;
}

/*
 *  Activating a closure.  Beside ST_activate_block for the same reason: it
 *  is a context switch and the registers live here.
 *
 *  The difference from a BlockContext is the whole point.  A BlockContext IS
 *  the frame, so activating one writes into the object somebody is holding,
 *  and its variables live in the home method's temporaries.  A closure is
 *  only a description; each activation builds a MethodContext of its own,
 *  whose field 4 names the closure it is running, and whose temporary frame
 *  holds the arguments and the copied values.  fetch_context_registers
 *  needs no change for that: it already takes anything that is not a
 *  BlockContext as its own home, so the temporary bytecodes address this
 *  frame rather than an enclosing one.
 */
int
ST_activate_closure(st_oop closure, uint32_t argc)
{
    st_oop      outer;
    st_oop      method;
    st_oop      receiver;
    st_oop      ctx;
    uint32_t    copied;
    uint32_t    slots;
    uint32_t    i;

    outer = OM_fetch_pointer(ST_CLOSURE_OUTER_CONTEXT, closure);
    if (!OM_is_present(outer))
        return 0;
    /*
     *  The method and receiver come from the closure's birthplace, and a
     *  closure activation carries both, so this reads correctly however
     *  deeply closures are nested.  Note do_return nils a returning
     *  context's sender and ip but leaves its method and receiver intact --
     *  which is exactly what lets a closure outlive its creator.
     */
    method   = OM_fetch_pointer(ST_CTX_METHOD, outer);
    receiver = OM_fetch_pointer(ST_CTX_RECEIVER, outer);
    if (!OM_is_object(method))
        return 0;

    copied = OM_fetch_word_length(closure);
    if (copied < ST_CLOSURE_FIRST_COPIED)
        return 0;
    copied -= ST_CLOSURE_FIRST_COPIED;

    slots = ST_header_large_context(method_header(method))
                ? ST_LARGE_CONTEXT_SLOTS : ST_SMALL_CONTEXT_SLOTS;
    if (argc + copied + ST_CTX_TEMP_FRAME_START > slots)
        return 0;                   /*  the frame cannot hold them  */

    ctx = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, slots);
    if (!OM_is_object(ctx))
        return 0;

    /*
     *  The frame is [arguments][copied values][the block's own temporaries
     *  and working stack], which is what the operand indices of the remote
     *  temporary bytecodes are counted against.
     */
    for (i = 0; i < argc; ++i)
        OM_store_pointer(ST_CTX_TEMP_FRAME_START + i, ctx,
                         ST_stack_value(argc - 1 - i));
    for (i = 0; i < copied; ++i)
        OM_store_pointer(ST_CTX_TEMP_FRAME_START + argc + i, ctx,
                         OM_fetch_pointer(ST_CLOSURE_FIRST_COPIED + i,
                                          closure));
    ST_pop_n(argc + 1);

    OM_store_pointer(ST_CTX_SENDER,   ctx, st_vm.active_context);
    OM_store_pointer(ST_CTX_IP,       ctx,
                     OM_fetch_pointer(ST_CLOSURE_STARTPC, closure));
    OM_store_pointer(ST_CTX_SP,       ctx,
                     OM_int_oop((st_int) (argc + copied)));
    OM_store_pointer(ST_CTX_METHOD,   ctx, method);
    OM_store_pointer(ST_CTX_CLOSURE,  ctx, closure);
    OM_store_pointer(ST_CTX_RECEIVER, ctx, receiver);

    store_context_registers();
    ++st_vm.call_depth;
    set_active_context(ctx);
    return 1;
}

static void
do_return(st_oop result, st_oop to_context, int from_block)
{
    st_oop  sender = to_context;

    ST_trace_return(result, from_block);

    /*
     *  nil is a live object, so testing reachability alone would let a
     *  return off the bottom of the stack dereference it.  An empty slot in
     *  Smalltalk holds nil, and that is what has to be checked for.
     */
    if (sender == ST_NIL || !OM_is_object(sender)) {
        /*  The bottom of the world: keep the answer and stop.  */
        if (getenv("ST_BOTTOM_LOG")) {
            fprintf(stderr, "st80: returned off the bottom at cycle %llu\n",
                    (unsigned long long) st_vm.cycle);
            ST_report_backtrace();
        }
        st_vm.return_value = result;
        st_vm.running      = 0;
        return;
    }
    if (st_vm.call_depth > 0)
        --st_vm.call_depth;

    /*
     *  Break the outgoing context's links before dropping it -- the Blue
     *  Book's nilContextFields.  Without this the sender chain stays whole
     *  whenever anything else still refers to the context, and every frame
     *  below it is retained: thisContext having been pushed, a BlockContext
     *  holding its home, a debugger looking on.  Contexts then pile up until
     *  the object table is gone, and the marking collector cannot help,
     *  because they really are still reachable.
     *
     *  The result and the destination are both counted across the switch so
     *  that neither can be the thing this release frees.
     */
    OM_increase_ref(sender);
    OM_increase_ref(result);
    OM_store_pointer(ST_CTX_SENDER, st_vm.active_context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, st_vm.active_context, ST_NIL);
    set_active_context(sender);
    ST_push(result);
    OM_decrease_ref(result);
    OM_decrease_ref(sender);
}

/*
 *  ----------  Jumping to a context  ----------
 *
 *  Two operations the exception library cannot express in Smalltalk, both
 *  of them do_return's machinery under a different name.
 *
 *  ST_return_to abandons the running context and everything it called, and
 *  completes the send that created `ctx` with `value` -- which is how a
 *  handler makes its on:do: answer.  ST_resume_at goes the other way: it
 *  makes a suspended context current again with `value` pushed as the
 *  result of the send it stopped at, which is what a resumable exception
 *  needs.
 *
 *  Neither stores the outgoing registers, for the same reason do_return
 *  does not: the context they are leaving is not coming back.
 */
void
ST_return_to(st_oop value, st_oop ctx)
{
    st_oop  sender;

    if (!OM_is_object(ctx))
        return;
    sender = OM_fetch_pointer(ST_CTX_SENDER, ctx);
    do_return(value, sender, 0);
}

/*
 *  Begin an activation again from its first bytecode.
 *
 *  This is what retry needs.  Re-sending on:do: from inside the handler
 *  would work once and grow the stack every time, so a retry that keeps
 *  failing runs out of memory rather than looping -- which is how an
 *  infinite retry crashed instead of hanging.  Restarting reuses the frame,
 *  and the arguments are already in it.
 *
 *  The reset happens here rather than through the 1983
 *  MethodContext>>restart, which resets a frame for the Debugger to resume
 *  later and sets its stack pointer to "numArgs + numTemps".  The header's
 *  temporary count already counts the arguments -- it is what
 *  activate_new_method uses for the stack pointer directly -- so that sum
 *  is the arguments twice, and the frame would come back with two junk
 *  slots below its stack.  Whether that was ever right for Xerox's own
 *  encoding is a question for the Debugger and not for this.
 */
void
ST_restart_at(st_oop ctx)
{
    st_oop  method;

    if (!OM_is_object(ctx))
        return;
    method = OM_fetch_pointer(ST_CTX_METHOD, ctx);
    if (!OM_is_object(method))
        return;
    OM_store_pointer(ST_CTX_IP, ctx,
                     OM_int_oop((st_int) method_initial_ip(method) + 1));
    OM_store_pointer(ST_CTX_SP, ctx,
                     OM_int_oop((st_int) method_temporary_count(method)));
    OM_store_pointer(ST_CTX_SENDER, st_vm.active_context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, st_vm.active_context, ST_NIL);
    set_active_context(ctx);
}

void
ST_resume_at(st_oop value, st_oop ctx)
{
    if (!OM_is_object(ctx))
        return;
    OM_store_pointer(ST_CTX_SENDER, st_vm.active_context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, st_vm.active_context, ST_NIL);
    set_active_context(ctx);
    ST_push(value);
}

/*
 *  The first unwind-protected context strictly between `from` and `home`,
 *  or nil; *home_found says whether home was reached at all.
 *
 *  "Unwind-protected" is a method whose primitive index is 198 -- not a
 *  primitive at all but a MARK, which is why nothing in prim.c implements
 *  it: an unknown primitive already fails, so ensure: and ifCurtailed: run
 *  their Smalltalk bodies and the number is left as a label a walk can see.
 *  199 is the same trick for a handler.  Squeak does exactly this, and the
 *  1983 image was asked whether it uses either number for anything real:
 *  its highest primitive is 135.
 */
/*
 *  The primitive number a frame's method declares, or 0.
 *
 *  The class check is not defensive tidiness.  A BlockContext's field 3 is
 *  its ARGUMENT COUNT, not a method -- the two layouts share the slot --
 *  so reading it as a method on the way past a Blue Book block would take
 *  the body of a SmallInteger.  One image holds both kinds now, sources/
 *  being Blue Book and lib/ closures, so such a frame is not a hypothesis.
 */
unsigned
ST_context_primitive(st_oop ctx)
{
    st_oop  method;

    if (!OM_is_object(ctx)
     || OM_fetch_class(ctx) != ST_CLASS_METHOD_CONTEXT)
        return 0;
    method = OM_fetch_pointer(ST_CTX_METHOD, ctx);
    if (!OM_is_object(method))
        return 0;
    return ST_method_primitive_index(method);
}

static st_oop
find_unwind_between(st_oop from, st_oop home, int *home_found)
{
    st_oop  ctx = OM_fetch_pointer(ST_CTX_SENDER, from);

    *home_found = 0;
    while (OM_is_present(ctx)) {
        if (ctx == home) {
            *home_found = 1;
            return ST_NIL;
        }
        if (ST_context_primitive(ctx) == 198)
            return ctx;
        ctx = OM_fetch_pointer(ST_CTX_SENDER, ctx);
    }
    return ST_NIL;
}

/*
 *  A non-local return whose home method has already returned.
 *
 *  The selector is looked up before it is sent, because the 1983 library
 *  does not implement cannotReturn: -- it is named by SystemTracer as a
 *  special oop and nothing else.  Sending it blind would land in
 *  doesNotUnderstand:, which opens a NotifierView, which asks Sensor for
 *  the cursor position, which is the reporting recursion this file already
 *  guards against elsewhere.  With nobody to tell, behave as before: stop
 *  and keep the value.
 */
static void
send_cannot_return(st_oop ctx, st_oop result)
{
    st_oop  found = ST_NIL;

    if (!OM_is_present(lookup_method(ST_SELECTOR_CANNOT_RETURN,
                                     OM_fetch_class(ctx), &found))) {
        st_vm.return_value = result;
        st_vm.running = 0;
        return;
    }
    ST_push(ctx);
    ST_push(result);
    ST_send_selector(ST_SELECTOR_CANNOT_RETURN, 1);
}

static void
send_about_to_return(st_oop ctx, st_oop result, st_oop unwind)
{
    st_oop  selector = st_om_vm_state[ST_VM_SELECTOR_ABOUT_TO_RETURN];
    st_oop  found = ST_NIL;

    if (!OM_is_present(selector)
     || !OM_is_present(lookup_method(selector, OM_fetch_class(ctx), &found))) {
        /*
         *  No ensure: machinery in this image.  Returning without running
         *  the unwind blocks is what the system did before there were any.
         */
        do_return(result, OM_fetch_pointer(ST_CTX_SENDER,
                              OM_fetch_pointer(ST_CLOSURE_OUTER_CONTEXT,
                                  OM_fetch_pointer(ST_CTX_CLOSURE, ctx))), 0);
        return;
    }
    ST_push(ctx);
    ST_push(result);
    ST_push(unwind);
    ST_send_selector(selector, 2);
}

/*
 *  Returning from a method, which is also how a block returns out of one.
 *
 *  The BlockContext arm below is exactly the three lines this function used
 *  to be, and is meant to stay that way: it is on the trace2 path, which
 *  runs blockCopy: twice, and the value of that oracle depends on this code
 *  being untouched rather than carefully preserved.  Everything closures
 *  need is in the other arm, and do_return itself is not modified at all --
 *  its nil-sender stop is what -eval depends on to get an answer back.
 */
static void
return_value(st_oop result)
{
    st_oop  ctx = st_vm.active_context;
    st_oop  home;
    st_oop  sender;

    if (OM_fetch_class(ctx) == ST_CLASS_BLOCK_CONTEXT) {
        home   = st_vm.home_context;
        sender = OM_fetch_pointer(ST_CTX_SENDER, home);
        do_return(result, sender, 0);
        return;
    }

    /*
     *  A MethodContext.  Field 4 is nil for an ordinary method -- measured
     *  true of every context in the 1983 image -- so the loop below exits
     *  at once and this is the old behaviour with one extra read.
     */
    home = ctx;
    for (;;) {
        st_oop  closure = OM_fetch_pointer(ST_CTX_CLOSURE, home);
        st_oop  outer;

        if (!OM_is_present(closure))
            break;
        outer = OM_fetch_pointer(ST_CLOSURE_OUTER_CONTEXT, closure);
        if (!OM_is_object(outer)
         || OM_fetch_class(outer) != ST_CLASS_METHOD_CONTEXT) {
            send_cannot_return(ctx, result);
            return;
        }
        home = outer;
    }

    if (home == ctx) {
        sender = OM_fetch_pointer(ST_CTX_SENDER, home);
        do_return(result, sender, 0);
        return;
    }
    {
        int     found = 0;
        st_oop  unwind = find_unwind_between(ctx, home, &found);

        if (OM_is_present(unwind)) {
            send_about_to_return(ctx, result, unwind);
            return;
        }
        if (!found) {
            /*  The home method returned before this block ran.  */
            send_cannot_return(ctx, result);
            return;
        }
    }
    sender = OM_fetch_pointer(ST_CTX_SENDER, home);
    do_return(result, sender, 0);
}

/*  ----------  Sending  ----------  */

static void
send_does_not_understand(st_oop receiver, st_oop selector, st_oop lookup_class)
{
    st_oop  args;
    st_oop  message;
    st_oop  method;
    st_oop  found;
    uint32_t i;

    args = OM_instantiate_pointers(ST_CLASS_ARRAY, st_vm.argument_count);
    for (i = 0; i < st_vm.argument_count; ++i)
        OM_store_pointer(i, args,
                         ST_stack_value(st_vm.argument_count - 1 - i));
    message = OM_instantiate_pointers(ST_CLASS_MESSAGE, 2);
    OM_store_pointer(ST_MESSAGE_SELECTOR, message, selector);
    OM_store_pointer(ST_MESSAGE_ARGUMENTS, message, args);

    ST_pop_n(st_vm.argument_count);
    ST_push(message);
    st_vm.argument_count = 1;

    method = lookup_method(ST_SELECTOR_DOES_NOT_UNDERSTAND, lookup_class, &found);
    if (!OM_is_present(method)) {
        char        buf[256];
        char        name[256];
        uint32_t    n;
        uint32_t    k;

        /*
         *  Name the selector.  Reporting only that "something" was not
         *  understood leaves the reader to guess which of the dozen sends in
         *  a line it was, and the guess is usually wrong.  The selector is a
         *  Symbol, so its bytes are its text.
         */
        ST_print_object(receiver, buf, sizeof buf);
        n = OM_is_present(selector) ? OM_fetch_byte_length(selector) : 0;
        if (n > sizeof name - 1)
            n = sizeof name - 1;
        for (k = 0; k < n; ++k)
            name[k] = (char) OM_fetch_byte(k, selector);
        name[n] = '\0';

        if (errors_reported)
            fprintf(stderr, "st80: %s does not understand #%s, and does not "
                            "understand doesNotUnderstand: either\n",
                    buf, n ? name : "(not a symbol)");
        ST_report_backtrace();
        if (getenv("ST_LOOKUP_LOG")) {
            st_oop  cls = OM_fetch_class(receiver);
            char    cname[64];

            while (OM_is_present(cls)) {
                st_oop  d = OM_fetch_pointer(ST_CLASS_METHOD_DICT, cls);

                OM_class_name_of(cls, cname, sizeof cname);
                {
                    uint32_t cap = OM_method_dict_capacity(d);
                    uint32_t s2;
                    uint32_t used = 0;
                    int      here = 0;

                    for (s2 = 0; s2 < cap; ++s2) {
                        st_oop k = OM_method_dict_key(d, s2);

                        if (k != ST_NIL && OM_is_object(k))
                            ++used;
                        if (k == selector)
                            here = 1;
                    }
                    fprintf(stderr, "    in %s: capacity %u, %u used,"
                                    " selector %s\n",
                            cname[0] ? cname : "?", cap, used,
                            here ? "PRESENT" : "absent");
                }
                cls = OM_fetch_pointer(ST_CLASS_SUPERCLASS, cls);
            }
        }
        st_vm.running = 0;
        return;
    }
    st_vm.new_method      = method;
    st_vm.primitive_index = 0;
    activate_new_method();
}

static void execute_new_method(st_oop receiver, st_oop selector,
                               st_oop lookup_class, int traced);

void
ST_send_selector(st_oop selector, uint32_t argc)
{
    st_oop  receiver;
    st_oop  cls;

    st_vm.message_selector = selector;
    st_vm.argument_count   = argc;
    receiver = ST_stack_value(argc);
    cls      = OM_fetch_class(receiver);
    execute_new_method(receiver, selector, cls, 1);
}

static void
send_to_class(st_oop selector, uint32_t argc, st_oop lookup_class)
{
    st_oop  receiver;

    st_vm.message_selector = selector;
    st_vm.argument_count   = argc;
    receiver = ST_stack_value(argc);
    /*
     *  Super sends are untraced.  The Xerox tracer reports sends from the
     *  ordinary send path only, so the one super send in trace2 -- at cycle
     *  181 -- produces no line even though it activates a method and
     *  execution continues inside it at cycle 182.
     */
    execute_new_method(receiver, selector, lookup_class, 0);
}

static void
execute_new_method(st_oop receiver, st_oop selector, st_oop lookup_class,
                   int traced)
{
    st_oop      found;
    st_oop      method;
    st_oop      header;
    unsigned    flag;
    st_oop      args[8];
    uint32_t    i;

    method = lookup_method(selector, lookup_class, &found);

    for (i = 0; i < st_vm.argument_count && i < 8; ++i)
        args[i] = ST_stack_value(st_vm.argument_count - 1 - i);

    if (traced)
        ST_trace_send(receiver, selector, st_vm.argument_count, args);

    if (!OM_is_present(method)) {
        send_does_not_understand(receiver, selector, lookup_class);
        return;
    }
    st_vm.new_method = method;
    header = method_header(method);
    flag   = ST_header_flag_value(header);

    /*
     *  Flags 5 and 6 are the quick returns: no context is built and no
     *  primitive runs, which is why a method that merely answers an
     *  instance variable costs almost nothing.  They are still ordinary
     *  sends as far as the trace is concerned -- "aPoint x" appears in
     *  trace2 and Point>>x is a flag-6 method.
     */
    if (flag == 5) {
        ST_pop_n(st_vm.argument_count);
        /*  receiver is now on top; leave it as the result  */
        return;
    }
    if (flag == 6) {
        uint32_t    index = ST_header_temporary_count(header);
        st_oop      value = OM_fetch_pointer(index, receiver);

        ST_pop_n(st_vm.argument_count + 1);
        ST_push(value);
        return;
    }

    /*
     *  A method with a header extension carries its own argument count; it
     *  must agree with what the send bytecode supplied, or the stack frame
     *  we are about to build would be misaligned.
     */
    if (ST_header_flag_value(header) == 7
     && method_argument_count(method) != st_vm.argument_count) {
        fprintf(stderr, "st80: %u arguments sent to a method expecting %u "
                        "at cycle %llu\n",
                st_vm.argument_count, method_argument_count(method),
                (unsigned long long) st_vm.cycle);
        st_vm.running = 0;
        return;
    }
    st_vm.primitive_index = ST_method_primitive_index(method);
    if (st_vm.primitive_index > 0) {
        ST_trace_primitive(st_vm.primitive_index);
        st_vm.success = 1;
        if (ST_primitive_dispatch(st_vm.primitive_index))
            return;                     /*  the primitive answered  */
        /*  Otherwise fall through and run the method's Smalltalk body.  */
    }
    activate_new_method();
}

/*  ----------  Special selector sends  ----------  */

static void
send_special_selector(uint8_t code)
{
    uint32_t    index = (uint32_t) (code - 176) * 2;
    st_oop      selector;
    uint32_t    argc;

    st_vm.success = 1;
    if (ST_special_selector_primitive(code))
        return;

    selector = OM_fetch_pointer(index, ST_SPECIAL_SELECTORS);
    argc     = (uint32_t) OM_int_value(
                    OM_fetch_pointer(index + 1, ST_SPECIAL_SELECTORS));
    ST_send_selector(selector, argc);
}

/*  ----------  Startup  ----------  */

int
ST_interp_init(char *errbuf, size_t errlen)
{
    st_oop  scheduler;
    st_oop  process;
    st_oop  context;

    memset(&st_vm, 0, sizeof st_vm);

    scheduler = OM_fetch_pointer(ST_ASSOCIATION_VALUE, ST_SCHEDULER_ASSOCIATION);
    if (!OM_is_present(scheduler)) {
        if (errbuf)
            snprintf(errbuf, errlen, "scheduler association holds no scheduler");
        return -1;
    }
    /*
     *  Reconnect the VM to the image it was saved from.
     *
     *  Only if nothing is connected yet: a bootstrap has already made these
     *  connections for real, and the slots hold what it made.  A reload has
     *  the slots and nothing else, which is the case this exists for.
     */
    if (st_om_vm_state[ST_VM_DISPLAY] != ST_NIL
     && GFX_display_form() == ST_NIL)
        GFX_set_display(st_om_vm_state[ST_VM_DISPLAY]);
    if (st_om_vm_state[ST_VM_INPUT_SEMAPHORE] != ST_NIL
     && SCHED_input_semaphore() == ST_NIL)
        SCHED_set_input_semaphore(st_om_vm_state[ST_VM_INPUT_SEMAPHORE]);

    /*  ProcessorScheduler: activeProcess is its second instance variable.  */
    process = OM_fetch_pointer(1, scheduler);
    if (!OM_is_present(process)) {
        if (errbuf)
            snprintf(errbuf, errlen, "no active process in the scheduler");
        return -1;
    }
    /*  Process: suspendedContext is its first instance variable.  */
    context = OM_fetch_pointer(1, process);
    if (!OM_is_present(context)) {
        if (errbuf)
            snprintf(errbuf, errlen, "active process has no suspended context");
        return -1;
    }
    ST_interp_install_roots(extra_roots);
    ST_interp_register();
    st_vm.active_context = ST_NIL;
    set_active_context(context);
    st_vm.running = 1;
    st_vm.cycle   = 0;
    return 0;
}

/*  ----------  The dispatch loop  ----------  */

uint64_t
ST_interp_run(uint64_t limit)
{
    uint64_t    executed = 0;

    while (st_vm.running && (limit == 0 || executed < limit)) {
        uint8_t     code;
        uint32_t    ip_at_start;

        /*
         *  Once per bytecode, at a point where the registers are consistent:
         *  deliver queued interrupt signals and switch processes if one is
         *  pending.  Phase 7 puts the safepoint poll here too.
         */
        /*
         *  The safepoint poll.  A relaxed load in the common case; it parks
         *  only when a collection is asking, and at that moment this
         *  thread's registers are consistent and its roots are known.
         */
        WORKER_poll();
        SCHED_check_process_switch();
        if (!st_vm.running)
            break;
        ip_at_start = st_vm.instruction_pointer;
        code = next_byte();
        ++st_vm.cycle;
        ++executed;
        ST_trace_bytecode(code, st_vm.method, ip_at_start);

        switch (code) {
        /*  0-15: push receiver instance variable  */
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
            ST_push(OM_fetch_pointer(code, st_vm.receiver));
            break;

        /*  16-31: push temporary  */
        case 16: case 17: case 18: case 19: case 20: case 21: case 22:
        case 23: case 24: case 25: case 26: case 27: case 28: case 29:
        case 30: case 31:
            ST_push(OM_fetch_pointer(ST_CTX_TEMP_FRAME_START + (code - 16),
                                     st_vm.home_context));
            break;

        /*  32-63: push literal constant  */
        case 32: case 33: case 34: case 35: case 36: case 37: case 38:
        case 39: case 40: case 41: case 42: case 43: case 44: case 45:
        case 46: case 47: case 48: case 49: case 50: case 51: case 52:
        case 53: case 54: case 55: case 56: case 57: case 58: case 59:
        case 60: case 61: case 62: case 63:
            ST_push(method_literal(code - 32, st_vm.method));
            break;

        /*  64-95: push the value of a literal variable (an Association)  */
        case 64: case 65: case 66: case 67: case 68: case 69: case 70:
        case 71: case 72: case 73: case 74: case 75: case 76: case 77:
        case 78: case 79: case 80: case 81: case 82: case 83: case 84:
        case 85: case 86: case 87: case 88: case 89: case 90: case 91:
        case 92: case 93: case 94: case 95:
            ST_push(OM_fetch_pointer(ST_ASSOCIATION_VALUE,
                                     method_literal(code - 64, st_vm.method)));
            break;

        /*  96-103: pop and store receiver instance variable  */
        case 96: case 97: case 98: case 99:
        case 100: case 101: case 102: case 103:
            OM_store_pointer(code - 96, st_vm.receiver, ST_pop());
            break;

        /*  104-111: pop and store temporary  */
        case 104: case 105: case 106: case 107:
        case 108: case 109: case 110: case 111:
            OM_store_pointer(ST_CTX_TEMP_FRAME_START + (code - 104),
                             st_vm.home_context, ST_pop());
            break;

        case 112: ST_push(st_vm.receiver); break;
        case 113: ST_push(ST_TRUE);  break;
        case 114: ST_push(ST_FALSE); break;
        case 115: ST_push(ST_NIL);   break;
        case 116: ST_push(OM_int_oop(-1)); break;
        case 117: ST_push(OM_int_oop(0));  break;
        case 118: ST_push(OM_int_oop(1));  break;
        case 119: ST_push(OM_int_oop(2));  break;

        case 120: return_value(st_vm.receiver); break;
        case 121: return_value(ST_TRUE);  break;
        case 122: return_value(ST_FALSE); break;
        case 123: return_value(ST_NIL);   break;
        case 124: return_value(ST_pop()); break;
        case 125: {
            /*  Return from a block: the caller, not the home's sender.  */
            st_oop  value  = ST_pop();
            st_oop  caller = OM_fetch_pointer(ST_CTX_CALLER,
                                              st_vm.active_context);

            do_return(value, caller, 1);
            break;
        }

        case 128: {         /*  extended push  */
            uint8_t     desc  = next_byte();
            uint32_t    kind  = desc >> 6;
            uint32_t    index = desc & 63;

            if (kind == 0)
                ST_push(OM_fetch_pointer(index, st_vm.receiver));
            else if (kind == 1)
                ST_push(OM_fetch_pointer(ST_CTX_TEMP_FRAME_START + index,
                                         st_vm.home_context));
            else if (kind == 2)
                ST_push(method_literal(index, st_vm.method));
            else
                ST_push(OM_fetch_pointer(ST_ASSOCIATION_VALUE,
                                         method_literal(index, st_vm.method)));
            break;
        }
        case 129:           /*  extended store  */
        case 130: {         /*  extended pop and store  */
            uint8_t     desc  = next_byte();
            uint32_t    kind  = desc >> 6;
            uint32_t    index = desc & 63;
            st_oop      value = (code == 130) ? ST_pop() : ST_stack_top();

            if (kind == 0)
                OM_store_pointer(index, st_vm.receiver, value);
            else if (kind == 1)
                OM_store_pointer(ST_CTX_TEMP_FRAME_START + index,
                                 st_vm.home_context, value);
            else if (kind == 3)
                OM_store_pointer(ST_ASSOCIATION_VALUE,
                                 method_literal(index, st_vm.method), value);
            break;
        }

        case 131: {         /*  single extended send  */
            uint8_t     desc = next_byte();

            ST_send_selector(method_literal(desc & 31, st_vm.method),
                             (uint32_t) (desc >> 5));
            break;
        }
        case 132: {         /*  double extended send  */
            uint8_t     argc = next_byte();
            uint8_t     lit  = next_byte();

            ST_send_selector(method_literal(lit, st_vm.method), argc);
            break;
        }
        case 133: {         /*  single extended send to super  */
            uint8_t     desc = next_byte();
            st_oop      cls  = method_literal(
                                    ST_header_literal_count(
                                        method_header(st_vm.method)) - 1,
                                    st_vm.method);

            send_to_class(method_literal(desc & 31, st_vm.method),
                          (uint32_t) (desc >> 5),
                          OM_fetch_pointer(ST_CLASS_SUPERCLASS,
                                           OM_fetch_pointer(ST_ASSOCIATION_VALUE, cls)));
            break;
        }
        case 134: {         /*  double extended send to super  */
            uint8_t     argc = next_byte();
            uint8_t     lit  = next_byte();
            st_oop      cls  = method_literal(
                                    ST_header_literal_count(
                                        method_header(st_vm.method)) - 1,
                                    st_vm.method);

            send_to_class(method_literal(lit, st_vm.method), argc,
                          OM_fetch_pointer(ST_CLASS_SUPERCLASS,
                                           OM_fetch_pointer(ST_ASSOCIATION_VALUE, cls)));
            break;
        }

        case 135: ST_pop_n(1); break;
        case 136: ST_push(ST_stack_top()); break;
        case 137: ST_push(st_vm.active_context); break;

        /*
         *  ----------  138, 140-143: the closure set  ----------
         *
         *  Squeak's V3PlusClosures assignment, which occupies exactly the
         *  codes the Blue Book leaves unassigned.  None of these is emitted
         *  when compiling in the Blue Book dialect, and none is reachable at
         *  all in a build with no BlockClosure, so the 1983 image never
         *  meets one.
         */

        /*
         *  138: push a new Array.  The top bit of the operand says the
         *  elements come off the stack, deepest first, which is how a
         *  dynamic array { a. b } is built; without it the array arrives
         *  full of nil, which is how a shared temporary vector starts.
         */
        case 138: {
            uint8_t     descriptor = next_byte();
            uint32_t    size = descriptor & 127;
            st_oop      array = OM_instantiate_pointers(ST_CLASS_ARRAY, size);

            if (!OM_is_object(array)) {
                st_vm.running = 0;
                break;
            }
            if (descriptor & 128) {
                uint32_t    i;

                for (i = 0; i < size; ++i)
                    OM_store_pointer(i, array, ST_stack_value(size - 1 - i));
                ST_pop_n(size);
            }
            ST_push(array);
            break;
        }

        /*
         *  140-142: a temporary shared with a block that outlives its
         *  frame.  It lives in an Array held by a frame slot -- the vector
         *  -- so that copying the vector into a closure shares the variable
         *  rather than its value.  The operands are the slot within the
         *  vector and then the frame temporary holding the vector.
         */
        case 140: {
            uint32_t    index = next_byte();
            uint32_t    vector = next_byte();

            ST_push(OM_fetch_pointer(index,
                        OM_fetch_pointer(ST_CTX_TEMP_FRAME_START + vector,
                                         st_vm.home_context)));
            break;
        }
        case 141: {
            uint32_t    index = next_byte();
            uint32_t    vector = next_byte();

            OM_store_pointer(index,
                OM_fetch_pointer(ST_CTX_TEMP_FRAME_START + vector,
                                 st_vm.home_context),
                ST_stack_top());
            break;
        }
        case 142: {
            uint32_t    index = next_byte();
            uint32_t    vector = next_byte();

            OM_store_pointer(index,
                OM_fetch_pointer(ST_CTX_TEMP_FRAME_START + vector,
                                 st_vm.home_context),
                ST_pop());
            break;
        }

        /*
         *  143: make a closure and skip its body.
         *
         *  The body is inline in the enclosing method, so the closure needs
         *  only where it starts; the jump past it is part of this bytecode
         *  rather than a separate one, which is where it differs from the
         *  Blue Book's blockCopy: followed by a jump.
         */
        case 143: {
            uint8_t     counts = next_byte();
            uint32_t    high = next_byte();
            uint32_t    low = next_byte();
            uint32_t    block_size = high * 256 + low;
            uint32_t    argc = counts & 15;
            uint32_t    copied = (uint32_t) counts >> 4;
            st_oop      closure_class =
                            st_om_vm_state[ST_VM_CLASS_BLOCK_CLOSURE];
            st_oop      closure;
            uint32_t    i;

            if (!OM_is_present(closure_class)) {
                fprintf(stderr, "st80: a closure was built with no "
                                "BlockClosure loaded\n");
                st_vm.running = 0;
                break;
            }
            closure = OM_instantiate_pointers(closure_class,
                                              ST_CLOSURE_FIRST_COPIED + copied);
            if (!OM_is_object(closure)) {
                st_vm.running = 0;
                break;
            }
            OM_store_pointer(ST_CLOSURE_OUTER_CONTEXT, closure,
                             st_vm.active_context);
            /*  One-relative, like every instruction pointer in a context.  */
            OM_store_pointer(ST_CLOSURE_STARTPC, closure,
                             OM_int_oop((st_int) st_vm.instruction_pointer + 1));
            OM_store_pointer(ST_CLOSURE_NUM_ARGS, closure,
                             OM_int_oop((st_int) argc));
            for (i = 0; i < copied; ++i)
                OM_store_pointer(ST_CLOSURE_FIRST_COPIED + i, closure,
                                 ST_stack_value(copied - 1 - i));
            ST_pop_n(copied);
            ST_push(closure);
            st_vm.instruction_pointer += block_size;
            break;
        }

        /*  144-151: unconditional short jump  */
        case 144: case 145: case 146: case 147:
        case 148: case 149: case 150: case 151:
            st_vm.instruction_pointer += (uint32_t) (code - 143);
            break;

        /*  152-159: pop and jump on false, short  */
        case 152: case 153: case 154: case 155:
        case 156: case 157: case 158: case 159: {
            st_oop  value = ST_pop();

            if (value == ST_FALSE)
                st_vm.instruction_pointer += (uint32_t) (code - 151);
            else if (value != ST_TRUE)
                ST_must_be_boolean(value);
            break;
        }

        /*  160-167: unconditional long jump  */
        case 160: case 161: case 162: case 163:
        case 164: case 165: case 166: case 167: {
            int32_t offset = ((int32_t) (code - 164) * 256) + next_byte();

            st_vm.instruction_pointer =
                (uint32_t) ((int32_t) st_vm.instruction_pointer + offset);
            break;
        }

        /*  168-171: pop and jump on true, long  */
        case 168: case 169: case 170: case 171: {
            int32_t offset = ((int32_t) (code - 168) * 256) + next_byte();
            st_oop  value  = ST_pop();

            if (value == ST_TRUE)
                st_vm.instruction_pointer =
                    (uint32_t) ((int32_t) st_vm.instruction_pointer + offset);
            else if (value != ST_FALSE)
                ST_must_be_boolean(value);
            break;
        }

        /*  172-175: pop and jump on false, long  */
        case 172: case 173: case 174: case 175: {
            int32_t offset = ((int32_t) (code - 172) * 256) + next_byte();
            st_oop  value  = ST_pop();

            if (value == ST_FALSE)
                st_vm.instruction_pointer =
                    (uint32_t) ((int32_t) st_vm.instruction_pointer + offset);
            else if (value != ST_TRUE)
                ST_must_be_boolean(value);
            break;
        }

        /*  176-207: arithmetic and special selector sends  */
        case 176: case 177: case 178: case 179: case 180: case 181:
        case 182: case 183: case 184: case 185: case 186: case 187:
        case 188: case 189: case 190: case 191:
        case 192: case 193: case 194: case 195: case 196: case 197:
        case 198: case 199: case 200: case 201: case 202: case 203:
        case 204: case 205: case 206: case 207:
            send_special_selector(code);
            break;

        default:
            if (code >= 208) {
                uint32_t    argc = (uint32_t) (code - 208) / 16;
                uint32_t    lit  = (uint32_t) (code - 208) % 16;

                ST_send_selector(method_literal(lit, st_vm.method), argc);
            }  else  {
                fprintf(stderr, "st80: unused bytecode %u at cycle %llu\n",
                        (unsigned) code, (unsigned long long) st_vm.cycle);
                st_vm.running = 0;
            }
            break;
        }
    }
    return executed;
}

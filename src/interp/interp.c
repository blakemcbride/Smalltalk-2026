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
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "st_socket.h"

_Thread_local st_interp     st_vm;

static int  name_method(st_oop receiver, st_oop method, char *out, size_t len);

/*
 *  Every running interpreter, so the collector can walk them all.  Written
 *  when a thread joins or leaves and read only at a safepoint, where by
 *  construction nothing is joining or leaving.
 */
#define MAX_INTERPRETERS    ST_MAX_INTERPRETERS

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

        if (ST_cas_strong(&interpreters[i], &empty, (uintptr_t) &st_vm)) {
            /*  The row this slot maps to, cleared of whatever the last
             *  thread to hold the slot left in it.  */
            SCHED_hands_register(i);
            return;
        }
    }
}

void
ST_interp_unregister(void)
{
    unsigned    i;

    for (i = 0; i < MAX_INTERPRETERS; ++i) {
        uintptr_t   mine = (uintptr_t) &st_vm;

        if (ST_cas_strong(&interpreters[i], &mine, 0)) {
            /*
             *  After the table no longer names this thread, and before
             *  its thread-locals can be freed: the row is static storage,
             *  so a scanner that read it a moment ago reads nil, never a
             *  dangling thread-local.
             */
            SCHED_hands_unregister();
            return;
        }
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
    /*
     *  Never outside the context, whatever the compiler estimated.
     *
     *  The frame size is computed from `temporaries + max_stack_depth', and
     *  max_stack_depth is not always an upper bound: Pharo's
     *  DateAndTimeLeapTest overflows a context sized exactly to it.  Sizing
     *  contexts to that estimate is still right and fixed the common case,
     *  but the interpreter cannot be the thing that trusts it.  A push past
     *  the end writes into the next object's header and glibc reports
     *  `corrupted size vs. prev_size' thousands of bytecodes later, in
     *  whatever unrelated code next touches the heap -- a fault with no
     *  path back to its cause.
     *
     *  One comparison, to turn that into a report.  It has to be against a
     *  CACHED length: reading it from the context per push is an
     *  object-table dereference in the hottest path in the system and cost
     *  8-12% on every kernel --
     *
     *      arithmetic 20.0 -> 23.2 ms, mandelbrot 50.0 -> 54.9,
     *      intervals 47.0 -> 49.6, collections 46.0 -> 51.2
     *
     *  -- while against st_vm.stack_limit, loaded once per context switch,
     *  it is 18.8, 44.9, 45.1 and 48.7.  Which is to say: free, and the
     *  first version was not.
     */
    uint32_t    next = st_vm.stack_pointer + 1;

    if (next >= st_vm.stack_limit) {
        char    name[200];

        /*  Named whether or not errors are being reported: an overflow
         *  ends the run, and the method is the whole of the diagnosis.  */
        if (!name_method(st_vm.receiver, st_vm.method, name, sizeof name))
            snprintf(name, sizeof name, "?");
        fprintf(stderr, "st80: a method overflowed its frame at %u slots "
                        "(the context holds %u); its declared frame is too "
                        "small: %s\n",
                next, (unsigned) st_vm.stack_limit, name);
        ST_set_error_reporting(1);
        ST_report_backtrace();
        /*  And the chain as the scheduler sees it, registers written back.  */
        ST_store_active_context();
        ST_interp_dump_workers();
        st_vm.running = 0;
        return;
    }
    st_vm.stack_pointer = next;
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

uint32_t
ST_stack_room(void)
{
    /*
     *  ST_push refuses when stack_pointer + 1 reaches stack_limit, so the
     *  last slot it will fill is stack_limit - 1 and the room is the gap
     *  between there and where the pointer stands.  A pointer already at or
     *  past the limit -- possible only in a context somebody has been
     *  writing to by hand -- has no room at all rather than a wrapped
     *  four billion.
     */
    if (st_vm.stack_pointer + 1 >= st_vm.stack_limit)
        return 0;
    return st_vm.stack_limit - 1 - st_vm.stack_pointer;
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

static void corrupt_method(const char *reason);

/*
 *  The next byte of the running method, or 0 with the fault recorded.
 *
 *  Bounded by st_vm.method_end (Bugs3 B11).  This is called for opcodes
 *  from the top of the dispatch loop, which checks the bound itself before
 *  calling and delivers the fault there; and for OPERANDS from inside a
 *  handler, where nothing can be delivered because the handler is half way
 *  through a bytecode.  So an operand fetch past the end answers 0, leaves
 *  the pointer where it is, and writes the reason down: the pointer is now
 *  at the end, and the top of the loop finds it there before the next
 *  bytecode is fetched.  One bytecode runs with a zero operand first, which
 *  is a push of the first field or a jump of zero, and then the activation
 *  is abandoned.
 */
static uint8_t
next_byte(void)
{
    uint8_t b;

    if (st_vm.instruction_pointer >= st_vm.method_end) {
        if (!st_vm.corrupt_reason)
            st_vm.corrupt_reason = "an instruction pointer past the end of "
                                   "the method";
        return 0;
    }
    b = OM_fetch_byte(st_vm.instruction_pointer, st_vm.method);
    ++st_vm.instruction_pointer;
    return b;
}

/*
 *  A literal of the running method, by index from a bytecode.
 *
 *  ST_OOP_INVALID, with the activation already abandoned, when the index
 *  is past the method's literal frame -- every caller must test for that
 *  and touch nothing afterwards, because the context it was executing in
 *  is no longer the active one.  The Blue Book fetches the literal
 *  unchecked, which is right for bytecodes a compiler wrote and was the
 *  segfault in Bugs3 B11 for bytecodes anything else wrote: 255 is `send
 *  literal 15 with two arguments', and a method with one literal has its
 *  first bytecode where literal 15 would be.  The comparison is against a
 *  register cached with the method, so it costs what the stack-limit
 *  comparison in ST_push costs, which was measured at nothing.
 */
static st_oop
literal_at(uint32_t index)
{
    if (index >= st_vm.literal_limit) {
        corrupt_method("a literal index past the method's literal frame");
        return ST_OOP_INVALID;
    }
    return OM_fetch_pointer(ST_METHOD_LITERAL_START + index, st_vm.method);
}

/*
 *  The same two questions for the other two things a bytecode indexes by
 *  a number it carries: an instance variable of the receiver, and a
 *  temporary of the home context.  Both bounds are cached registers
 *  (st_vm.receiver_limit, st_vm.home_limit), so each is the one comparison
 *  literal_at makes.  A bytecode the compiler wrote can never fail them;
 *  one written through CompiledMethod>>at:put: can, and did -- ASAN caught
 *  `push instance variable 0' on an instance with no instance variables
 *  reading the word after the object.  The activation is abandoned with
 *  a CorruptMethod, as for a bad literal.
 */
static int
receiver_variable_ok(uint32_t index)
{
    if (index >= st_vm.receiver_limit) {
        corrupt_method("an instance-variable index past the receiver's "
                       "fields");
        return 0;
    }
    return 1;
}

static int
temporary_ok(uint32_t index)
{
    if (ST_CTX_TEMP_FRAME_START + index >= st_vm.home_limit) {
        corrupt_method("a temporary index past the home context");
        return 0;
    }
    return 1;
}

/*  The receiver's pointer-field count, or zero when it has no fields a
 *  bytecode may name: a SmallInteger, or a byte or word object.  */
static uint32_t
receiver_field_count(st_oop receiver)
{
    if (!OM_is_object(receiver))
        return 0;
#if defined(ST_OM_MT)
    if (!(OM_head(receiver)->flags & ST_FMT_POINTERS))
        return 0;
#else
    if (!OM_pointer_bit(receiver))
        return 0;
#endif
    return OM_fetch_word_length(receiver);
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
    /*
     *  The last slot this context has, cached here rather than read from
     *  the object on every push.  Fetching it per push cost 8-12% across
     *  every benchmark kernel -- it is an object-table dereference in the
     *  hottest path in the system.  Loaded once per context switch it is
     *  a comparison against a field already in cache.
     */
    st_vm.stack_limit = OM_fetch_word_length(ctx);
    /*  And the two an indexing bytecode is checked against; see
     *  receiver_variable_ok and temporary_ok.  */
    st_vm.receiver_limit = receiver_field_count(st_vm.receiver);
    st_vm.home_limit     = OM_fetch_word_length(st_vm.home_context);

    /*
     *  Nothing below is believed until it has been looked at (Bugs3 B11).
     *
     *  A context's ip and sp are ordinary instance variables, and
     *  `thisContext sender sender instVarAt: 2 put: -1' is an ordinary
     *  statement; when that context was returned to, the registers came
     *  from it as they stood and the next push wrote over the context's own
     *  header.  The checks are two comparisons per context switch, against
     *  values this function has just fetched anyway.
     *
     *  This cannot raise -- it runs inside do_return and inside a process
     *  switch, with no bytecode in flight -- so a bad register is CLAMPED
     *  to something harmless, the reason is written down, and the
     *  instruction pointer is put at the end of the method, where the top
     *  of the dispatch loop finds it before fetching anything and abandons
     *  the activation with #corruptMethod.
     */
    {
        st_oop      ip_field = fetch_integer(ST_CTX_IP, ctx);
        st_oop      sp_field = fetch_integer(ST_CTX_SP, ctx);
        st_int      ip = OM_is_int(ip_field) ? OM_int_value(ip_field) : -1;
        st_int      sp = OM_is_int(sp_field) ? OM_int_value(sp_field) : -1;
        const char *fault = NULL;

        if (!OM_is_object(st_vm.method)
         || OM_fetch_class(st_vm.method) != ST_CLASS_COMPILED_METHOD) {
            st_vm.literal_limit = 0;
            st_vm.method_end    = 0;
            fault = "a context whose method is not a CompiledMethod";
        }  else  {
            st_vm.literal_limit =
                ST_header_literal_count(method_header(st_vm.method));
            st_vm.method_end = OM_fetch_byte_length(st_vm.method);
            /*  One-relative, and never inside the header and literals.  */
            if (ip < 1 || (uint32_t) (ip - 1) > st_vm.method_end
             || (uint32_t) (ip - 1) < method_initial_ip(st_vm.method))
                fault = "a context whose instruction pointer is outside "
                        "its method's bytecodes";
        }
        /*  Zero is an empty stack; the top must stay inside the object.  */
        if (sp < 0
         || (uint32_t) sp + ST_CTX_TEMP_FRAME_START - 1 >= st_vm.stack_limit)
            fault = "a context whose stack pointer is outside the context";

        if (fault) {
            if (!st_vm.corrupt_reason)
                st_vm.corrupt_reason = fault;
            st_vm.instruction_pointer = st_vm.method_end;
            st_vm.stack_pointer       = ST_CTX_TEMP_FRAME_START - 1;
            return;
        }
        st_vm.instruction_pointer = (uint32_t) ip - 1;
        st_vm.stack_pointer = (uint32_t) sp + ST_CTX_TEMP_FRAME_START - 1;
    }
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
     *  Named before it is fetched from: a Process whose suspendedContext is
     *  not a context, or a sender that is not one, used to be read as if it
     *  were, and the registers filled from a String's bytes ran until
     *  next_byte dereferenced them.  The report is the diagnosis -- the
     *  chain that made the switch, and every parked process -- and the run
     *  stops on the context it still has.
     */
    if (!OM_is_object(ctx)
     || (OM_fetch_class(ctx) != ST_CLASS_METHOD_CONTEXT
      && OM_fetch_class(ctx) != ST_CLASS_BLOCK_CONTEXT)) {
        char    name[200];

        if (!name_method(st_vm.receiver, st_vm.method, name, sizeof name))
            snprintf(name, sizeof name, "?");
        fprintf(stderr, "st80: asked to run %s (oop %#llx) as a context, "
                        "from %s\n",
                OM_is_int(ctx) ? "a SmallInteger"
                : !OM_is_object(ctx) ? "something that is not an object"
                : "an object that is not a context",
                (unsigned long long) ctx, name);
        if (OM_is_object(st_vm.active_process)) {
            char    cname[100];

            if (!OM_class_name_of(OM_fetch_class(st_vm.active_process),
                                  cname, sizeof cname))
                snprintf(cname, sizeof cname, "?");
            fprintf(stderr, "       the active process oop %#llx is now %s "
                            "with count %u\n",
                    (unsigned long long) st_vm.active_process, cname,
                    OM_count_bits(st_vm.active_process));
        }
        ST_set_error_reporting(1);
        ST_report_backtrace();
        ST_store_active_context();
        ST_interp_dump_workers();
        st_vm.running = 0;
        return;
    }
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

/*  How far back a chain is printed, here and by ST_report_backtrace.  */
#define BACKTRACE_LIMIT     24

void
ST_interp_dump_workers(void)
{
    unsigned    i;

    for (i = 0; i < MAX_INTERPRETERS; ++i) {
        st_interp  *vm = (st_interp *) ST_load_acquire(&interpreters[i]);
        st_oop      p, l;
        long        priority = -1;
        char        list[64] = "no list";

        if (!vm)
            continue;
        p = vm->active_process;
        l = OM_is_present(p) ? OM_fetch_pointer(ST_PROCESS_MY_LIST, p) : ST_NIL;
        if (OM_is_present(p) && OM_is_int(OM_fetch_pointer(ST_PROCESS_PRIORITY, p)))
            priority = (long) OM_int_value(OM_fetch_pointer(ST_PROCESS_PRIORITY, p));
        if (OM_is_present(l))
            ST_print_object(l, list, sizeof list);
        fprintf(stderr, "       interpreter %u: running %d, parked %d, nominated %d, "
                        "active process at priority %ld waiting on %s\n",
                i, vm->running, vm->disowned, vm->new_process_waiting,
                priority, list);
        /*
         *  And where that process is.  A verdict that names a semaphore
         *  and not the method waiting on it leaves the reader to guess
         *  which of the image's locks was taken twice; the chain names it
         *  in one line.  From the process's parked context, because the
         *  registers of an idle worker are stale by construction.
         */
        if (OM_is_present(p)) {
            st_oop      ctx = OM_fetch_pointer(ST_PROCESS_SUSPENDED_CONTEXT, p);
            unsigned    depth = 0;

            while (OM_is_present(ctx) && depth < BACKTRACE_LIMIT) {
                st_oop  method   = OM_fetch_pointer(ST_CTX_METHOD, ctx);
                st_oop  receiver = OM_fetch_pointer(ST_CTX_RECEIVER, ctx);
                char    name[200];

                if (!OM_is_present(method)
                 || !name_method(receiver, method, name, sizeof name))
                    snprintf(name, sizeof name, "?");
                fprintf(stderr, "           %s %s\n", depth ? "from" : "in", name);
                ctx = OM_fetch_pointer(ST_CTX_SENDER, ctx);
                ++depth;
            }
        }
    }
}

/*
 *  A token is an oop the network layer carries without knowing what it is;
 *  its visitor takes a user pointer and ours does not, so the visitor of
 *  the moment sits in a static.  The root walk runs on one thread, at a
 *  safepoint, so a static is exactly enough.
 */
static om_visit_fn  token_visitor;

static void
visit_token(uintptr_t token, void *user)
{
    (void) user;
    token_visitor((st_oop) token);
}

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

        if (vm) {
            visit(vm->active_context);
            /*  And the processes only this worker's scheduler holds.  */
            visit(vm->active_process);
            visit(vm->new_process);
        }
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
    visit(SCHED_timer_semaphore());
    /*
     *  The Semaphores the network layer will signal, and the ones already
     *  queued for delivery.  Held in C -- a socket table and the async
     *  queue -- and reachable from nowhere in the image once the Socket
     *  that owned them is dropped; visited here directly, beside the timer
     *  semaphore, rather than through extra_roots, which a -run has none
     *  of and a socket can be opened from any mode.
     */
    token_visitor = visit;
    NET_visit_tokens(visit_token, NULL);
    SCHED_visit_async_roots(visit);
    visit(SCHED_pending_process());
    visit(st_om_vm_state[ST_VM_INPUT_SEMAPHORE]);
    visit(st_om_vm_state[ST_VM_DISPLAY]);
    visit(st_om_vm_state[ST_VM_CLASS_BLOCK_CLOSURE]);
    visit(st_om_vm_state[ST_VM_SELECTOR_ABOUT_TO_RETURN]);
    visit(st_om_vm_state[ST_VM_SELECTOR_OUT_OF_MEMORY]);
    visit(st_om_vm_state[ST_VM_SELECTOR_DEPTH_EXCEEDED]);
    if (extra_roots)
        extra_roots(visit);
}

/*
 *  ----------  The other side of a one-way become  ----------
 *
 *  OM_forward_identity rewrites every reference in the heap.  These two
 *  answer for the references that are NOT in the heap: the interpreter's
 *  registers, and the handful of objects C holds in variables of its own.
 *
 *  They are split into `can this be forwarded at all' and `forward it',
 *  asked in that order, because a refusal has to be a refusal -- the
 *  primitive fails and nothing has moved.  Doing it the other way would
 *  mean discovering half way through the sweep that the answer was no.
 */
/*
 *  Thread-local, because this predicate is asked outside a safepoint.
 *
 *  ST_interp_forward_forbidden runs on whichever worker is asking, and
 *  primitive 249 asks it once per element of a bulk forward with every other
 *  worker still running.  Two workers asking at once through one pair of
 *  globals answer each other's question: the first stores its oop, the
 *  second overwrites it, and the first reads a `found' that belongs to a
 *  search it did not make.  That is a wrong answer, not merely a race the
 *  sanitizer would name -- a legitimate become: refused, or worse, a
 *  forbidden one allowed.
 */
static _Thread_local st_oop   probe_for;
static _Thread_local int      probe_found;

static void
probe_visit(st_oop p)
{
    if (p == probe_for)
        probe_found = 1;
}

int
ST_interp_swap_forbidden(st_oop p)
{
    unsigned    i;

    if (!OM_is_object(p))
        return 1;
    /*
     *  A context or a method that some worker is executing IN.  The
     *  interpreter caches an instruction pointer into the method's
     *  bytecodes and a stack limit from the context's length, and both stop
     *  meaning anything the moment the object underneath them changes.
     *  Nothing sensible forwards either; refusing says so rather than
     *  leaving a worker reading freed memory.
     */
    for (i = 0; i < MAX_INTERPRETERS; ++i) {
        st_interp  *vm = (st_interp *) ST_load_acquire(&interpreters[i]);

        if (vm && (p == vm->active_context || p == vm->home_context
                || p == vm->method))
            return 1;
    }
    if (p == st_vm.active_context || p == st_vm.home_context
     || p == st_vm.method)
        return 1;
    /*
     *  Held by C in variables with no setter this can reach.  Each one has
     *  a single owner elsewhere in the system, and a forward would leave
     *  that owner holding a freed object.
     */
    if (p == GFX_display_form() || p == SCHED_input_semaphore()
     || p == SCHED_timer_semaphore() || p == SCHED_pending_process())
        return 1;
    /*  A Semaphore the socket table names: the I/O thread will signal it.  */
    if (NET_holds_token((uintptr_t) p))
        return 1;
    return 0;
}

/*
 *  And the same question for a FORWARD, which is the swap's question plus
 *  one more.
 *
 *  The extra one is everything the bootstrap holds -- its symbol table and
 *  its class table, C arrays of oops with no setter this can reach.
 *  Forwarding rewrites references, and it cannot rewrite those, so an object
 *  in either would be left named by a table pointing at a dead entry.
 *
 *  A SWAP does not ask this, and that distinction is the difference between
 *  a working image and fifty-five thousand refusals.  Swapping exchanges
 *  BODIES and leaves every object pointer naming a live object, so a C array
 *  of oops is as valid afterwards as before -- and Smalltalk itself is in
 *  that array, while SystemDictionary>>grow is a become:, so every global
 *  defined at run time goes through one.  What a swap must still refuse is
 *  an object whose BODY some C register has cached a raw pointer into,
 *  which is what the test above covers.
 */
int
ST_interp_forward_forbidden(st_oop p)
{
    if (ST_interp_swap_forbidden(p))
        return 1;
    if (extra_roots) {
        probe_for   = p;
        probe_found = 0;
        extra_roots(probe_visit);
        probe_for = ST_OOP_INVALID;
        if (probe_found)
            return 1;
    }
    return 0;
}

static void
forward_register(st_oop *reg, st_oop from, st_oop to)
{
    if (*reg == from)
        *reg = to;
}

void
ST_interp_forward_roots(st_oop from, st_oop to)
{
    unsigned    i;

    /*
     *  Every worker's registers, not just this one's -- they are all parked
     *  at a safepoint, which is the only moment another thread's register
     *  file may be written.
     *
     *  active_context, home_context and method are not here: they are the
     *  set ST_interp_forward_forbidden refuses, so by the time this runs
     *  none of them can be `from'.
     */
    for (i = 0; i < MAX_INTERPRETERS; ++i) {
        st_interp  *vm = (st_interp *) ST_load_acquire(&interpreters[i]);

        if (!vm)
            continue;
        forward_register(&vm->receiver, from, to);
        forward_register(&vm->message_selector, from, to);
        forward_register(&vm->new_method, from, to);
        forward_register(&vm->return_value, from, to);
        forward_register(&vm->active_process, from, to);
        forward_register(&vm->new_process, from, to);
    }
    /*
     *  And this thread's, registered or not: the -eval path never registers
     *  and is exactly where a doIt sends becomeForward:.
     */
    forward_register(&st_vm.receiver, from, to);
    forward_register(&st_vm.message_selector, from, to);
    forward_register(&st_vm.new_method, from, to);
    forward_register(&st_vm.return_value, from, to);
    forward_register(&st_vm.active_process, from, to);
    forward_register(&st_vm.new_process, from, to);
}

void
ST_interp_install_roots(om_root_provider extra)
{
    extra_roots = extra;
    OM_set_root_provider(provide_roots);
    OM_set_root_forwarder(ST_interp_forward_roots, ST_interp_forward_forbidden);
    OM_set_swap_guard(ST_interp_swap_forbidden);
}

/*
 *  Write this worker's registers into its active context -- unless the
 *  worker has parked that process and handed it on.  Then the context is
 *  one another worker may be executing, and the registers here are stale
 *  by exactly as much as that worker has since done.  The safepoint poll
 *  calls this on every worker before a collection, idle workers included,
 *  and an idle worker is precisely one whose last process was parked and
 *  taken: every collection wrote a parked process's old registers over a
 *  running one's, and it looked like anything -- a SortedCollection whose
 *  compare answered nil, Smalltalk reading as nil, a frame overflowed.
 *  The scheduler's own switch already skipped the store for a disowned
 *  process; this makes every caller skip it.
 */
void
ST_store_active_context(void)
{
    if (st_vm.disowned)
        return;
    store_context_registers();
}

void
ST_set_active_context(st_oop ctx)
{
    set_active_context(ctx);
    /*
     *  A process switch replaces the whole stack, so the depth of the one
     *  that just left says nothing about the one arriving.  See
     *  ST_return_to.
     */
    st_vm.call_depth      = ST_stack_depth();
    st_vm.depth_signalled = 0;
}

/*  ----------  Method lookup  ----------  */

/*
 *  Walk the superclass chain looking for the selector.  The method
 *  dictionary layout was validated against Xerox's method.oops dump before
 *  the interpreter was written, so this traverses known-good structure.
 */
/*
 *  How long a superclass chain may be before it is taken for a cycle.
 *
 *  `CA superclass: CB. CB superclass: CA. CA new zork' spun this worker in
 *  C for ever (Bugs3 B6): the walk below has no safepoint in it, so SIGTERM
 *  could not stop the process and the next collection parked every other
 *  worker behind the one that never arrived.  The deepest chain in any
 *  image this system builds is under twenty classes; a walk that has taken
 *  this many steps is going round, and answering `not found' turns the spin
 *  into a doesNotUnderstand -- delivered through the same bounded walk, and
 *  caught by the root-class fallback in send_does_not_understand when the
 *  cycle hides Object as well.  Behavior>>superclass: in lib/ refuses to
 *  make a cycle in the first place; this is for the ones made without
 *  asking, through instVarAt:put: or an image built by hand.
 */
#define MAX_SUPERCLASS_CHAIN    4096

static st_oop
lookup_method(st_oop selector, st_oop start_class, st_oop *found_class)
{
    st_oop      cls = start_class;
    unsigned    hops = 0;

    /*  A nil superclass is the top of the chain, so the walk stops there.  */
    while (OM_is_present(cls)) {
        st_oop      dict = OM_fetch_pointer(ST_CLASS_METHOD_DICT, cls);
        uint32_t    capacity = OM_method_dict_capacity(dict);
        uint32_t    slot;

        if (++hops > MAX_SUPERCLASS_CHAIN)
            break;                      /*  a cycle: see the constant  */

        /*
         *  Probe from the selector's hash, the way the dictionary was
         *  filled, and stop at the first empty slot.
         *
         *  This used to scan every slot of every dictionary in the chain,
         *  which is correct and is why it was never noticed: it finds the
         *  method wherever it is.  It also made OM_method_dict_key alone
         *  18-33% of every profile taken of this system, because a send
         *  costs one comparison per SLOT per class rather than one per
         *  class.
         *
         *  Stopping at nil is safe because method_dictionary_at_put keeps
         *  the load factor under three quarters precisely so that a
         *  dictionary always has a nil on any probe path -- its comment
         *  says so, having been written when the image's own
         *  includesSelector: was finding three selectors in five.  This is
         *  now the same algorithm the image uses, which is the point:
         *  interpreter and image agree by construction rather than by both
         *  happening to find the same answer.
         */
        if (capacity) {
            uint32_t    start = (uint32_t)
                            (OM_identity_hash(selector) % capacity);
            uint32_t    probe;

            for (probe = 0; probe < capacity; ++probe) {
                st_oop  key;

                slot = (start + probe) % capacity;
                key  = OM_method_dict_key(dict, slot);
                if (key == selector) {
                    if (found_class)
                        *found_class = cls;
                    return OM_method_dict_value(dict, slot);
                }
                if (key == ST_NIL)
                    break;      /*  a nil ends the probe: not in here  */
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
    st_oop      cls = OM_fetch_class(receiver);
    unsigned    hops = 0;

    /*  Bounded as lookup_method is: this runs to REPORT a cycle, too.  */
    while (OM_is_present(cls) && ++hops <= MAX_SUPERCLASS_CHAIN) {
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
 *  chain distinguishes the two immediately.  BACKTRACE_LIMIT is defined
 *  beside ST_interp_dump_workers, which prints the same chain for a parked
 *  process.
 */

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

/*
 *  Tell the image it has run out of room, and answer whether that was
 *  possible.
 *
 *  Three things have to be true, and each of them is a reason to stop rather
 *  than send if it is not: an emergency reserve that has not already been
 *  spent, a selector the bootstrap bound, and a method to find under it.
 *  The last matters for the Blue Book profile, which loads sources/ alone:
 *  1983's Object has no outOfMemory, so that build stops exactly as it did
 *  before rather than sending into a doesNotUnderstand with no room to
 *  build the Message.
 *
 *  The arguments of the failed send come off first, leaving its receiver on
 *  top, which is what a zero-argument send wants.
 */
/*
 *  How deep a stack an out-of-room error can be raised on.
 *
 *  It was 16,384, and the number was measured: an Error raised 10,000
 *  frames down and caught at the top was instant, and the same thing
 *  100,000 frames down had not finished after five minutes.  Raising an
 *  error nothing can deliver turns a process that dies in two minutes with
 *  a message into one that hangs, and a hang is the failure Bugs1.md calls
 *  the most expensive to diagnose in the field -- so anything deeper than
 *  that was left to stop the way it always had.
 *
 *  The measurement was right and the diagnosis was wrong, which is why the
 *  number has moved rather than been defended.  Exception delivery is not
 *  quadratic: context_is_live in prim.c gave up walking after exactly
 *  100,000 hops and answered `that context has already returned', so the
 *  handler's return signalled a fresh error from the same depth, which did
 *  the same thing, for ever.  A loop with no output at all, and the cliff
 *  was between 99,000 frames and 100,000 rather than anywhere a cost curve
 *  would put one.  With that guard gone, an Error a MILLION frames down is
 *  caught in 350 milliseconds and the cost is linear.
 *
 *  So the cap is now high enough that no stack this VM will allow -- see
 *  ST_MAX_CALL_DEPTH below, which stops one long before here -- is refused
 *  an out-of-room error it could have caught.  It stays as a backstop
 *  rather than being deleted, because raising on a stack nothing can walk
 *  would still be a hang, and a hang is worse than a message.
 */
#define ST_OOM_MAX_UNWIND_DEPTH  (16 * 1024 * 1024)

/*
 *  How deep a stack may get before the image is told it has a runaway.
 *
 *  A method that does not stop calling itself reached about five million
 *  frames here: 4.4 GB resident after five seconds, 12.3 GB after fifteen,
 *  and then `out of memory activating a method' and exit -- taking every
 *  other worker, every open connection and every request in flight with it.
 *  Nothing was signalled on the way, so nothing could be caught.
 *
 *  Two hundred thousand frames is about fifty megabytes of contexts and is
 *  reached in a tenth of a second, which is the point: the error arrives
 *  while the machine is still healthy.  It is far above anything a program
 *  that means to recurse will use -- a balanced tree of a billion nodes is
 *  thirty frames deep -- and far below where the table runs out.
 *
 *  ST_MAX_CALL_DEPTH in the environment moves it; 0 turns it off, which
 *  restores the old behaviour exactly for anyone who wants it.
 */
#define ST_DEFAULT_MAX_CALL_DEPTH   200000

/*
 *  Read once, from any worker, and atomic because `any worker' means every
 *  worker may be the one that reads it.  -1 is `not yet'; the value each of
 *  them would compute is the same, so a race is harmless in fact and is
 *  still a race, which ThreadSanitizer is right to say so about.
 */
static st_atomic_int    st_max_call_depth = -1;

static int
max_call_depth(void)
{
    int known = ST_load_relaxed(&st_max_call_depth);

    if (known < 0) {
        const char *text = getenv("ST_MAX_CALL_DEPTH");

        known = ST_DEFAULT_MAX_CALL_DEPTH;
        if (text && *text) {
            long    wanted = strtol(text, NULL, 10);

            if (wanted >= 0 && wanted <= INT_MAX)
                known = (int) wanted;
        }
        ST_store_relaxed(&st_max_call_depth, known);
    }
    return known;
}

/*
 *  The true depth of the running stack, by walking it.
 *
 *  st_vm.call_depth is a counter, and a counter cannot see a handler that
 *  returns past a million frames in one jump.  Every non-local move calls
 *  this to put the counter right; see the field's comment in interp.h.
 */
int
ST_stack_depth(void)
{
    st_oop  scan = st_vm.active_context;
    int     depth = 0;

    while (OM_is_present(scan)) {
        ++depth;
        scan = OM_fetch_pointer(ST_CTX_SENDER, scan);
    }
    return depth;
}

/*
 *  Tell the image its stack is too deep, and answer whether that was
 *  possible.
 *
 *  Same shape as send_out_of_memory below, and for the same reasons: a
 *  selector the bootstrap bound, a method to find under it, and the
 *  arguments of the failed send taken off so the receiver is on top.  The
 *  send REPLACES the one that was about to be made, so what it answers is
 *  what that send answers -- which for an unhandled Error is nil, and for a
 *  handled one is whatever the handler decided.
 */
static int
send_depth_exceeded(uint32_t argc)
{
    st_oop  selector = st_om_vm_state[ST_VM_SELECTOR_DEPTH_EXCEEDED];
    st_oop  receiver;
    st_oop  found = ST_NIL;

    if (!OM_is_present(selector))
        return 0;
    receiver = ST_stack_value(argc);
    if (!OM_is_present(lookup_method(selector, OM_fetch_class(receiver),
                                     &found)))
        return 0;
    ST_pop_n(argc);
    st_vm.argument_count  = 0;
    st_vm.depth_signalled = 1;
    ST_send_selector(selector, 0);
    return 1;
}

/*
 *  Whether this activation is the one that goes too far.
 *
 *  Costs one comparison on the ordinary path.  The flag is what keeps the
 *  error deliverable: raising it, finding a handler and running one are all
 *  sends from a stack that is by definition over the ceiling, and every
 *  non-local move resets both the counter and the flag, so the ceiling
 *  re-arms as soon as the stack is genuinely shallow again.
 */
static int
depth_ceiling_reached(void)
{
    int limit = max_call_depth();

    return limit > 0 && st_vm.call_depth > limit && !st_vm.depth_signalled;
}

static int
send_out_of_memory(void)
{
#ifdef ST_OM_MT
    st_oop  selector = st_om_vm_state[ST_VM_SELECTOR_OUT_OF_MEMORY];
    st_oop  receiver;
    st_oop  found = ST_NIL;

    if (!OM_is_present(selector))
        return 0;
    if (st_vm.call_depth > ST_OOM_MAX_UNWIND_DEPTH)
        return 0;
    receiver = ST_stack_value(st_vm.argument_count);
    if (!OM_is_present(lookup_method(selector, OM_fetch_class(receiver),
                                     &found)))
        return 0;
    if (!OM_release_table_reserve())
        return 0;
    ST_pop_n(st_vm.argument_count);
    st_vm.argument_count = 0;
    ST_send_selector(selector, 0);
    return 1;
#else
    /*  Chapter 27's table is a fixed 16-bit one and has no reserve to give. */
    return 0;
#endif
}

static void
activate_new_method(void)
{
    st_oop      header = method_header(st_vm.new_method);
    uint32_t    slots  = ST_context_slots_for(header);
    st_oop      ctx;
    uint32_t    temps  = method_temporary_count(st_vm.new_method);
    uint32_t    i;

    /*
     *  Too deep before out of room: a runaway recursion is a bug in a
     *  program and not a program that needs more room, and stopping it here
     *  costs a tenth of a second where letting it run to the table's end
     *  cost twenty seconds, twelve gigabytes and the whole process.  See
     *  ST_MAX_CALL_DEPTH.
     */
    if (depth_ceiling_reached() && send_depth_exceeded(st_vm.argument_count))
        return;

    ctx = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, slots);
    if (!OM_is_object(ctx)) {
        /*
         *  Out of room, which used to be the end of the image.
         *
         *  The object table stopped at a fixed four million entries and this
         *  was where a program died -- with, in the case that found it, 282
         *  million words of heap still free.  It grows now, up to
         *  st_om_table_max, so reaching here means that ceiling or a real
         *  refusal from the allocator.
         *
         *  Where the stack is shallow enough to unwind, the image is TOLD
         *  before it is stopped.  Signalling costs objects at the moment
         *  there are none, so the ceiling is lifted by an emergency reserve
         *  first and #outOfMemory is then sent to the receiver of the send
         *  that could not be made.  A handler unwinds and the program
         *  carries on; an unhandled Error reports, resumes, allocates again,
         *  and arrives here with the reserve already spent, which is where
         *  the process stops.  See send_out_of_memory for what `shallow
         *  enough' means and why there is a limit at all.
         *
         *  The send replaces the one that failed, so its answer is what that
         *  send answers.  Nothing else could be true: the method is exactly
         *  the one there was no room to enter.
         */
        if (send_out_of_memory())
            return;
#ifdef ST_OM_MT
        fprintf(stderr, "st80: out of memory activating a method: "
                        "%u words and %u object table entries free "
                        "(the table holds %u of at most %u; ST_MAX_OBJECTS "
                        "raises the ceiling)\n",
                OM_core_left(), OM_oops_left(),
                st_om_table_size, st_om_table_max);
#else
        /*  The Blue Book memory's table is Chapter 27's and does not grow. */
        fprintf(stderr, "st80: out of memory activating a method: "
                        "%u words and %u object table entries free\n",
                OM_core_left(), OM_oops_left());
#endif
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

    /*  See activate_new_method: a block can run away just as a method can. */
    if (depth_ceiling_reached() && send_depth_exceeded(argc))
        return 1;

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
/*
 *  Build a closure's activation context WITHOUT entering it.
 *
 *  Forking needs this and nothing else does.  Process>>forContext: wants a
 *  context it can resume, and in 1983 that was easy because a BlockContext
 *  IS one -- `[...] newProcess' handed the block straight over.  A
 *  BlockClosure is not a context: it has three fields where a context has
 *  six, so passing one where 1983 passed a block reads off the end of the
 *  object.  ASAN caught it inside fetch_context_registers the first time
 *  anything forked.
 *
 *  So the closure is activated here the way ST_activate_closure activates
 *  it, with two differences: the sender is nil, because this frame is the
 *  bottom of a new process rather than a call from the current one, and
 *  the result is answered rather than entered.  Zero arguments, because a
 *  process starts a block that takes none.
 */
st_oop
ST_closure_as_context(st_oop closure)
{
    st_oop      outer;
    st_oop      method;
    st_oop      receiver;
    st_oop      ctx;
    uint32_t    copied;
    uint32_t    slots;
    uint32_t    i;

    if (!OM_is_object(closure))
        return ST_OOP_INVALID;
    outer = OM_fetch_pointer(ST_CLOSURE_OUTER_CONTEXT, closure);
    if (!OM_is_present(outer))
        return ST_OOP_INVALID;
    method   = OM_fetch_pointer(ST_CTX_METHOD, outer);
    receiver = OM_fetch_pointer(ST_CTX_RECEIVER, outer);
    if (!OM_is_object(method))
        return ST_OOP_INVALID;

    copied = OM_fetch_word_length(closure);
    if (copied < ST_CLOSURE_FIRST_COPIED)
        return ST_OOP_INVALID;
    copied -= ST_CLOSURE_FIRST_COPIED;

    slots = ST_context_slots_for(method_header(method));
    if (copied + ST_CTX_TEMP_FRAME_START > slots)
        return ST_OOP_INVALID;

    ctx = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, slots);
    if (!OM_is_object(ctx))
        return ST_OOP_INVALID;

    for (i = 0; i < copied; ++i)
        OM_store_pointer(ST_CTX_TEMP_FRAME_START + i, ctx,
                         OM_fetch_pointer(ST_CLOSURE_FIRST_COPIED + i,
                                          closure));

    OM_store_pointer(ST_CTX_SENDER,   ctx, ST_NIL);
    OM_store_pointer(ST_CTX_IP,       ctx,
                     OM_fetch_pointer(ST_CLOSURE_STARTPC, closure));
    OM_store_pointer(ST_CTX_SP,       ctx, OM_int_oop((st_int) copied));
    OM_store_pointer(ST_CTX_METHOD,   ctx, method);
    OM_store_pointer(ST_CTX_CLOSURE,  ctx, closure);
    OM_store_pointer(ST_CTX_RECEIVER, ctx, receiver);
    return ctx;
}

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

    slots = ST_context_slots_for(method_header(method));
    if (argc + copied + ST_CTX_TEMP_FRAME_START > slots)
        return 0;                   /*  the frame cannot hold them  */

    /*
     *  See activate_new_method.  This is the path a runaway BLOCK takes --
     *  `b := [:x | b value: x + 1]. b value: 0' never activates a method at
     *  all, because value: is a primitive -- so guarding methods alone
     *  would have left the shortest runaway in Smalltalk unguarded.
     */
    if (depth_ceiling_reached() && send_depth_exceeded(argc))
        return 1;

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
    /*
     *  One jump, however many frames it skipped: do_return counted the one
     *  it was handed.  Recount, or the counter drifts upward for ever after
     *  the first caught exception and the depth ceiling fires on a stack
     *  three frames deep.
     */
    st_vm.call_depth      = ST_stack_depth();
    st_vm.depth_signalled = 0;
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
    st_vm.call_depth      = ST_stack_depth();   /*  see ST_return_to  */
    st_vm.depth_signalled = 0;
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
find_unwind_between(st_oop from, st_oop home, int *home_found, int *frames)
{
    st_oop  ctx = OM_fetch_pointer(ST_CTX_SENDER, from);

    *home_found = 0;
    *frames     = 1;                    /*  `from' itself  */
    while (OM_is_present(ctx)) {
        ++*frames;
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
 *  How many contexts a non-local return from `from' discards: `from'
 *  itself, every caller between it and `home', and `home'.  -1 when home
 *  is not on the chain at all.  The Blue Book block's chain is its caller
 *  links, which share the field with a method context's sender.
 */
static int
frames_through(st_oop from, st_oop home)
{
    st_oop  ctx = from;
    int     n = 1;

    while (ctx != home) {
        ctx = OM_fetch_pointer(ST_CTX_CALLER, ctx);
        if (!OM_is_present(ctx))
            return -1;
        ++n;
    }
    return n;
}

/*
 *  Put the depth counter right after a non-local return (Bugs3 B15).
 *
 *  do_return takes one off the counter, which is right for the ordinary
 *  return it was written for and wrong for a `^' inside a block, which
 *  discards the block's context, every frame between it and its home, and
 *  the home itself in one jump.  The leak was one frame per non-local
 *  return, and the library returns out of blocks constantly -- includes:,
 *  detect:, at:ifAbsent:, inheritsFrom: and so isKindOf: and so every
 *  mixed-type arithmetic -- so `1 to: 200000 do: [:i | 1 + 1.0]' raised
 *  RecursionDepthExceeded on a stack three frames deep, and the count
 *  carried across evaluations in one Process because nothing ever reset
 *  it.  ST_return_to and ST_resume_at recount by walking the whole stack;
 *  that is right for them and would make deep recursion with a non-local
 *  return at every level quadratic here, so the frames are counted during
 *  the walk the return already makes and subtracted, and the whole-stack
 *  walk is kept only for the case where the home was not found on the
 *  chain.
 *
 *  The ceiling's flag is re-armed only when the stack is genuinely under
 *  the ceiling again.  Re-arming it unconditionally, as the jumps above
 *  do, would let a non-local return inside the reporting of a depth error
 *  -- which runs, by definition, from over the ceiling -- signal a second
 *  depth error from inside the first, and so on until the table was gone.
 */
static void
settle_depth_after_unwind(int discarded)
{
    if (discarded < 0)
        st_vm.call_depth = ST_stack_depth();
    else {
        st_vm.call_depth -= discarded - 1;      /*  do_return took one  */
        if (st_vm.call_depth < 0)
            st_vm.call_depth = 0;
    }
    if (st_vm.depth_signalled) {
        int limit = max_call_depth();

        if (limit <= 0 || st_vm.call_depth <= limit)
            st_vm.depth_signalled = 0;
    }
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

/*
 *  A conditional jump whose value is neither true nor false.
 *
 *  This used to stop the VM: ST_must_be_boolean printed a line, dumped a
 *  backtrace and set st_vm.running = 0.  There was no send, so there was
 *  nothing for `on: Error do:' to catch -- the process was simply gone,
 *  and with it every other worker's work, on a system whose whole claim is
 *  that it is a server on every core.  It was reachable from the library:
 *  `FileDirectory new printString' halted the image, because printOn:
 *  tests an instance variable that new never set, which is the single most
 *  common consequence of an uninitialised variable and the reason every
 *  other Smalltalk makes this an ordinary error.
 *
 *  So the Blue Book's own answer: put the value back and send it
 *  #mustBeBoolean, which Object implements and lib/Kernel-Exceptions
 *  overrides to signal a catchable NonBooleanReceiver.
 *
 *  The instruction pointer is REWOUND to the jump first, so what
 *  mustBeBoolean answers is retested by the same bytecode rather than
 *  landing on the stack beside a branch that was silently not taken.  That
 *  is what makes the 1983 comment -- "proceed for truth" -- true here: the
 *  method answers true, the jump runs again and takes the true arm, and
 *  the stack is the depth the compiler computed.  It also means a handler
 *  can answer false and pick the other arm.  Retesting could loop if
 *  mustBeBoolean answered a non-Boolean, which is why the override in
 *  lib/ answers one of exactly two objects whatever a handler does.
 *
 *  With no mustBeBoolean anywhere in the receiver's chain -- during the
 *  bootstrap, before Object has any methods -- there is nobody to tell,
 *  and stopping is still what to do.
 */
static void
send_must_be_boolean(st_oop value, uint32_t ip_at_start)
{
    st_oop  found = ST_NIL;

    if (!OM_is_present(lookup_method(ST_SELECTOR_MUST_BE_BOOLEAN,
                                     OM_fetch_class(value), &found))) {
        ST_must_be_boolean(value);
        return;
    }
    st_vm.instruction_pointer = ip_at_start;
    ST_push(value);
    ST_send_selector(ST_SELECTOR_MUST_BE_BOOLEAN, 0);
}

/*
 *  Find `selector' up the receiver's chain, or failing that up nil's.
 *
 *  For the selectors the VM sends on its own account -- the errors it
 *  raises in place of something it could not do.  A receiver whose class
 *  chain does not reach Object, which `Behavior new new' is, has none of
 *  them; nil's class does reach Object, and Object's implementations of
 *  these send nothing to the receiver that it might not understand.  The
 *  send is still to the original receiver, which is what a handler wants
 *  to see.
 */
static st_oop
lookup_for_vm(st_oop selector, st_oop receiver)
{
    st_oop  found = ST_NIL;
    st_oop  method = lookup_method(selector, OM_fetch_class(receiver), &found);

    if (!OM_is_present(method))
        method = lookup_method(selector, OM_fetch_class(ST_NIL), &found);
    return method;
}

/*
 *  End the process this worker is running, and only that process.
 *
 *  Where the interpreter has nothing left to send and nobody to tell:
 *  a receiver nothing in the image can deliver a doesNotUnderstand: to,
 *  today; a compiler-emitted send that is not a send, before.  Those
 *  cleared st_vm.running, which is the right end for a single-process run
 *  and the wrong one for a pool (Bugs3 B5, B7): a worker whose run ends
 *  takes the pool with it, because `every worker idle' can never again be
 *  true for the rest.
 *
 *  So this does what `Processor terminateActive' does, from C: the
 *  faulted send answers nil, the process is parked as if it had
 *  suspended itself, and the worker looks for something else to run.
 *  Under -serve that is the next request; with one process and no pool
 *  the scheduler finds nothing, says so, and the run ends as it would
 *  have -- with the report first, and the exit code of an unhandled
 *  error rather than of an answer.
 */
static void
abandon_active_process(const char *why)
{
    if (errors_reported)
        fprintf(stderr, "st80: %s; the process is ended\n", why);
    ST_report_backtrace();
    ST_pop_n(st_vm.argument_count + 1);
    ST_push(ST_NIL);
    st_vm.argument_count = 0;
    ++st_vm.unhandled_errors;
    SCHED_suspend_active();
}

/*
 *  Abandon the running activation: its method cannot be executed (Bugs3
 *  B11).
 *
 *  The reasons are literal_at's, next_byte's and fetch_context_registers':
 *  a literal index past the frame, an instruction pointer past the end, a
 *  stack pointer outside the context, a method that is not one.  All of
 *  them used to be trusted and all of them segfaulted; none of them is a
 *  fault a handler could have seen coming, and none can be continued from,
 *  because the bytes that come next are the bytes that were wrong.
 *
 *  So the context is returned from, with nil, and the nil is then
 *  replaced by what #corruptMethod answers when sent to the receiver --
 *  the shape of send_depth_exceeded and send_out_of_memory: the error is
 *  raised in the image, from the sender's frame, where `on: Error do:'
 *  around the call can catch it, and unhandled it reports and answers nil
 *  to the sender, which carries on.  The line on stderr is kept
 *  regardless, because a method whose bytes have been rewritten is worth
 *  a line even when the image handles the error: nothing else says which
 *  method it was.
 *
 *  A profile that binds no #corruptMethod -- the Blue Book one -- gets
 *  the return with nil and the line, which is still not a segfault.
 */
static void
corrupt_method(const char *reason)
{
    st_oop      ctx = st_vm.active_context;
    st_oop      receiver = st_vm.receiver;
    st_oop      selector = st_om_vm_state[ST_VM_SELECTOR_CORRUPT_METHOD];
    st_oop      caller;
    char        name[200];

    st_vm.corrupt_reason = NULL;
    if (!name_method(receiver, st_vm.method, name, sizeof name))
        snprintf(name, sizeof name, "?");
    if (errors_reported)
        fprintf(stderr, "st80: cannot run %s: %s; the activation is "
                        "abandoned\n", name, reason);
    if (!OM_is_object(ctx)) {
        st_vm.running = 0;
        return;
    }
    caller = OM_fetch_pointer(ST_CTX_CALLER, ctx);
    /*
     *  Held across the return.  The abandoned context may be the only
     *  thing that refers to its receiver -- `Bugs3CM new foo' makes an
     *  instance whose one reference is the frame's receiver slot -- and
     *  do_return frees the frame.  Without this the send below went to
     *  whatever the object table next handed out under that number, which
     *  in the first run was an Array.
     */
    OM_increase_ref(receiver);
    do_return(ST_NIL, caller, 0);
    /*
     *  Not at the bottom of the world, and only where the image has a
     *  #corruptMethod to send; otherwise nil stands as the answer.
     */
    if (st_vm.running && OM_is_present(selector)
     && OM_is_present(lookup_for_vm(selector, receiver))) {
        ST_pop_n(1);
        ST_push(receiver);
        ST_send_selector(selector, 0);
    }
    OM_decrease_ref(receiver);
}

int
ST_argument_count_matches(st_oop receiver, st_oop selector, uint32_t argc)
{
    st_oop  found = ST_NIL;
    st_oop  method = lookup_method(selector, OM_fetch_class(receiver), &found);

    if (!OM_is_present(method))
        return 1;                       /*  doesNotUnderstand: takes any  */
    return method_argument_count(method) == argc;
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
 *  The BlockContext arm below is the three lines this function used to be,
 *  with the depth accounting of Bugs3 B15 around them, and is meant to
 *  stay that way: it is on the trace2 path, which runs blockCopy: twice,
 *  and the value of that oracle depends on this code being untouched
 *  rather than carefully preserved.  The accounting is a walk of the
 *  caller chain and an adjustment to a counter -- no send, no bytecode, no
 *  object touched -- so the trace it produces is the trace it produced.
 *  Everything closures need is in the other arm, and do_return itself is
 *  not modified at all -- its nil-sender stop is what -eval depends on to
 *  get an answer back.
 */
static void
return_value(st_oop result)
{
    st_oop  ctx = st_vm.active_context;
    st_oop  home;
    st_oop  sender;

    if (OM_fetch_class(ctx) == ST_CLASS_BLOCK_CONTEXT) {
        int     discarded;

        home   = st_vm.home_context;
        sender = OM_fetch_pointer(ST_CTX_SENDER, home);
        discarded = frames_through(ctx, home);
        do_return(result, sender, 0);
        settle_depth_after_unwind(discarded);
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
        int     discarded = 0;
        st_oop  unwind = find_unwind_between(ctx, home, &found, &discarded);

        if (OM_is_present(unwind)) {
            send_about_to_return(ctx, result, unwind);
            return;
        }
        if (!found) {
            /*  The home method returned before this block ran.  */
            send_cannot_return(ctx, result);
            return;
        }
        sender = OM_fetch_pointer(ST_CTX_SENDER, home);
        do_return(result, sender, 0);
        settle_depth_after_unwind(discarded);
    }
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

    /*
     *  Nobody up the receiver's chain implements doesNotUnderstand: (Bugs3
     *  B7).  `Behavior new new' is such a receiver -- its class has no
     *  superclass and no methods -- and so is everything, once somebody
     *  has sent `Object removeSelector: #doesNotUnderstand:'.  This used
     *  to print a line and clear st_vm.running, which under -serve ends
     *  the worker and with it the pool: one reflective expression in one
     *  request took every other request down.
     *
     *  Three fallbacks, each an ordinary send the image can catch, before
     *  anything drastic:
     *
     *  cannotInterpret: first, looked up the receiver's chain.  The Blue
     *  Book reserves the selector for a class whose method dictionary is
     *  nil, and Squeak sends it with the Message; here it is what a class
     *  chain with no doesNotUnderstand: gets, which is the same idea one
     *  step further out, and it lets an image say what such a receiver
     *  should do.  lib/Kernel-Exceptions gives Object one that raises the
     *  MessageNotUnderstood doesNotUnderstand: would have.
     *
     *  Then doesNotUnderstand: again, from the class of nil: every image
     *  has nil, its class has Object above it, and Object's handler builds
     *  a MessageNotUnderstood from the Message and the receiver without
     *  sending the receiver anything it might not understand -- its
     *  description guards `receiver class name' with a handler for exactly
     *  this case.  A receiver whose chain does not reach Object thereby
     *  gets the error every other object gets.
     *
     *  Only when the image has no handler at all -- during the bootstrap,
     *  before Object has methods, or after both have been removed -- is
     *  the process ended, and then it is THIS process, not the pool.
     */
    if (!OM_is_present(method)) {
        st_oop  cannot = lookup_method(ST_SELECTOR_CANNOT_INTERPRET,
                                       lookup_class, &found);

        if (OM_is_present(cannot)
         && method_argument_count(cannot) == 1) {
            st_vm.message_selector = ST_SELECTOR_CANNOT_INTERPRET;
            method = cannot;
        }
    }
    if (!OM_is_present(method))
        method = lookup_method(ST_SELECTOR_DOES_NOT_UNDERSTAND,
                               OM_fetch_class(ST_NIL), &found);
    if (!OM_is_present(method)) {
        char        buf[256];
        char        name[256];
        char        why[600];
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

        snprintf(why, sizeof why, "%s does not understand #%s, and nothing "
                                  "in the image understands doesNotUnderstand: "
                                  "or cannotInterpret: either",
                 buf, n ? name : "(not a symbol)");
        if (getenv("ST_LOOKUP_LOG")) {
            st_oop      cls = OM_fetch_class(receiver);
            char        cname[64];
            unsigned    hops = 0;

            while (OM_is_present(cls) && ++hops <= MAX_SUPERCLASS_CHAIN) {
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
        abandon_active_process(why);
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


/*
 *  Run `method' on the receiver and st_vm.argument_count arguments that
 *  are on the stack, exactly as a send does once lookup has found it: the
 *  quick returns, the argument-count check, the primitive, the activation.
 *  Shared by the send path and by primitive 188, withArgs:executeMethod:,
 *  which runs a CompiledMethod that is installed nowhere.
 */
static void
run_method_found(st_oop receiver, st_oop method)
{
    st_oop      header;
    unsigned    flag;

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
        /*
         *  Name the selector and show the stack.  The count alone says a
         *  frame would have been misaligned and nothing about where -- and
         *  the answer to "where" is the only thing anybody wants next.  I
         *  have hit this message twice while porting Pharo protocol, both
         *  times spent a run adding a print to find out which method it
         *  meant, and both times the answer was one send.
         *
         *  Reported, and then delivered as a doesNotUnderstand: rather
         *  than by clearing st_vm.running (Bugs3 B5).  `3 perform: #+'
         *  arrived here with no arguments for a method that takes one, and
         *  the halt ended the worker, and one worker leaving ends the pool
         *  -- a heartbeat process forked beside it stopped too.  The
         *  perform primitives now fail on the mismatch before the stack is
         *  shuffled, as the Blue Book's primitivePerform does, so what
         *  still reaches here is a compiler that emitted a send with the
         *  wrong count.  That is a bug worth a line on stderr, and it is
         *  still one process's bug: the receiver did not understand the
         *  message as it was sent, which is what doesNotUnderstand: means,
         *  and the Message it is handed carries the arguments as they
         *  came.  The error is catchable, reported by the image, and
         *  resumable with nil; the pool never hears of it.
         */
        char    selector[128];

        OM_string_of(st_vm.message_selector, selector, sizeof selector);
        fprintf(stderr, "st80: %u arguments sent to #%s, which expects %u, "
                        "at cycle %llu\n",
                st_vm.argument_count, selector,
                method_argument_count(method),
                (unsigned long long) st_vm.cycle);
        ST_report_backtrace();
        send_does_not_understand(receiver, st_vm.message_selector,
                                 OM_fetch_class(receiver));
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

void
ST_execute_method(st_oop method, uint32_t argc)
{
    st_vm.argument_count = argc;
    run_method_found(ST_stack_value(argc), method);
}

unsigned
ST_method_argument_count(st_oop method)
{
    return method_argument_count(method);
}

static void
execute_new_method(st_oop receiver, st_oop selector, st_oop lookup_class,
                   int traced)
{
    st_oop      found;
    st_oop      method;
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
    run_method_found(receiver, method);
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
    /*
     *  Refuse a context that does not point at a bytecode.
     *
     *  A method's bytes begin with its header and literal frame, so a
     *  resumable instruction pointer is at least (literals + 1) words in.
     *  An image whose active process was saved without its registers being
     *  written back points somewhere inside that frame instead, and the
     *  interpreter then executes literals: Blake's snapshot resumed at
     *  IP 2 of a method whose bytecodes start at byte 40, reached the low
     *  byte of literal 0 -- 0xF6, "send literal 6 with two arguments", in a
     *  method with four literals -- and died in lookup_method on a selector
     *  read from past the end of the frame.
     *
     *  The writer no longer produces such an image.  This is here because a
     *  segfault is the worst possible way to be told, and every image
     *  written before the fix is still on somebody's disk.
     */
    {
        st_oop      ip_field = OM_fetch_pointer(ST_CTX_IP, context);
        st_oop      method   = OM_fetch_pointer(ST_CTX_METHOD, context);
        uint32_t    first;

        if (!OM_is_int(ip_field) || !OM_is_present(method)) {
            if (errbuf)
                snprintf(errbuf, errlen,
                         "the saved process resumes at no bytecode: its "
                         "context has no instruction pointer or no method");
            return -1;
        }
        first = method_initial_ip(method);
        if ((uint32_t) OM_int_value(ip_field) <= first) {
            if (errbuf)
                snprintf(errbuf, errlen,
                         "the saved process resumes inside its method's "
                         "literal frame, at %ld where the bytecodes start "
                         "at %u -- the image was written by a snapshot that "
                         "did not store the interpreter's registers",
                         (long) OM_int_value(ip_field), first);
            return -1;
        }
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
#ifdef ST_OM_MT
        /*
         *  The other half of the poll: tell the object memory this thread
         *  has reached a bytecode boundary, so that what it retired a
         *  couple of epochs ago can be reclaimed without stopping anyone.
         */
        if ((st_vm.cycle & 1023u) == 0)
            OM_epoch_step();
#endif
        SCHED_check_process_switch();
        if (!st_vm.running)
            break;
        /*
         *  Nothing is fetched from past the end of the method (Bugs3
         *  B11).  One comparison against a register loaded with the
         *  method; it is also where a fault that fetch_context_registers
         *  or an operand fetch could only write down is delivered, since
         *  both leave the pointer here.  A method that simply runs off its
         *  end without a return -- which a compiler never writes and a
         *  rewritten one may -- arrives here too.
         */
        if (st_vm.instruction_pointer >= st_vm.method_end) {
            corrupt_method(st_vm.corrupt_reason
                           ? st_vm.corrupt_reason
                           : "an instruction pointer past the end of the "
                             "method");
            continue;
        }
        ip_at_start = st_vm.instruction_pointer;
        code = next_byte();
        ++st_vm.cycle;
        ++executed;
        ST_trace_bytecode(code, st_vm.method, ip_at_start);

        switch (code) {
        /*  0-15: push receiver instance variable  */
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
            if (receiver_variable_ok(code))
                ST_push(OM_fetch_pointer(code, st_vm.receiver));
            break;

        /*  16-31: push temporary  */
        case 16: case 17: case 18: case 19: case 20: case 21: case 22:
        case 23: case 24: case 25: case 26: case 27: case 28: case 29:
        case 30: case 31:
            if (temporary_ok(code - 16))
                ST_push(OM_fetch_pointer(ST_CTX_TEMP_FRAME_START + (code - 16),
                                         st_vm.home_context));
            break;

        /*  32-63: push literal constant  */
        case 32: case 33: case 34: case 35: case 36: case 37: case 38:
        case 39: case 40: case 41: case 42: case 43: case 44: case 45:
        case 46: case 47: case 48: case 49: case 50: case 51: case 52:
        case 53: case 54: case 55: case 56: case 57: case 58: case 59:
        case 60: case 61: case 62: case 63: {
            st_oop  lit = literal_at(code - 32);

            if (lit != ST_OOP_INVALID)
                ST_push(lit);
            break;
        }

        /*  64-95: push the value of a literal variable (an Association)  */
        case 64: case 65: case 66: case 67: case 68: case 69: case 70:
        case 71: case 72: case 73: case 74: case 75: case 76: case 77:
        case 78: case 79: case 80: case 81: case 82: case 83: case 84:
        case 85: case 86: case 87: case 88: case 89: case 90: case 91:
        case 92: case 93: case 94: case 95: {
            st_oop  lit = literal_at(code - 64);

            if (lit != ST_OOP_INVALID)
                ST_push(OM_fetch_pointer(ST_ASSOCIATION_VALUE, lit));
            break;
        }

        /*  96-103: pop and store receiver instance variable  */
        case 96: case 97: case 98: case 99:
        case 100: case 101: case 102: case 103: {
            st_oop  value = ST_pop();

            if (receiver_variable_ok(code - 96))
                OM_store_pointer(code - 96, st_vm.receiver, value);
            break;
        }

        /*  104-111: pop and store temporary  */
        case 104: case 105: case 106: case 107:
        case 108: case 109: case 110: case 111: {
            st_oop  value = ST_pop();

            if (temporary_ok(code - 104))
                OM_store_pointer(ST_CTX_TEMP_FRAME_START + (code - 104),
                                 st_vm.home_context, value);
            break;
        }

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

            if (kind == 0) {
                if (receiver_variable_ok(index))
                    ST_push(OM_fetch_pointer(index, st_vm.receiver));
            } else if (kind == 1) {
                if (temporary_ok(index))
                    ST_push(OM_fetch_pointer(ST_CTX_TEMP_FRAME_START + index,
                                             st_vm.home_context));
            } else {
                st_oop  lit = literal_at(index);

                if (lit == ST_OOP_INVALID)
                    break;
                if (kind == 2)
                    ST_push(lit);
                else
                    ST_push(OM_fetch_pointer(ST_ASSOCIATION_VALUE, lit));
            }
            break;
        }
        case 129:           /*  extended store  */
        case 130: {         /*  extended pop and store  */
            uint8_t     desc  = next_byte();
            uint32_t    kind  = desc >> 6;
            uint32_t    index = desc & 63;
            st_oop      value = (code == 130) ? ST_pop() : ST_stack_top();

            if (kind == 0) {
                if (receiver_variable_ok(index))
                    OM_store_pointer(index, st_vm.receiver, value);
            } else if (kind == 1) {
                if (temporary_ok(index))
                    OM_store_pointer(ST_CTX_TEMP_FRAME_START + index,
                                     st_vm.home_context, value);
            } else if (kind == 3) {
                st_oop  lit = literal_at(index);

                if (lit != ST_OOP_INVALID)
                    OM_store_pointer(ST_ASSOCIATION_VALUE, lit, value);
            }
            break;
        }

        case 131: {         /*  single extended send  */
            uint8_t     desc = next_byte();
            st_oop      sel  = literal_at(desc & 31);

            if (sel != ST_OOP_INVALID)
                ST_send_selector(sel, (uint32_t) (desc >> 5));
            break;
        }
        case 132: {         /*  double extended send  */
            uint8_t     argc = next_byte();
            uint8_t     lit  = next_byte();
            st_oop      sel  = literal_at(lit);

            if (sel != ST_OOP_INVALID)
                ST_send_selector(sel, argc);
            break;
        }
        /*
         *  The super sends read the method's LAST literal for the class
         *  the method was compiled in.  A method with no literals has no
         *  last one, and the index wraps; literal_at refuses it.
         */
        case 133: {         /*  single extended send to super  */
            uint8_t     desc = next_byte();
            st_oop      sel  = literal_at(desc & 31);
            st_oop      cls;

            if (sel == ST_OOP_INVALID)
                break;
            cls = literal_at(st_vm.literal_limit - 1);
            if (cls == ST_OOP_INVALID)
                break;
            send_to_class(sel, (uint32_t) (desc >> 5),
                          OM_fetch_pointer(ST_CLASS_SUPERCLASS,
                                           OM_fetch_pointer(ST_ASSOCIATION_VALUE, cls)));
            break;
        }
        case 134: {         /*  double extended send to super  */
            uint8_t     argc = next_byte();
            uint8_t     lit  = next_byte();
            st_oop      sel  = literal_at(lit);
            st_oop      cls;

            if (sel == ST_OOP_INVALID)
                break;
            cls = literal_at(st_vm.literal_limit - 1);
            if (cls == ST_OOP_INVALID)
                break;
            send_to_class(sel, argc,
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
                send_must_be_boolean(value, ip_at_start);
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
                send_must_be_boolean(value, ip_at_start);
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
                send_must_be_boolean(value, ip_at_start);
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
                st_oop      sel  = literal_at(lit);

                if (sel != ST_OOP_INVALID)
                    ST_send_selector(sel, argc);
            }  else  {
                fprintf(stderr, "st80: unused bytecode %u at cycle %llu\n",
                        (unsigned) code, (unsigned long long) st_vm.cycle);
                st_vm.running = 0;
            }
            break;
        }
    }
    /*
     *  A nominee this worker was about to run, and now never will: back on
     *  its ready list for whoever is still running.  See
     *  SCHED_check_process_switch.
     */
    SCHED_release_nomination();
    return executed;
}



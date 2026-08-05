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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

st_interp   st_vm;

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
    /*  Bytecodes begin after the header word and the literal frame.  */
    return (ST_header_literal_count(method_header(method))
            + ST_METHOD_LITERAL_START) * 2;
}

static unsigned
method_primitive_index(st_oop method)
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
 *  Roots the object memory cannot see: the interpreter's registers and the
 *  objects the VM itself holds on to.  Everything else is reachable from the
 *  guaranteed pointers, which the collector walks on its own.
 */
static void
provide_roots(om_visit_fn visit)
{
    visit(st_vm.active_context);
    visit(st_vm.home_context);
    visit(st_vm.method);
    visit(st_vm.receiver);
    visit(st_vm.new_method);
    visit(st_vm.message_selector);
    visit(GFX_display_form());
    visit(SCHED_input_semaphore());
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

    while (OM_is_object(cls)) {
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

static void
do_return(st_oop result, st_oop to_context, int from_block)
{
    st_oop  sender = to_context;

    ST_trace_return(result, from_block);

    if (!OM_is_object(sender)) {
        /*  Returning from the bottom of the world stops the interpreter.  */
        st_vm.running = 0;
        return;
    }
    if (st_vm.call_depth > 0)
        --st_vm.call_depth;
    OM_increase_ref(result);
    set_active_context(sender);
    ST_push(result);
    OM_decrease_ref(result);
}

static void
return_value(st_oop result)
{
    st_oop  home = st_vm.home_context;
    st_oop  sender;

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
    if (!OM_is_object(method)) {
        char    buf[256];

        ST_print_object(receiver, buf, sizeof buf);
        fprintf(stderr, "st80: %s does not understand a selector, and does "
                        "not understand doesNotUnderstand: either\n", buf);
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

    if (!OM_is_object(method)) {
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
    st_vm.primitive_index = method_primitive_index(method);
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
    if (!OM_is_object(scheduler)) {
        if (errbuf)
            snprintf(errbuf, errlen, "scheduler association holds no scheduler");
        return -1;
    }
    /*  ProcessorScheduler: activeProcess is its second instance variable.  */
    process = OM_fetch_pointer(1, scheduler);
    if (!OM_is_object(process)) {
        if (errbuf)
            snprintf(errbuf, errlen, "no active process in the scheduler");
        return -1;
    }
    /*  Process: suspendedContext is its first instance variable.  */
    context = OM_fetch_pointer(1, process);
    if (!OM_is_object(context)) {
        if (errbuf)
            snprintf(errbuf, errlen, "active process has no suspended context");
        return -1;
    }
    OM_set_root_provider(provide_roots);
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

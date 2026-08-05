/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Primitive methods.  See prim.h for the success and failure contract.
 */

#include "interp.h"
#include "prim.h"
#include "gfx.h"
#include "st_sched.h"
#include "st_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  ----------  Argument helpers  ----------  */

static int
integer_arg(uint32_t from_top, st_int *out)
{
    st_oop  p = ST_stack_value(from_top);

    if (!OM_is_int(p))
        return 0;
    *out = OM_int_value(p);
    return 1;
}

/*
 *  Sizes and indices routinely exceed the 15-bit SmallInteger range -- a
 *  640x480 bitmap is 19200 words -- so the Blue Book carries values up to
 *  65535 as either a SmallInteger or a two-byte LargePositiveInteger.  Every
 *  primitive that takes a length or an index has to accept both.
 *
 *  Digits are stored least significant first.
 */
static int
positive_16bit_value(st_oop p, uint32_t *out)
{
    if (OM_is_int(p)) {
        st_int  v = OM_int_value(p);

        if (v < 0)
            return 0;
        *out = (uint32_t) v;
        return 1;
    }
    if (!OM_is_object(p))
        return 0;
    if (OM_fetch_class(p) != ST_CLASS_LARGE_POSITIVE_INTEGER)
        return 0;
    if (OM_fetch_byte_length(p) != 2)
        return 0;
    *out = (uint32_t) OM_fetch_byte(0, p)
         | ((uint32_t) OM_fetch_byte(1, p) << 8);
    return 1;
}

static st_oop
positive_16bit_integer_for(uint32_t value)
{
    st_oop  big;

    if (value <= (uint32_t) ST_INT_MAX)
        return OM_int_oop((st_int) value);
    if (value > 65535)
        return ST_OOP_INVALID;
    big = OM_instantiate_bytes(ST_CLASS_LARGE_POSITIVE_INTEGER, 2);
    if (!OM_is_object(big))
        return ST_OOP_INVALID;
    OM_store_byte(0, big, (uint8_t) (value & 0xFF));
    OM_store_byte(1, big, (uint8_t) (value >> 8));
    return big;
}

/*  Answer a value that may need to become a LargePositiveInteger.  */
static int
answer_positive_16bit(uint32_t value, uint32_t pop)
{
    st_oop  result = positive_16bit_integer_for(value);

    if (result == ST_OOP_INVALID)
        return 0;
    ST_pop_n(pop);
    ST_push(result);
    return 1;
}

/*  Answer a SmallInteger result, failing if the value will not fit.  */
static int
answer_integer(st_int value, uint32_t pop)
{
    if (!OM_int_fits(value))
        return 0;
    ST_pop_n(pop);
    ST_push(OM_int_oop(value));
    return 1;
}

static int
answer_boolean(int value, uint32_t pop)
{
    ST_pop_n(pop);
    ST_push(value ? ST_TRUE : ST_FALSE);
    return 1;
}

/*  ----------  Arithmetic, primitives 1 to 17  ----------  */

/*
 *  Every one of these fails rather than truncating when the result leaves
 *  SmallInteger range.  That failure is what sends control into the
 *  Smalltalk body, which promotes to LargePositiveInteger -- so an
 *  "overflow" here is ordinary arithmetic, not an error.
 */
static int
arithmetic_primitive(unsigned index)
{
    st_int  a;
    st_int  b;

    if (!integer_arg(1, &a) || !integer_arg(0, &b))
        return 0;

    switch (index) {
    case 1:  return answer_integer(a + b, 2);
    case 2:  return answer_integer(a - b, 2);
    case 3:  return answer_boolean(a <  b, 2);
    case 4:  return answer_boolean(a >  b, 2);
    case 5:  return answer_boolean(a <= b, 2);
    case 6:  return answer_boolean(a >= b, 2);
    case 7:  return answer_boolean(a == b, 2);
    case 8:  return answer_boolean(a != b, 2);
    case 9:  return answer_integer(a * b, 2);
    case 10:
        if (b == 0 || a % b != 0)
            return 0;               /*  inexact division is not a SmallInteger */
        return answer_integer(a / b, 2);
    case 11: {                      /*  \\  floored modulo  */
        st_int  r;

        if (b == 0)
            return 0;
        r = a % b;
        if (r != 0 && ((r < 0) != (b < 0)))
            r += b;
        return answer_integer(r, 2);
    }
    case 12: {                      /*  //  floored division  */
        st_int  q;

        if (b == 0)
            return 0;
        q = a / b;
        if ((a % b) != 0 && ((a < 0) != (b < 0)))
            --q;
        return answer_integer(q, 2);
    }
    case 13:                        /*  quo:  truncated division  */
        if (b == 0)
            return 0;
        return answer_integer(a / b, 2);
    case 14: return answer_integer(a & b, 2);
    case 15: return answer_integer(a | b, 2);
    case 16: return answer_integer(a ^ b, 2);
    case 17:                        /*  bitShift:  */
        if (b >= 0) {
            if (b >= 31)
                return 0;
            {
                st_int  shifted = a << b;

                /*  Fail if any significant bit was pushed out.  */
                if ((shifted >> b) != a)
                    return 0;
                return answer_integer(shifted, 2);
            }
        }
        if (b <= -31)
            return answer_integer(a < 0 ? -1 : 0, 2);
        return answer_integer(a >> (-b), 2);
    default:
        return 0;
    }
}

/*  ----------  Primitive 18: Number @  ----------  */

static int
primitive_make_point(void)
{
    st_oop  x = ST_stack_value(1);
    st_oop  y = ST_stack_value(0);
    st_oop  point;

    if (!OM_is_int(x) || !OM_is_int(y))
        return 0;
    point = OM_instantiate_pointers(ST_CLASS_POINT, 2);
    if (!OM_is_object(point))
        return 0;
    OM_store_pointer(ST_POINT_X, point, x);
    OM_store_pointer(ST_POINT_Y, point, y);
    ST_pop_n(2);
    ST_push(point);
    return 1;
}

/*  ----------  Object shape  ----------
 *
 *  A class's format word says how its instances are laid out.  The layout
 *  below was derived from the image itself rather than from the book, by
 *  reading the format of classes whose shape is known:
 *
 *      Point        16rC005   pointers, not indexable, 2 fixed fields
 *      Semaphore    16rC007   pointers, not indexable, 3 fixed fields
 *      MethodContext 16rE00D  pointers, indexable,     6 fixed fields
 *      Array        16rE001   pointers, indexable,     0 fixed fields
 *      Float        16r6001   words,    indexable,     0 fixed fields
 *      DisplayBitmap 16r6001  words,    indexable,     0 fixed fields
 *      String       16r2001   bytes,    indexable,     0 fixed fields
 *
 *  giving, on the raw SmallInteger pointer:
 *
 *      bit 15  instances hold pointers
 *      bit 14  instances hold words or pointers rather than bytes
 *      bit 13  instances are indexable
 *      bits 1..11  count of named instance variables
 *
 *  The shifts apply to the object POINTER, not to its integer value; the
 *  pointer carries the value shifted left one with a tag in the low bit, and
 *  that shift is already accounted for here.
 */

typedef struct {
    int         pointers;
    int         words;          /*  word-indexable, as opposed to bytes  */
    int         indexable;
    uint32_t    fixed;
} om_shape;

static om_shape
shape_of_class(st_oop cls)
{
    om_shape    shape;
    st_oop      format;

    memset(&shape, 0, sizeof shape);
    if (!OM_is_object(cls))
        return shape;
    format = OM_fetch_pointer(ST_CLASS_FORMAT, cls);
    if (!OM_is_int(format))
        return shape;
    shape.pointers  = (format >> 15) & 1;
    shape.words     = !shape.pointers && ((format >> 14) & 1);
    shape.indexable = (format >> 13) & 1;
    shape.fixed     = (uint32_t) ((format >> 1) & 0x7FF);
    return shape;
}

/*  ----------  Indexed access, primitives 60 to 62  ----------  */

/*
 *  The indexable fields start after the named instance variables, so an
 *  index is only valid within what is left.
 */
static int
indexable_length(st_oop object, const om_shape *shape)
{
    if (shape->pointers) {
        uint32_t    length = OM_fetch_word_length(object);

        return (length < shape->fixed) ? 0 : (int) (length - shape->fixed);
    }
    if (shape->words)
        return (int) OM_fetch_word_length(object);
    return (int) OM_fetch_byte_length(object);
}

static int
primitive_at(void)
{
    st_oop      object = ST_stack_value(1);
    uint32_t    index;
    om_shape    shape;
    int         length;

    if (!OM_is_object(object)
     || !positive_16bit_value(ST_stack_value(0), &index))
        return 0;
    shape  = shape_of_class(OM_fetch_class(object));
    length = indexable_length(object, &shape);
    if (index < 1 || (int) index > length)
        return 0;
    if (shape.pointers) {
        st_oop  value = OM_fetch_pointer(shape.fixed + index - 1, object);

        ST_pop_n(2);
        ST_push(value);
        return 1;
    }
    if (shape.words)
        return answer_positive_16bit(OM_fetch_word(index - 1, object), 2);
    return answer_integer(OM_fetch_byte(index - 1, object), 2);
}

static int
primitive_at_put(void)
{
    st_oop      object = ST_stack_value(2);
    st_oop      value  = ST_stack_value(0);
    uint32_t    index;
    om_shape    shape;
    int         length;

    if (!OM_is_object(object)
     || !positive_16bit_value(ST_stack_value(1), &index))
        return 0;
    shape  = shape_of_class(OM_fetch_class(object));
    length = indexable_length(object, &shape);
    if (index < 1 || (int) index > length)
        return 0;
    if (shape.pointers) {
        OM_store_pointer(shape.fixed + index - 1, object, value);
        ST_pop_n(3);
        ST_push(value);
        return 1;
    }
    if (shape.words) {
        uint32_t    v;

        if (!positive_16bit_value(value, &v) || v > 65535)
            return 0;
        OM_store_word(index - 1, object, (uint16_t) v);
    }  else  {
        if (!OM_is_int(value))
            return 0;
        if (OM_int_value(value) < 0 || OM_int_value(value) > 255)
            return 0;
        OM_store_byte(index - 1, object, (uint8_t) OM_int_value(value));
    }
    ST_pop_n(3);
    ST_push(value);
    return 1;
}

static int
primitive_size(void)
{
    st_oop      object = ST_stack_value(0);
    om_shape    shape;

    if (!OM_is_object(object))
        return 0;
    shape = shape_of_class(OM_fetch_class(object));
    return answer_positive_16bit((uint32_t) indexable_length(object, &shape), 1);
}

/*  ----------  Instantiation, primitives 70 and 71  ----------  */

static int
primitive_new(void)
{
    st_oop      cls = ST_stack_value(0);
    st_oop      instance;
    om_shape    shape;

    if (!OM_is_object(cls))
        return 0;
    shape = shape_of_class(cls);
    if (shape.indexable)
        return 0;               /*  an indexable class needs new:  */
    instance = OM_instantiate_pointers(cls, shape.fixed);
    if (!OM_is_object(instance))
        return 0;
    ST_pop_n(1);
    ST_push(instance);
    return 1;
}

/*
 *  The format word says whether instances hold pointers, words or bytes, so
 *  new: has to consult it rather than guess.
 */
static int
primitive_new_with_arg(void)
{
    st_oop      cls = ST_stack_value(1);
    uint32_t    count;
    st_oop      instance;
    om_shape    shape;

    if (!OM_is_object(cls) || !positive_16bit_value(ST_stack_value(0), &count))
        return 0;
    shape = shape_of_class(cls);
    if (!shape.indexable)
        return 0;
    if (shape.pointers)
        instance = OM_instantiate_pointers(cls, shape.fixed + count);
    else if (shape.words)
        instance = OM_instantiate_words(cls, count);
    else
        instance = OM_instantiate_bytes(cls, count);
    if (!OM_is_object(instance))
        return 0;
    ST_pop_n(2);
    ST_push(instance);
    return 1;
}

/*  ----------  Primitive 72: become:  ----------
 *
 *  Two-way identity exchange.  With an object table this is a swap of two
 *  table entries and nothing else in the heap moves or is rewritten -- which
 *  is the argument for keeping the table once threads arrive, where the
 *  alternative is a stop-the-world heap scan.
 */
static int
primitive_become(void)
{
    st_oop  a = ST_stack_value(1);
    st_oop  b = ST_stack_value(0);

    if (!OM_is_object(a) || !OM_is_object(b))
        return 0;
    OM_swap_identities(a, b);
    ST_pop_n(1);
    return 1;
}

/*  ----------  Instance variable access, primitives 73 and 74  ----------  */

static int
primitive_inst_var_at(void)
{
    st_oop  object = ST_stack_value(1);
    st_int  index;

    if (!OM_is_object(object) || !integer_arg(0, &index))
        return 0;
    if (index < 1 || (uint32_t) index > OM_fetch_word_length(object))
        return 0;
    ST_pop_n(2);
    ST_push(OM_fetch_pointer((uint32_t) index - 1, object));
    return 1;
}

static int
primitive_inst_var_at_put(void)
{
    st_oop  object = ST_stack_value(2);
    st_oop  value  = ST_stack_value(0);
    st_int  index;

    if (!OM_is_object(object) || !integer_arg(1, &index))
        return 0;
    if (index < 1 || (uint32_t) index > OM_fetch_word_length(object))
        return 0;
    OM_store_pointer((uint32_t) index - 1, object, value);
    ST_pop_n(3);
    ST_push(value);
    return 1;
}

/*  ----------  Blocks, primitives 80 and 81  ----------  */

static int
primitive_block_copy(void)
{
    st_oop      context = ST_stack_value(1);
    st_int      argc;
    st_oop      home;
    st_oop      block;
    uint32_t    size;
    st_oop      initial_ip;

    if (!integer_arg(0, &argc) || !OM_is_object(context))
        return 0;
    if (OM_fetch_class(context) == ST_CLASS_BLOCK_CONTEXT)
        home = OM_fetch_pointer(ST_CTX_HOME, context);
    else
        home = context;
    size  = OM_fetch_word_length(home);
    block = OM_instantiate_pointers(ST_CLASS_BLOCK_CONTEXT, size);
    if (!OM_is_object(block))
        return 0;

    /*
     *  The block's code begins after the blockCopy: send and the jump the
     *  compiler emits to skip over the block body: two bytes, plus one for
     *  the image's one-relative instruction pointers.
     */
    initial_ip = OM_int_oop((st_int) st_vm.instruction_pointer + 3);
    OM_store_pointer(ST_CTX_INITIAL_IP, block, initial_ip);
    OM_store_pointer(ST_CTX_IP, block, initial_ip);
    OM_store_pointer(ST_CTX_SP, block, OM_int_oop(0));
    OM_store_pointer(ST_CTX_BLOCK_ARG_COUNT, block, OM_int_oop(argc));
    OM_store_pointer(ST_CTX_HOME, block, home);

    ST_pop_n(2);
    ST_push(block);
    return 1;
}

/*
 *  Activating a block is a context switch, so it is implemented in the
 *  interpreter where the registers live.
 */
int         ST_activate_block(st_oop block, uint32_t argc);

static int
primitive_value(uint32_t argc)
{
    st_oop  block = ST_stack_value(argc);

    if (!OM_is_object(block) || OM_fetch_class(block) != ST_CLASS_BLOCK_CONTEXT)
        return 0;
    {
        st_oop  want = OM_fetch_pointer(ST_CTX_BLOCK_ARG_COUNT, block);

        if (!OM_is_int(want) || (uint32_t) OM_int_value(want) != argc)
            return 0;
    }
    return ST_activate_block(block, argc);
}

/*  ----------  Primitive 96: copyBits  ----------
 *
 *  The one primitive the Blue Book says the whole graphics system needs.
 */
static int
primitive_copy_bits(void)
{
    st_oop      bitblt = ST_stack_top();
    gfx_blit    blit;

    if (!GFX_blit_from_oop(bitblt, &blit))
        return 0;
    GFX_copy_bits(&blit);
    /*
     *  If that drew on the screen, remember the region so the window is
     *  refreshed.  The clipped rectangle is the one actually touched.
     */
    if (blit.dest.oop == GFX_display_form())
        GFX_damage(blit.damage_x, blit.damage_y, blit.damage_w, blit.damage_h);
    return 1;                   /*  answers the receiver  */
}

/*  ----------  Input and display, primitives 90 to 95, 101, 102  ----------  */

static int
primitive_mouse_point(void)
{
    st_oop  point;
    int     x;
    int     y;

    GFX_mouse_point(&x, &y);
    point = OM_instantiate_pointers(ST_CLASS_POINT, 2);
    if (!OM_is_object(point))
        return 0;
    OM_store_pointer(ST_POINT_X, point, OM_int_oop(x));
    OM_store_pointer(ST_POINT_Y, point, OM_int_oop(y));
    ST_pop_n(1);
    ST_push(point);
    return 1;
}

static int
primitive_input_semaphore(void)
{
    SCHED_set_input_semaphore(ST_stack_value(0));
    ST_pop_n(1);
    return 1;                   /*  receiver remains as the result  */
}

static int
primitive_input_word(void)
{
    uint16_t    word;

    if (!GFX_next_event_word(&word))
        return 0;               /*  empty buffer: the primitive fails  */
    ST_pop_n(1);
    ST_push(OM_int_oop((st_int) word));
    return 1;
}

static int
primitive_be_display(void)
{
    GFX_set_display(ST_stack_top());
    return 1;
}

static int
primitive_be_cursor(void)
{
    /*  The host draws its own pointer; the image's cursor Form is ignored. */
    return 1;
}

/*  ----------  Time, primitives 98 to 100  ----------  */

/*
 *  Both clocks answer 32-bit values through a four-byte object, since a
 *  SmallInteger cannot hold them.
 */
static int
store_32_into(st_oop target, uint32_t value)
{
    if (!OM_is_object(target) || OM_fetch_byte_length(target) != 4)
        return 0;
    OM_store_byte(0, target, (uint8_t) (value & 0xFF));
    OM_store_byte(1, target, (uint8_t) ((value >> 8) & 0xFF));
    OM_store_byte(2, target, (uint8_t) ((value >> 16) & 0xFF));
    OM_store_byte(3, target, (uint8_t) ((value >> 24) & 0xFF));
    return 1;
}

static int
primitive_time_words_into(void)
{
    st_oop      target = ST_stack_value(0);
    uint32_t    seconds = (uint32_t) (ST_time_smalltalk_ms() / 1000);

    if (!store_32_into(target, seconds))
        return 0;
    ST_pop_n(1);
    return 1;
}

static int
primitive_tick_words_into(void)
{
    st_oop      target = ST_stack_value(0);
    uint32_t    ticks = (uint32_t) (ST_time_monotonic_ns() / 1000000);

    if (!store_32_into(target, ticks))
        return 0;
    ST_pop_n(1);
    return 1;
}

/*  ----------  Identity and class, primitives 110 and 111  ----------  */

static int
primitive_equivalent(void)
{
    return answer_boolean(ST_stack_value(1) == ST_stack_value(0), 2);
}

static int
primitive_class(void)
{
    st_oop  cls = OM_fetch_class(ST_stack_value(0));

    ST_pop_n(1);
    ST_push(cls);
    return 1;
}

/*  ----------  Dispatch  ----------  */

int
ST_primitive_dispatch(unsigned index)
{
    if (index >= 1 && index <= 17)
        return arithmetic_primitive(index);

    switch (index) {
    case 18:  return primitive_make_point();
    case 60:  return primitive_at();
    case 61:  return primitive_at_put();
    case 62:  return primitive_size();
    case 70:  return primitive_new();
    case 71:  return primitive_new_with_arg();
    case 72:  return primitive_become();
    case 73:  return primitive_inst_var_at();
    case 74:  return primitive_inst_var_at_put();
    case 80:  return primitive_block_copy();
    case 81:  return primitive_value(st_vm.argument_count);
    case 85:  return SCHED_primitive_signal();
    case 86:  return SCHED_primitive_wait();
    case 87:  return SCHED_primitive_resume();
    case 88:  return SCHED_primitive_suspend();
    case 89:  return 1;         /*  flushCache: we keep no method cache yet  */
    case 90:  return primitive_mouse_point();
    case 91:  return 1;         /*  cursorLocPut: the host owns the pointer  */
    case 92:  return 1;         /*  cursorLink:                              */
    case 93:  return primitive_input_semaphore();
    case 94:  return 1;         /*  sampleInterval:                          */
    case 95:  return primitive_input_word();
    case 96:  return primitive_copy_bits();
    case 98:  return primitive_time_words_into();
    case 99:  return primitive_tick_words_into();
    case 101: return primitive_be_cursor();
    case 102: return primitive_be_display();
    case 110: return primitive_equivalent();
    case 111: return primitive_class();
    default:  return 0;
    }
}

/*
 *  The special-selector bytecodes.  The arithmetic group runs only when the
 *  receiver is a SmallInteger; everything else falls through to a real send,
 *  which is why "aMetaclass new" appears in the reference traces as a send
 *  followed by Primitive #70 from Behavior>>new's own header rather than as
 *  a silent special-selector primitive.
 */
int
ST_special_selector_primitive(uint8_t code)
{
    static const unsigned arithmetic_map[16] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 17, 12, 14, 15
    };

    if (code >= 176 && code <= 191) {
        if (!OM_is_int(ST_stack_value(1)))
            return 0;
        /*
         *  Through the full dispatcher, not arithmetic_primitive: the @
         *  bytecode maps to primitive 18, which builds a Point and is not
         *  part of the 1..17 arithmetic group.
         */
        return ST_primitive_dispatch(arithmetic_map[code - 176]);
    }
    switch (code) {
    case 198: return primitive_equivalent();
    case 199: return primitive_class();
    case 200: return primitive_block_copy();
    case 201: return primitive_value(0);
    case 202: return primitive_value(1);
    default:  return 0;
    }
}

void
ST_must_be_boolean(st_oop value)
{
    char    buf[256];

    ST_print_object(value, buf, sizeof buf);
    fprintf(stderr, "st80: %s is not a boolean at cycle %llu\n", buf,
            (unsigned long long) st_vm.cycle);
    st_vm.running = 0;
}

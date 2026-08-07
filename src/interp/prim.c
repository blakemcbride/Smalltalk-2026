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
#include <math.h>

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

/*
 *  Answer any non-negative value, promoting to a LargePositiveInteger when
 *  it will not fit a SmallInteger.  coreLeft reports hundreds of thousands
 *  of words, which is far outside the 15-bit range the Blue Book memory
 *  gives a SmallInteger, so answering one is not optional.
 *
 *  Digits are stored least significant first, as elsewhere.
 */
static int
answer_positive(uint64_t value, uint32_t pop)
{
    st_oop      big;
    unsigned    bytes = 0;
    uint64_t    scan  = value;
    unsigned    i;

    if (value <= (uint64_t) ST_INT_MAX) {
        ST_pop_n(pop);
        ST_push(OM_int_oop((st_int) value));
        return 1;
    }
    while (scan) {
        ++bytes;
        scan >>= 8;
    }
    big = OM_instantiate_bytes(ST_CLASS_LARGE_POSITIVE_INTEGER, bytes);
    if (!OM_is_present(big))
        return 0;
    for (i = 0; i < bytes; ++i)
        OM_store_byte(i, big, (uint8_t) ((value >> (i * 8)) & 0xFF));
    ST_pop_n(pop);
    ST_push(big);
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
 *
 *  answer_integer is what enforces that, and for + and - it is enough,
 *  because the SUM of two SmallIntegers always fits st_int and can be
 *  tested exactly:
 *
 *      bb   st_int is int32_t and the range is +/-16384, so two of them
 *           cannot come within three orders of magnitude of overflowing.
 *      mt   st_int is int64_t and the range is +/-2^62, so the widest
 *           possible sum is 2*(2^62-1) = 2^63-2 and the widest difference
 *           is 2^63-1 -- INT64_MAX exactly, and the low end lands on
 *           INT64_MIN exactly.  Both are representable, so a + b is the
 *           true value and answer_integer sees it.
 *
 *  MULTIPLICATION is different and this is where the bug was.  21 factorial
 *  is 20 factorial times 21, whose true value needs 66 bits; a * b wrapped,
 *  the wrapped value was -4249290049419214848, that is comfortably inside
 *  +/-2^62, so answer_integer was handed something that fit and the
 *  primitive SUCCEEDED with an answer off by 2^64.  The Smalltalk body that
 *  would have promoted it never ran.
 *
 *  It is the shape this system keeps producing: the VM being more forgiving
 *  than the image expects.  A primitive that quietly succeeds where it was
 *  required to fail is invisible, because failing is the normal path and
 *  nothing reports not taking it.  So the overflow has to be detected
 *  BEFORE the product is formed, which is what multiply_fits does.
 */

/*
 *  a * b, answered through *out, or 0 if it leaves SmallInteger range.
 *
 *  Checked by division rather than by computing and looking, because
 *  computing is the thing that must not happen: signed overflow is
 *  undefined behaviour, so a wrapped product is not merely a wrong number
 *  the compiler is entitled to assume cannot occur.  Division truncates
 *  toward zero in C99, which is what makes each comparison exact.
 *
 *  There is no ST_INT_MIN / -1 trap here: ST_INT_MIN is -2^62, not
 *  INT64_MIN, so its negation is representable.
 */
static int
multiply_fits(st_int a, st_int b, st_int *out)
{
    if (a == 0 || b == 0) {
        *out = 0;
        return 1;
    }
    if (a > 0) {
        if (b > 0) {
            if (a > ST_INT_MAX / b)
                return 0;
        } else if (b < ST_INT_MIN / a) {
            return 0;
        }
    } else {
        if (b > 0) {
            if (a < ST_INT_MIN / b)
                return 0;
        } else if (a < ST_INT_MAX / b) {
            return 0;
        }
    }
    *out = a * b;
    return 1;
}

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
    case 9: {                       /*  *  */
        st_int  product;

        if (!multiply_fits(a, b, &product))
            return 0;               /*  the Smalltalk body promotes it  */
        return answer_integer(product, 2);
    }
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
    case 17: {                      /*  bitShift:  */
        /*
         *  How far a shift can go before it stops being defined at all.
         *  Two below the width leaves room for the sign bit and for the
         *  one bit a shift by exactly that amount would need.
         */
        enum { SHIFT_LIMIT = (int) (sizeof(st_int) * 8) - 2 };

        if (b >= 0) {
            st_int  shifted;

            if (b >= SHIFT_LIMIT)
                return 0;
            /*
             *  A left shift by multiplication, deliberately.
             *
             *  "a << b" is undefined for negative a, and undefined again if
             *  the result overflows -- so the old "shift, then check that
             *  shifting back agrees" could not detect what it was testing
             *  for: the compiler is entitled to assume the overflow never
             *  happened and fold the check away.  Multiplying by 2^b is the
             *  same value with none of that, and it reuses the one place
             *  overflow is decided.
             *
             *  The old bound of 31 was inherited from the 16-bit memory and
             *  refused shifts this memory can represent perfectly well:
             *  1 bitShift: 40 fell back to the Smalltalk body for no reason.
             */
            if (!multiply_fits(a, (st_int) 1 << b, &shifted))
                return 0;
            return answer_integer(shifted, 2);
        }
        /*  A right shift is arithmetic: it cannot leave the range.  */
        if (-b >= SHIFT_LIMIT)
            return answer_integer(a < 0 ? -1 : 0, 2);
        return answer_integer(a >> (-b), 2);
    }
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

/*
 *  63 and 64: String at: and at:put:.
 *
 *  The same access as 60 and 61 but in Characters rather than in the byte
 *  values, which is why the Blue Book gives them numbers of their own.  A
 *  String's elements ARE Characters as far as Smalltalk is concerned; only
 *  the storage is bytes.
 *
 *  Symbol class>>intern: cannot run without 64: it builds a new Symbol with
 *  "super at: j put: (aString at: j)", so an image whose VM lacks it can
 *  intern nothing, and a library that cannot intern cannot print, because
 *  printString goes looking for a Symbol before it gets anywhere.
 */
static int
primitive_string_at(void)
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
    if (shape.pointers || shape.words || index < 1 || (int) index > length)
        return 0;
    {
        uint8_t     byte = OM_fetch_byte(index - 1, object);
        st_oop      ch = OM_fetch_pointer(byte, ST_CHARACTER_TABLE);

        if (!OM_is_object(ch))
            return 0;
        ST_pop_n(2);
        ST_push(ch);
    }
    return 1;
}

static int
primitive_string_at_put(void)
{
    st_oop      object = ST_stack_value(2);
    st_oop      value  = ST_stack_value(0);
    uint32_t    index;
    om_shape    shape;
    int         length;
    st_oop      code;

    if (!OM_is_object(object)
     || !positive_16bit_value(ST_stack_value(1), &index))
        return 0;
    shape  = shape_of_class(OM_fetch_class(object));
    length = indexable_length(object, &shape);
    if (shape.pointers || shape.words || index < 1 || (int) index > length)
        return 0;
    /*  The argument must be a Character; its value is its only field.  */
    if (!OM_is_object(value) || OM_fetch_class(value) != ST_CLASS_CHARACTER)
        return 0;
    code = OM_fetch_pointer(0, value);
    if (!OM_is_int(code) || OM_int_value(code) < 0 || OM_int_value(code) > 255)
        return 0;
    OM_store_byte(index - 1, object, (uint8_t) OM_int_value(code));
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

/*
 *  A fresh activation record for a block, copying what makes it that block.
 *
 *  A Blue Book BlockContext is two things at once: the closure, which
 *  blockCopy: made and which somebody holds, AND the frame an activation
 *  runs in.  ST_activate_block writes the instruction pointer, the stack
 *  pointer, the caller and the arguments into it -- so evaluating a block
 *  while an evaluation of the SAME block object is already in progress
 *  overwrites the state of the one still running.  Three consequences, and
 *  only the first is a 1983 behaviour worth preserving anywhere:
 *
 *      A recursive block corrupts its own frame.  fib and factorial written
 *      as blocks answer nil.  Xerox's blocks did this too.
 *
 *      Two workers cannot evaluate one block object at the same time.  That
 *      is not a compatibility question, it is a correctness bug in a system
 *      whose point is parallelism, and forkParallel: would meet it the
 *      moment it handed one block to N workers.
 *
 *      A block cannot outlive its home.  This does NOT fix that; real
 *      closures do, and they are Phase D.
 *
 *  So each activation gets its own record and the original stays the
 *  pristine closure.  Its identity is what nobody may share; the home it
 *  points at is shared deliberately, because that is what closing over an
 *  enclosing frame means -- and under threads a race there is the program's
 *  business, exactly as doc/CONCURRENCY.md already says of every field.
 *
 *  The copy is held only in C until ST_activate_block makes it the active
 *  context, which counts it.  That is safe here for the reason it is safe
 *  in primitive_block_copy above: nothing between the two allocates, and a
 *  collection cannot run where nothing allocates.
 */
static st_oop
copy_block_for_activation(st_oop block)
{
    uint32_t    size = OM_fetch_word_length(block);
    st_oop      copy = OM_instantiate_pointers(ST_CLASS_BLOCK_CONTEXT, size);

    if (!OM_is_object(copy))
        return ST_OOP_INVALID;
    OM_store_pointer(ST_CTX_INITIAL_IP, copy,
                     OM_fetch_pointer(ST_CTX_INITIAL_IP, block));
    OM_store_pointer(ST_CTX_BLOCK_ARG_COUNT, copy,
                     OM_fetch_pointer(ST_CTX_BLOCK_ARG_COUNT, block));
    OM_store_pointer(ST_CTX_HOME, copy,
                     OM_fetch_pointer(ST_CTX_HOME, block));
    return copy;
}

/*
 *  Activating a BlockClosure.  Squeak's numbers, because ported Pharo
 *  source declares exactly these and has to keep working unedited:
 *  201 to 205 are value through value:value:value:value:, 206 is
 *  valueWithArguments:, and 221/222 are the valueNoContextSwitch pair --
 *  aliases here, since a process switch is decided once per bytecode rather
 *  than at a send, so there is nothing to suppress.
 */
/*
 *  248: Object>>primitiveReportOnStandardError: aString
 *
 *  A headless image has no way to say anything.  Transcript is a
 *  TextCollector: it draws, and drawing is exactly what is unavailable when
 *  there is no screen -- so an unhandled error could only be seen by the
 *  one means that was not there.  One line to stderr costs nothing and
 *  makes every headless diagnostic possible, this one first.
 */
static int
primitive_report_on_standard_error(void)
{
    st_oop      text = ST_stack_value(0);
    uint32_t    n;
    uint32_t    i;

    if (!OM_is_object(text))
        return 0;
    /*
     *  Silent when the VM's own reporting is off.  The bootstrap turns it
     *  off deliberately for the first pass of the class initializers, which
     *  is expected to fail partway; a report that ignored that would print
     *  the same handful of failures on every build and teach nobody
     *  anything.
     */
    if (ST_errors_reported()) {
        n = OM_fetch_byte_length(text);
        for (i = 0; i < n; ++i)
            fputc(OM_fetch_byte(i, text), stderr);
        fputc('\n', stderr);
        fflush(stderr);
    }
    ST_pop_n(1);                    /*  answers the receiver  */
    return 1;
}

/*  Is this object a context of either kind?  */
static int
is_a_context(st_oop p)
{
    st_oop  cls;

    if (!OM_is_object(p))
        return 0;
    cls = OM_fetch_class(p);
    return cls == ST_CLASS_METHOD_CONTEXT || cls == ST_CLASS_BLOCK_CONTEXT;
}

/*
 *  246: ContextPart>>return: value.  Abandon everything up to and including
 *  the receiver, and let the send that created it answer `value`.
 */
static int
primitive_context_return(void)
{
    st_oop  ctx   = ST_stack_value(1);
    st_oop  value = ST_stack_value(0);

    if (!is_a_context(ctx))
        return 0;
    if (!OM_is_present(OM_fetch_pointer(ST_CTX_SENDER, ctx)))
        return 0;                   /*  nothing to answer to  */
    ST_pop_n(2);
    ST_return_to(value, ctx);
    return 1;
}

/*
 *  247: ContextPart>>resume: value.  Carry on where the receiver stopped,
 *  as though the send it was waiting on had answered `value`.
 */
static int
primitive_context_resume(void)
{
    st_oop  ctx   = ST_stack_value(1);
    st_oop  value = ST_stack_value(0);

    if (!is_a_context(ctx))
        return 0;
    ST_pop_n(2);
    ST_resume_at(value, ctx);
    return 1;
}

static int
primitive_closure_value(uint32_t argc)
{
    st_oop  closure = ST_stack_value(argc);
    st_oop  want;

    if (!ST_is_block_closure(closure))
        return 0;
    want = OM_fetch_pointer(ST_CLOSURE_NUM_ARGS, closure);
    if (!OM_is_int(want) || (uint32_t) OM_int_value(want) != argc)
        return 0;                   /*  the Smalltalk body reports it  */
    return ST_activate_closure(closure, argc);
}

/*
 *  valueWithArguments:, for both kinds of block.
 *
 *  The arguments arrive in an Array and have to be spread onto the stack
 *  where an activation expects to find them.  Primitive 82 is the Blue Book
 *  number for the BlockContext form -- declared by BlockContext since 1983
 *  and never implemented here, which mattered because its Smalltalk
 *  fallback has the argument-count test inverted and cannot work either.
 */
static int
primitive_value_with_arguments(int closure_form)
{
    st_oop      block = ST_stack_value(1);
    st_oop      args  = ST_stack_value(0);
    st_oop      want;
    uint32_t    argc;
    uint32_t    i;

    if (!OM_is_object(args) || OM_fetch_class(args) != ST_CLASS_ARRAY)
        return 0;
    argc = OM_fetch_word_length(args);

    if (closure_form) {
        if (!ST_is_block_closure(block))
            return 0;
        want = OM_fetch_pointer(ST_CLOSURE_NUM_ARGS, block);
    }  else  {
        if (!OM_is_object(block)
         || OM_fetch_class(block) != ST_CLASS_BLOCK_CONTEXT)
            return 0;
        want = OM_fetch_pointer(ST_CTX_BLOCK_ARG_COUNT, block);
    }
    if (!OM_is_int(want) || (uint32_t) OM_int_value(want) != argc)
        return 0;

    /*
     *  Replace the Array with its elements: the receiver stays where it is
     *  and the activation then finds exactly what an ordinary send left.
     */
    ST_pop_n(1);
    for (i = 0; i < argc; ++i)
        ST_push(OM_fetch_pointer(i, args));

    if (closure_form)
        return ST_activate_closure(block, argc);
    {
        st_oop  activation = copy_block_for_activation(block);

        if (activation == ST_OOP_INVALID)
            return 0;
        return ST_activate_block(activation, argc);
    }
}

static int
primitive_value(uint32_t argc)
{
    st_oop  block = ST_stack_value(argc);
    st_oop  activation;

    /*
     *  Bytecodes 201 and 202 come here before any lookup, and a closure is
     *  a perfectly good receiver for them -- so take it rather than failing
     *  into a send that would only arrive back at primitive 201.
     */
    if (ST_is_block_closure(block))
        return primitive_closure_value(argc);
    if (!OM_is_object(block) || OM_fetch_class(block) != ST_CLASS_BLOCK_CONTEXT)
        return 0;
    {
        st_oop  want = OM_fetch_pointer(ST_CTX_BLOCK_ARG_COUNT, block);

        if (!OM_is_int(want) || (uint32_t) OM_int_value(want) != argc)
            return 0;
    }
    activation = copy_block_for_activation(block);
    if (activation == ST_OOP_INVALID)
        return 0;
    return ST_activate_block(activation, argc);
}

/*
 *  75: the identity hash.
 *
 *  The Blue Book defines it as half the object pointer read as a signed
 *  16-bit quantity, which is a 16-bit memory talking about itself.  What the
 *  callers need is only that it is a SmallInteger, that it never changes for
 *  an object, and that it spreads -- every Set and Dictionary in the library
 *  hashes with it, so without this nothing can be stored in one at all.
 *
 *  The 64-bit memory keeps a hash in the object header for exactly this, and
 *  it survives compaction, which an address-derived hash would not.  The
 *  16-bit memory has no header field to spare, so it answers the pointer
 *  itself, which is what the 1983 machine did.
 */
static int
primitive_object_hash(void)
{
    st_oop      object = ST_stack_value(0);
    uint32_t    hash;

    if (!OM_is_object(object))
        return 0;               /*  a SmallInteger is its own hash  */
    hash = OM_identity_hash(object);
    return answer_positive_16bit(hash, 1);
}

/*
 *  77 and 78: walking every instance of a class.
 *
 *  Symbol class>>rehash uses them, and so does anything that asks the system
 *  what exists.  The walk is over the object table, which is the only place
 *  that knows.
 */
static int
primitive_some_instance(void)
{
    st_oop  class_oop = ST_stack_value(0);
    st_oop  found = OM_next_instance_after(ST_OOP_INVALID, class_oop);

    if (!OM_is_present(found))
        return 0;               /*  none: the primitive fails  */
    ST_pop_n(1);
    ST_push(found);
    return 1;
}

static int
primitive_next_instance(void)
{
    st_oop  object = ST_stack_value(0);
    st_oop  found;

    if (!OM_is_object(object))
        return 0;
    found = OM_next_instance_after(object, OM_fetch_class(object));
    if (!OM_is_present(found))
        return 0;
    ST_pop_n(1);
    ST_push(found);
    return 1;
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

/*  ----------  Floats, primitives 40 to 54  ----------
 *
 *  Smalltalk-80 Floats are IEEE 754 single precision: two 16-bit words, most
 *  significant first.  That was read out of the 1983 image rather than
 *  assumed -- 16r3F80 0000 is 1.0 and 16r3E99 9999 is 0.3.
 *
 *  A bootstrapped image is free to carry double precision instead, so the
 *  accessors below take the width from the object and handle either.
 */

static int
float_value(st_oop p, double *out)
{
    uint32_t    words;

    if (!OM_is_present(p) || OM_fetch_class(p) != ST_CLASS_FLOAT)
        return 0;
    words = OM_fetch_word_length(p);
    if (words == 2) {
        union { float f; uint32_t u; } bits;

        bits.u = ((uint32_t) OM_fetch_word(0, p) << 16) | OM_fetch_word(1, p);
        *out = (double) bits.f;
        return 1;
    }
    if (words == 4) {
        union { double d; uint64_t u; } bits;
        unsigned    i;

        bits.u = 0;
        for (i = 0; i < 4; ++i)
            bits.u = (bits.u << 16) | OM_fetch_word(i, p);
        *out = bits.d;
        return 1;
    }
    return 0;
}

static st_oop
make_float(double value)
{
    union { float f; uint32_t u; } bits;
    st_oop      p = OM_instantiate_words(ST_CLASS_FLOAT, 2);

    if (!OM_is_present(p))
        return ST_OOP_INVALID;
    bits.f = (float) value;
    OM_store_word(0, p, (uint16_t) (bits.u >> 16));
    OM_store_word(1, p, (uint16_t) (bits.u & 0xFFFF));
    return p;
}

/*  Either operand may be the receiver, so both are read the same way.  */
static int
float_operands(double *a, double *b)
{
    return float_value(ST_stack_value(1), a) && float_value(ST_stack_value(0), b);
}

static int
answer_float(double value, uint32_t pop)
{
    st_oop  result = make_float(value);

    if (result == ST_OOP_INVALID)
        return 0;
    ST_pop_n(pop);
    ST_push(result);
    return 1;
}

static int
float_primitive(unsigned index)
{
    double  a;
    double  b;

    switch (index) {
    case 40: {                          /*  SmallInteger asFloat  */
        st_int  value;

        if (!integer_arg(0, &value))
            return 0;
        return answer_float((double) value, 1);
    }
    case 51: {                          /*  truncated  */
        if (!float_value(ST_stack_value(0), &a))
            return 0;
        if (a > (double) ST_INT_MAX || a < (double) ST_INT_MIN)
            return 0;                   /*  needs a LargeInteger: fail  */
        return answer_integer((st_int) a, 1);
    }
    case 52:                            /*  fractionPart  */
        if (!float_value(ST_stack_value(0), &a))
            return 0;
        return answer_float(a - (double) (long long) a, 1);
    case 53: {                          /*  exponent  */
        int exponent;

        if (!float_value(ST_stack_value(0), &a))
            return 0;
        if (a == 0.0)
            return answer_integer(0, 1);
        frexp(a, &exponent);
        return answer_integer(exponent - 1, 1);
    }
    case 54: {                          /*  timesTwoPower:  */
        st_int  power;

        if (!float_value(ST_stack_value(1), &a) || !integer_arg(0, &power))
            return 0;
        return answer_float(ldexp(a, (int) power), 2);
    }
    default:
        break;
    }

    if (!float_operands(&a, &b))
        return 0;
    switch (index) {
    case 41: return answer_float(a + b, 2);
    case 42: return answer_float(a - b, 2);
    case 43: return answer_boolean(a <  b, 2);
    case 44: return answer_boolean(a >  b, 2);
    case 45: return answer_boolean(a <= b, 2);
    case 46: return answer_boolean(a >= b, 2);
    case 47: return answer_boolean(a == b, 2);
    case 48: return answer_boolean(a != b, 2);
    case 49: return answer_float(a * b, 2);
    case 50:
        if (b == 0.0)
            return 0;
        return answer_float(a / b, 2);
    default: return 0;
    }
}

/*  ----------  CompiledMethods, primitives 68, 69 and 79  ----------
 *
 *  A method is a byte object whose leading words are the header and the
 *  literal frame, so objectAt: reaches those words while at: reaches the
 *  bytecodes.  The image builds methods at run time -- that is what the
 *  compiler in the image does -- so newMethod:header: is essential.
 */

static uint32_t
method_word_slots(st_oop method)
{
    return OM_fetch_byte_length(method) / (uint32_t) sizeof(st_oop) == 0
            ? OM_fetch_byte_length(method) / 2
            : OM_fetch_byte_length(method) / (uint32_t) sizeof(st_oop);
}

static int
primitive_object_at(void)
{
    st_oop      method = ST_stack_value(1);
    st_int      index;

    if (!OM_is_present(method) || !integer_arg(0, &index))
        return 0;
    if (index < 1 || (uint32_t) index > method_word_slots(method))
        return 0;
    ST_pop_n(2);
    ST_push(OM_fetch_pointer((uint32_t) index - 1, method));
    return 1;
}

static int
primitive_object_at_put(void)
{
    st_oop      method = ST_stack_value(2);
    st_oop      value  = ST_stack_value(0);
    st_int      index;

    if (!OM_is_present(method) || !integer_arg(1, &index))
        return 0;
    if (index < 1 || (uint32_t) index > method_word_slots(method))
        return 0;
    OM_store_pointer((uint32_t) index - 1, method, value);
    ST_pop_n(3);
    ST_push(value);
    return 1;
}

static int
primitive_new_method(void)
{
    st_oop      header = ST_stack_value(0);
    st_oop      cls    = ST_stack_value(2);
    st_int      bytecodes;
    uint32_t    literals;
    st_oop      method;

    if (!OM_is_int(header) || !integer_arg(1, &bytecodes) || bytecodes < 0)
        return 0;
    if (!OM_is_present(cls))
        return 0;
    literals = ST_header_literal_count(header);
    /*
     *  The header and the literal frame come first, then the bytecodes.  The
     *  stride is the object memory's pointer size, the same one the
     *  interpreter uses to find a method's first bytecode.
     */
    method = OM_instantiate_bytes(cls,
                (literals + 1) * (uint32_t) sizeof(st_oop)
                    + (uint32_t) bytecodes);
    if (!OM_is_present(method))
        return 0;
    OM_store_pointer(0, method, header);
    ST_pop_n(3);
    ST_push(method);
    return 1;
}

/*  ----------  Bulk copy, primitive 105  ----------  */

static int
primitive_replace_from_to_with_starting_at(void)
{
    st_oop      target = ST_stack_value(4);
    st_oop      source = ST_stack_value(1);
    st_int      start;
    st_int      stop;
    st_int      from;
    st_int      i;
    int         target_bytes;
    int         source_bytes;

    if (!OM_is_present(target) || !OM_is_present(source))
        return 0;
    if (!integer_arg(3, &start) || !integer_arg(2, &stop)
     || !integer_arg(0, &from))
        return 0;
    if (start < 1 || stop < start - 1)
        return 0;
    target_bytes = !OM_pointer_bit(target);
    source_bytes = !OM_pointer_bit(source);
    if (target_bytes != source_bytes)
        return 0;               /*  no mixing pointers and bytes  */

    for (i = 0; i <= stop - start; ++i) {
        uint32_t    to_index   = (uint32_t) (start + i - 1);
        uint32_t    from_index = (uint32_t) (from + i - 1);

        if (target_bytes) {
            if (to_index >= OM_fetch_byte_length(target)
             || from_index >= OM_fetch_byte_length(source))
                return 0;
            OM_store_byte(to_index, target, OM_fetch_byte(from_index, source));
        }  else  {
            if (to_index >= OM_fetch_word_length(target)
             || from_index >= OM_fetch_word_length(source))
                return 0;
            OM_store_pointer(to_index, target,
                             OM_fetch_pointer(from_index, source));
        }
    }
    ST_pop_n(4);
    return 1;                   /*  answers the receiver  */
}

/*  ----------  Reflection, primitives 83 and 84  ----------
 *
 *  perform: is how the image dispatches on a selector it computed rather
 *  than wrote.  The text scanner uses it for every stop condition, so
 *  without these two nothing draws: the scanner fails, the error path tries
 *  to report which method failed, and that walk recurses until the object
 *  table is gone.  Neither has a Smalltalk fallback -- they cannot, since
 *  performing a send is exactly what they are for.
 */

static int
primitive_perform(void)
{
    uint32_t    argc = st_vm.argument_count;
    st_oop      selector;
    uint32_t    i;

    /*  The selector is the first argument, so there is always at least one. */
    if (argc < 1)
        return 0;
    selector = ST_stack_value(argc - 1);
    if (!OM_is_present(selector))
        return 0;

    /*
     *  Shuffle the real arguments down over the selector, leaving the stack
     *  exactly as an ordinary send of that selector would have left it.
     */
    for (i = 0; i + 1 < argc; ++i)
        ST_stack_put(argc - 1 - i, ST_stack_value(argc - 2 - i));
    ST_pop_n(1);

    ST_send_selector(selector, argc - 1);
    return 1;
}

static int
primitive_perform_with_arguments(void)
{
    st_oop      arguments;
    st_oop      selector;
    uint32_t    count;
    uint32_t    i;

    if (st_vm.argument_count != 2)
        return 0;
    arguments = ST_stack_value(0);
    selector  = ST_stack_value(1);
    if (!OM_is_present(arguments) || !OM_is_present(selector))
        return 0;
    if (OM_fetch_class(arguments) != ST_CLASS_ARRAY)
        return 0;
    count = OM_fetch_word_length(arguments);

    /*  Drop the selector and the array, then spread the array out.  */
    ST_pop_n(2);
    for (i = 0; i < count; ++i)
        ST_push(OM_fetch_pointer(i, arguments));

    ST_send_selector(selector, count);
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
    case 63:  return primitive_string_at();
    case 64:  return primitive_string_at_put();
    case 70:  return primitive_new();
    case 71:  return primitive_new_with_arg();
    case 72:  return primitive_become();
    case 73:  return primitive_inst_var_at();
    case 74:  return primitive_inst_var_at_put();
    case 75:  return primitive_object_hash();
    case 77:  return primitive_some_instance();
    case 78:  return primitive_next_instance();
    case 80:  return primitive_block_copy();
    case 81:  return primitive_value(st_vm.argument_count);
    case 40: case 41: case 42: case 43: case 44: case 45: case 46:
    case 47: case 48: case 49: case 50: case 51: case 52: case 53:
    case 54:
        return float_primitive(index);
    case 68:  return primitive_object_at();
    case 69:  return primitive_object_at_put();
    case 79:  return primitive_new_method();
    case 83:  return primitive_perform();
    case 84:  return primitive_perform_with_arguments();
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
    case 105: return primitive_replace_from_to_with_starting_at();
    case 113:
        /*
         *  quit.  Stop, and let the driver leave.
         *
         *  Without it, SystemDictionary>>quitPrimitive fell through to
         *  "self primitiveFailed", so choosing "Quit, without saving" from
         *  the system menu raised an error and printed a backtrace instead
         *  of quitting -- the one menu item whose whole job is to leave.
         *
         *  The receiver stays on the stack; nothing will read it.
         */
        ST_quit_requested = 1;
        st_vm.running = 0;
        return 1;
    case 112:                   /*  coreLeft  */
        return answer_positive(OM_core_left(), 1);
    case 115:                   /*  oopsLeft  */
        return answer_positive(OM_oops_left(), 1);
    case 116:
        /*
         *  signal:atOopsLeft:wordsLeft: arms a low-space warning.  We have
         *  no such warning yet, so accept the request and answer the
         *  receiver rather than fail into the image's error path.
         */
        ST_pop_n(3);
        return 1;
    /*
     *  246 and 247: jumping to a context, which the exception library
     *  cannot say in Smalltalk.  This system's own numbers -- the reserved
     *  240-255 block, documented in doc/CONCURRENCY.md -- because Squeak
     *  builds Context>>return: out of process machinery this VM does not
     *  have and ported source never names a number for it.
     */
    case 246: return primitive_context_return();
    case 248: return primitive_report_on_standard_error();
    case 247: return primitive_context_resume();

    /*
     *  82: BlockContext>>valueWithArguments:, a Blue Book number that was
     *  declared by the 1983 library and never implemented here.
     */
    case 82: return primitive_value_with_arguments(0);

    /*
     *  201-206 and 221-222: BlockClosure.  Squeak's numbers, kept because
     *  ported Pharo source declares them.  Every one fails harmlessly in a
     *  build with no BlockClosure, because ST_is_block_closure answers no.
     */
    case 201: return primitive_closure_value(0);
    case 202: return primitive_closure_value(1);
    case 203: return primitive_closure_value(2);
    case 204: return primitive_closure_value(3);
    case 205: return primitive_closure_value(4);
    case 206: return primitive_value_with_arguments(1);
    case 221: return primitive_closure_value(0);
    case 222: return primitive_closure_value(1);

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
    if (ST_errors_reported())
        fprintf(stderr, "st80: %s is not a boolean at cycle %llu\n", buf,
                (unsigned long long) st_vm.cycle);
    ST_report_backtrace();
    st_vm.running = 0;
}

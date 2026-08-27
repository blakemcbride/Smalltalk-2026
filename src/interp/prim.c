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
#include "worker.h"
#include "st_port.h"
#include "st_atomic.h"
#include "st_odbc.h"
#include "st_socket.h"
#include "st_crypto.h"
#include "image_compile.h"
#include "bootstrap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <math.h>

/*
 *  <dirent.h> and <unistd.h> are POSIX and Windows has neither.  Everything
 *  this file wants from them is behind the shim further down -- see
 *  "The file system, on two systems" -- and these are the headers each side
 *  needs to build it.
 */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

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

        /*
         *  The upper test is not decoration.  A 64-bit SmallInteger carries
         *  values this uint32_t cannot, and truncating them here does not
         *  fail safely -- it produces a DIFFERENT, in-range index that the
         *  bounds check downstream then approves.  `#(11 22 33) at: 4294967297'
         *  answered 11 rather than raising, and `Array new: 4294967296'
         *  answered an empty Array.  Nothing read out of bounds, so no
         *  sanitizer had anything to say; it was simply the wrong answer.
         */
        if (v < 0)
            return 0;
        /*
         *  Widened rather than cast, and that is not fussiness.  st_int is
         *  int64_t in this memory and INT32_T in the Blue Book one, where
         *  `(st_int) UINT32_MAX' is -1 -- so `v > (st_int) UINT32_MAX' was
         *  true for every non-negative value and every `new:' in the OM=bb
         *  build failed its primitive.  The 1983 traces said so at cycle
         *  125, four hundred and thirty-two lines out.
         */
        if ((uint64_t) v > (uint64_t) UINT32_MAX)
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
    int         weak;
    int         ephemeron;
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
    /*  Bit 12: the collector does not follow this class's indexed fields. */
    shape.weak      = (format >> 12) & 1;
    /*
     *  Bit 16: an ephemeron.  Above the Blue Book's fields rather than
     *  beside them, because 1983 uses bits 13, 14 and 15 and leaves only
     *  bit 12 between them and the instance size -- and weak has that one.
     *  Nothing in a 1983 image sets bit 16, so an old format word reads as
     *  ordinary, which is the requirement.
     */
    shape.ephemeron = (format >> 16) & 1;
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
    /*
     *  A variableSubclass: has named instance variables ahead of its indexed
     *  ones, and the sum is what gets allocated.  Both halves are uint32_t,
     *  so a count near the top of the range wraps: a class with three named
     *  variables asked for `new: 4294967294' answered an object of basicSize
     *  0.  Fail the primitive instead and let new: raise.
     */
    if (count > UINT32_MAX - shape.fixed)
        return 0;
    if (shape.pointers && shape.ephemeron)
        instance = OM_instantiate_ephemeron(cls, shape.fixed + count);
    else if (shape.pointers && shape.weak)
        instance = OM_instantiate_weak(cls, shape.fixed + count, shape.fixed);
    else if (shape.pointers)
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
 *
 *  OM_is_object on both sides is NOT the whole test, and taking it for the
 *  whole test cost an image: nil is OOP 2 and passes it, so
 *  `nil become: Object new' answered normally and every subsequent
 *  `nil printString' said 'an Object'.  The one-way path already refused
 *  exactly this; the memory now asks the same question for a swap, and a
 *  refusal fails the primitive so Object>>become:'s fallback raises.
 */
static int
primitive_become(void)
{
    st_oop  a = ST_stack_value(1);
    st_oop  b = ST_stack_value(0);

    if (!OM_swap_identities(a, b))
        return 0;
    ST_pop_n(1);
    return 1;
}

/*  ----------  Primitive 249: elementsForwardIdentityTo:  ----------
 *
 *  One-way become, in bulk: element i of the receiver is forwarded to
 *  element i of the argument, so every reference to the first becomes a
 *  reference to the second and references to the second are untouched.
 *
 *  249 because that is the number Pharo names for it, and this block of the
 *  primitive table is otherwise this system's own -- taking Pharo's number
 *  for Pharo's operation is what lets ported source say
 *  `<primitive: 249>' and mean it.  Object>>becomeForward: in lib/ is the
 *  one-element case, and MethodDictionary>>grow is why any of this exists:
 *  a method dictionary doubles by building a new one and forwarding.
 *
 *  Fails, changing nothing, if the two are not Arrays of equal size or if
 *  any element cannot be forwarded -- see OM_forward_identity.  A failure
 *  here means the Smalltalk fallback runs, which is what a primitive
 *  failure is for.
 */
static int
primitive_elements_forward_identity(void)
{
    st_oop      from_array = ST_stack_value(1);
    st_oop      to_array   = ST_stack_value(0);
    uint32_t    size;
    uint32_t    i;

    if (!OM_is_object(from_array) || !OM_is_object(to_array))
        return 0;
    if (OM_fetch_class(from_array) != OM_fetch_class(to_array))
        return 0;
    size = OM_fetch_word_length(from_array);
    if (size != OM_fetch_word_length(to_array))
        return 0;
    /*
     *  Every pair is checked before any pair moves.  A bulk become that
     *  forwarded three of five and then failed would leave the image in a
     *  state no caller asked for and none can undo.
     */
    for (i = 0; i < size; ++i) {
        if (!OM_can_forward_identity(OM_fetch_pointer(i, from_array),
                                     OM_fetch_pointer(i, to_array)))
            return 0;
    }
    /*
     *  One sweep of the object table per pair.  The bulk form exists
     *  because Pharo's does; the caller that matters passes one pair, and
     *  folding n pairs into a single sweep would be an optimisation for a
     *  case nothing in this system has yet.
     */
    for (i = 0; i < size; ++i) {
        if (!OM_forward_identity(OM_fetch_pointer(i, from_array),
                                 OM_fetch_pointer(i, to_array)))
            return 0;
    }
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
 *  250: a full collection, on request.
 *
 *  The 1983 library has no way to ask for one -- there is no
 *  garbageCollect anywhere in it -- which among other things means a weak
 *  reference can never be OBSERVED to let go from inside the image, and so
 *  cannot be tested there either.
 *
 *  Squeak's number for this is 130 and it is not available: PosixFile
 *  declares 130 and PosixFileDirectory declares 131 in this very library.
 *  Neither is implemented, so both fail today and their Smalltalk bodies
 *  run; taking the number would make a file method quietly collect garbage
 *  instead, which is the kind of silent difference this system spends most
 *  of its comments avoiding.  250 is in the block reserved for this
 *  system's own primitives.
 */
static int
primitive_full_collect(void)
{
    OM_collect();
    return 1;                       /*  answers the receiver  */
}

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
        /*
         *  And where it came from, when asked.
         *
         *  The message alone is nearly useless for anything the library
         *  raises from a shared place: `unable to grow this collection'
         *  names neither the collection nor the sender, and the same line
         *  can come from a dozen callers.  Off by default because an
         *  expected failure -- the first pass of the class initializers is
         *  full of them -- should not print a page each.
         */
        if (getenv("ST_ERROR_BACKTRACE"))
            ST_report_backtrace();
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
 *  Is `ctx` still on the stack -- that is, reachable from here by senders?
 *
 *  Both of the jumps below need this and neither can do without it.  A
 *  context that has already returned still looks perfectly well formed: it
 *  has a method, a receiver and a program counter, because do_return nils
 *  the fields of the frame it leaves and not of everything that frame
 *  called.  Jumping into one would carry on inside an activation nothing
 *  refers to any more, over a stack that has been reused.  That happens the
 *  moment an exception outlives its handler -- stored in a variable and
 *  resumed later -- which is a mistake worth a message rather than a crash.
 */
/*
 *  The walk has no step limit, and taking one out is what this comment is
 *  about.
 *
 *  It used to give up after 100,000 hops and answer `not live'.  That is a
 *  wrong answer rather than a cheap one, and the shape of what it caused is
 *  worth writing down: an exception raised 100,000 frames down found its
 *  handler, called ContextPart>>return:, had this primitive fail, fell back
 *  on the Smalltalk body -- which signals `return from a method that has
 *  already returned' -- and that new Error searched from the same depth,
 *  found the same handler, and failed here again.  A loop, not a stack
 *  overflow, so nothing grew and nothing reported: the process spun in
 *  silence for ever.  At 99,000 frames the same expression answered in
 *  seventy milliseconds.
 *
 *  The chain being walked ends at nil and is built by this interpreter one
 *  link at a time; the two other walkers in this file --
 *  primitive_find_next_handler and primitive_find_next_unwind -- have never
 *  had a limit, for the same reason.  A guard here bought nothing that they
 *  do not already do without and cost a hang at a depth a program can
 *  reach.  ST_MAX_CALL_DEPTH is where a stack that is too deep is stopped,
 *  before it can be walked at all.
 */
static int
context_is_live(st_oop ctx)
{
    st_oop  scan = st_vm.active_context;

    while (OM_is_present(scan)) {
        if (scan == ctx)
            return 1;
        scan = OM_fetch_pointer(ST_CTX_SENDER, scan);
    }
    return 0;
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
    if (!context_is_live(ctx))
        return 0;
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

    if (!is_a_context(ctx) || !context_is_live(ctx))
        return 0;
    ST_pop_n(2);
    ST_resume_at(value, ctx);
    return 1;
}

/*
 *  195 and 197: walking the sender chain, in C.
 *
 *  Squeak's numbers, and Squeak's reason for having them: both are pure
 *  OPTIMISATIONS with working Smalltalk bodies behind them, so a build
 *  without them is slower and not different.  Signalling walked the chain
 *  a send at a time, and each step asked a method for its primitive number
 *  through three more sends -- fine at this scale and the obvious thing to
 *  stop doing.
 *
 *  The walk is all they do.  Whether a handler HANDLES this exception is a
 *  Smalltalk question -- it sends #handles: -- and so is whether it is
 *  currently disabled, so the caller loops over these.
 */
static int
primitive_find_next_handler(void)
{
    st_oop  ctx = ST_stack_value(0);
    st_oop  scan;

    if (!is_a_context(ctx))
        return 0;
    scan = OM_fetch_pointer(ST_CTX_SENDER, ctx);
    while (OM_is_present(scan)) {
        if (ST_context_primitive(scan) == 199) {
            ST_pop_n(1);
            ST_push(scan);
            return 1;
        }
        scan = OM_fetch_pointer(ST_CTX_SENDER, scan);
    }
    ST_pop_n(1);
    ST_push(ST_NIL);
    return 1;
}

static int
primitive_find_next_unwind_up_to(void)
{
    st_oop  ctx   = ST_stack_value(1);
    st_oop  limit = ST_stack_value(0);
    st_oop  scan;

    if (!is_a_context(ctx))
        return 0;
    scan = OM_fetch_pointer(ST_CTX_SENDER, ctx);
    while (OM_is_present(scan) && scan != limit) {
        if (ST_context_primitive(scan) == 198) {
            ST_pop_n(2);
            ST_push(scan);
            return 1;
        }
        scan = OM_fetch_pointer(ST_CTX_SENDER, scan);
    }
    ST_pop_n(2);
    ST_push(ST_NIL);
    return 1;
}

/*
 *  251: ContextPart>>restartAndJump.  Run this activation again from the
 *  top, with the arguments it already has.
 *
 *  Not spelled "restart": MethodContext>>restart is a 1983 method that
 *  resets a frame without continuing it, and taking that name would shadow
 *  a Debugger operation with something that does more.
 */
static int
primitive_context_restart(void)
{
    st_oop  ctx = ST_stack_value(0);

    if (!is_a_context(ctx))
        return 0;
    if (OM_fetch_class(ctx) != ST_CLASS_METHOD_CONTEXT)
        return 0;                   /*  a block frame has no pattern to redo */
    if (!context_is_live(ctx))
        return 0;
    ST_pop_n(1);
    ST_restart_at(ctx);
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
    /*  Before it lands: the flash it may be about to undo.  */
    GFX_present_if_undoing(&blit);
    GFX_copy_bits(&blit);
    /*
     *  If that drew on the screen, remember the region so the window is
     *  refreshed.  The clipped rectangle is the one actually touched.
     */
    if (blit.dest.oop == GFX_display_form()) {
        GFX_note_blit(&blit);
        GFX_damage(blit.damage_x, blit.damage_y, blit.damage_w, blit.damage_h);
    }
    return 1;                   /*  answers the receiver  */
}

/*  ----------  Files, primitives 128 and 130 to 133  ----------
 *
 *  sources/Files-Posix declares five primitive numbers and this VM
 *  implemented none of them, so every file operation in the system failed.
 *  The File List showed it best: it opened, drew its three panes, and could
 *  never list anything, because FileDirectory class>>
 *  directoryFromName:setFileName: is one line -- `directory _ Disk' -- and
 *  Disk was nil.  Setting Disk without these made it worse, not better:
 *  fileNames invoked a primitive that did not exist, the method body is
 *  empty so the failure answered self, and PosixFileDirectory>>do: then
 *  recursed into itself.  A notifier became a hang.
 *
 *  Pages are 512 bytes -- FilePage>>dataSize, with no header or trailer --
 *  and numbered from ONE, so page n is the bytes at (n-1)*512.  A page
 *  carries its own byte count, and a short page is how the file says where
 *  it ends.
 *
 *  Field numbers are the instance variables of File and FilePage, which are
 *  frozen sources and so are as good as a struct:
 *
 *      File        fileDirectory fileName pageCache serialNumber
 *                  lastPageNumber binary readWrite error
 *      PosixFile   ... then fd cachedPageSize
 *      FilePage    file page binary
 *      PosixFilePage ... then pageNumber bytesInPage
 */
/*
 *  ----------  The file system, on two systems  ----------
 *
 *  These primitives are the 1983 File and FilePage protocol, and 1983 wrote
 *  it against a descriptor: open, close, size, read a page, write a page,
 *  truncate after a page, and list a directory.  POSIX answers all seven
 *  directly.  Windows answers none of them under those names -- there is no
 *  <dirent.h>, no <unistd.h>, and no pread or pwrite at all.
 *
 *  So the seven live here once each, and the primitive bodies below read the
 *  same on both platforms.  Three of the translations are worth stating,
 *  because each is a bug if it is done the obvious way instead:
 *
 *  _O_BINARY, ALWAYS.  MSVC's _open defaults to TEXT mode, which turns \n
 *  into \r\n on the way out, turns it back on the way in, and stops reading
 *  at the first 0x1A.  These primitives carry image pages -- snapshot.im
 *  goes through them -- so text mode is not a formatting difference, it is
 *  a corrupted image that reads back shorter than it was written.
 *
 *  POSITIONAL READS STAY POSITIONAL.  pread and pwrite take an offset and
 *  do not disturb the file pointer, which is what lets two workers hold the
 *  same file without a lock between them.  Seeking and then reading is two
 *  operations and a race.  Win32 has the same thing without the same name:
 *  ReadFile and WriteFile take the offset in an OVERLAPPED, and on a handle
 *  that was not opened FILE_FLAG_OVERLAPPED they complete synchronously at
 *  that offset.  That is the translation, and _lseeki64 plus _read is not.
 *
 *  SIZE COMES FROM THE DESCRIPTOR, IN 64 BITS.  _filelengthi64 rather than
 *  fstat, which spares us MSVC's struct _stat / struct _stat64 question and
 *  answers past 2 GB on both.
 *
 *  The directory walk is FindFirstFileA, the same one src/boot/profile.c
 *  already carries for #packages.  Two copies, and this is the second; if a
 *  third is ever wanted, that is the moment it belongs in src/port.
 */

#define ST_OPEN_RDONLY      0
#define ST_OPEN_RDWR        1
#define ST_OPEN_RDWR_CREATE 2

#if defined(_WIN32)

typedef struct {
    HANDLE              h;
    WIN32_FIND_DATAA    data;
    int                 pending;    /*  FindFirstFile already fetched one  */
} st_dir;

static int
st_file_open(const char *path, int mode)
{
    switch (mode) {
    case ST_OPEN_RDONLY:
        return _open(path, _O_RDONLY | _O_BINARY);
    case ST_OPEN_RDWR:
        return _open(path, _O_RDWR | _O_BINARY);
    default:
        return _open(path, _O_RDWR | _O_CREAT | _O_BINARY,
                     _S_IREAD | _S_IWRITE);
    }
}

static void st_file_close(int fd)    { _close(fd); }
static int64_t st_file_size(int fd)  { return (int64_t) _filelengthi64(fd); }

/*  Seconds since the Unix epoch, or -1.  _fstat64 for a 64-bit time.  */
static int64_t
st_file_mtime(int fd)
{
    struct _stat64  st;

    if (_fstat64(fd, &st) != 0)
        return -1;
    return (int64_t) st.st_mtime;
}

static int
st_path_is_directory(const char *path)
{
    DWORD   attr = GetFileAttributesA(path);

    return attr != INVALID_FILE_ATTRIBUTES
        && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int
st_file_truncate(int fd, int64_t end)
{
    /*  _chsize_s answers an errno_t, zero on success, not -1 on failure.  */
    return _chsize_s(fd, end) == 0 ? 0 : -1;
}

/*  The offset goes in the OVERLAPPED; nothing here touches the pointer.  */
static void
win_overlapped(OVERLAPPED *ov, int64_t off)
{
    memset(ov, 0, sizeof *ov);
    ov->Offset     = (DWORD) ((uint64_t) off & 0xFFFFFFFFu);
    ov->OffsetHigh = (DWORD) (((uint64_t) off >> 32) & 0xFFFFFFFFu);
}

static int64_t
st_file_pread(int fd, void *buf, size_t count, int64_t off)
{
    HANDLE      h = (HANDLE) _get_osfhandle(fd);
    OVERLAPPED  ov;
    DWORD       got = 0;

    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    win_overlapped(&ov, off);
    if (!ReadFile(h, buf, (DWORD) count, &got, &ov)) {
        /*  Reading at or past the end is an end, not a fault -- which is
         *  what a POSIX pread answers 0 for.  */
        if (GetLastError() == ERROR_HANDLE_EOF)
            return 0;
        errno = EIO;
        return -1;
    }
    return (int64_t) got;
}

static int64_t
st_file_pwrite(int fd, const void *buf, size_t count, int64_t off)
{
    HANDLE      h = (HANDLE) _get_osfhandle(fd);
    OVERLAPPED  ov;
    DWORD       put = 0;

    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    win_overlapped(&ov, off);
    if (!WriteFile(h, buf, (DWORD) count, &put, &ov)) {
        errno = EIO;
        return -1;
    }
    return (int64_t) put;
}

static int
st_dir_open(st_dir *d, const char *path)
{
    char    pattern[1024];

    snprintf(pattern, sizeof pattern, "%s\\*", path);
    d->h = FindFirstFileA(pattern, &d->data);
    if (d->h == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return 0;
    }
    d->pending = 1;
    return 1;
}

static const char *
st_dir_next(st_dir *d)
{
    if (d->pending) {
        d->pending = 0;
        return d->data.cFileName;
    }
    if (!FindNextFileA(d->h, &d->data))
        return NULL;
    return d->data.cFileName;
}

static void
st_dir_close(st_dir *d)
{
    if (d->h != INVALID_HANDLE_VALUE)
        FindClose(d->h);
}

#else   /*  POSIX  */

typedef struct { DIR *d; } st_dir;

static int
st_file_open(const char *path, int mode)
{
    int         fd;
    struct stat st;

    switch (mode) {
    case ST_OPEN_RDONLY:    fd = open(path, O_RDONLY);              break;
    case ST_OPEN_RDWR:      fd = open(path, O_RDWR);                break;
    default:                fd = open(path, O_RDWR | O_CREAT, 0666); break;
    }
    /*
     *  A directory is not a file, and POSIX will not say so until later.
     *
     *  open(2) on a directory SUCCEEDS read-only -- it is how one gets a
     *  descriptor to fstat or fdopendir -- so the image was handed a live
     *  descriptor for `doc' and did not find out until pread answered
     *  EISDIR, four frames further on:
     *
     *      PosixFile(Object)>>error:  ...  PosixFile>>read:
     *      doc read:, Is a directory
     *
     *  Windows never had the fault, because _open refuses a directory
     *  outright -- so the two platforms disagreed about where the same
     *  click failed.  Refusing here is what makes them agree, and it is the
     *  honest answer to the question this function was asked: there is no
     *  file of that name to open.
     */
    if (fd >= 0 && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        close(fd);
        errno = EISDIR;
        return -1;
    }
    return fd;
}

static void st_file_close(int fd)   { close(fd); }

static int
st_path_is_directory(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int64_t
st_file_size(int fd)
{
    struct stat st;

    if (fstat(fd, &st) != 0)
        return -1;
    return (int64_t) st.st_size;
}

/*  Seconds since the Unix epoch, or -1.  */
static int64_t
st_file_mtime(int fd)
{
    struct stat st;

    if (fstat(fd, &st) != 0)
        return -1;
    return (int64_t) st.st_mtime;
}

static int
st_file_truncate(int fd, int64_t end)
{
    return ftruncate(fd, (off_t) end) == 0 ? 0 : -1;
}

static int64_t
st_file_pread(int fd, void *buf, size_t count, int64_t off)
{
    return (int64_t) pread(fd, buf, count, (off_t) off);
}

static int64_t
st_file_pwrite(int fd, const void *buf, size_t count, int64_t off)
{
    return (int64_t) pwrite(fd, buf, count, (off_t) off);
}

static int
st_dir_open(st_dir *d, const char *path)
{
    d->d = opendir(path);
    return d->d != NULL;
}

static const char *
st_dir_next(st_dir *d)
{
    struct dirent *entry = readdir(d->d);

    return entry ? entry->d_name : NULL;
}

static void
st_dir_close(st_dir *d)
{
    closedir(d->d);
}

#endif  /*  _WIN32  */

#define FILE_NAME_FIELD         1
#define POSIX_FD_FIELD          8
#define PAGE_BUFFER_FIELD       1
#define PAGE_NUMBER_FIELD       3
#define PAGE_BYTES_FIELD        4
#define POSIX_PAGE_SIZE         512

static int  posix_errno;

/*
 *  Which descriptors are OURS, in THIS process.
 *
 *  A File keeps its descriptor in an instance variable, as a SmallInteger,
 *  and an instance variable is part of the object memory -- so a snapshot
 *  writes it out and -run reads it back into a process where that number
 *  means something else entirely.  The changes file came back holding fd 3
 *  and writing to it answered `Illegal seek', which is what you get for
 *  seeking on whatever fd 3 happens to be next time.
 *
 *  So a descriptor is only believed if this process opened it.  A fresh
 *  process has none marked, every descriptor an image carries is therefore
 *  stale, and the file is reopened by name the first time it is used.  This
 *  is the VM's half of what 1983 called external references: the image
 *  cannot be expected to know its file handles died.
 */
#define POSIX_MAX_FD    4096
/*
 *  Atomic, not because two workers ever own one descriptor -- the kernel
 *  hands each number out once -- but because the number comes back: a
 *  worker closing descriptor 5 clears its byte, the kernel gives 5 to the
 *  next open on another worker, and that worker sets the same byte with
 *  nothing but the kernel's ordering between the two.  ThreadSanitizer
 *  cannot see that ordering and reported it, correctly, as a race; the
 *  atomic says what the ordering already guaranteed.
 */
static st_atomic_int    fd_is_ours[POSIX_MAX_FD];

/*  A Smalltalk String from C, and the reverse.  */
static st_oop
string_from_c(const char *text, size_t n)
{
    st_oop      s = OM_instantiate_bytes(ST_CLASS_STRING, (uint32_t) n);
    size_t      i;

    if (!OM_is_present(s))
        return ST_OOP_INVALID;
    for (i = 0; i < n; ++i)
        OM_store_byte((uint32_t) i, s, (uint8_t) text[i]);
    return s;
}

static int
c_from_string(st_oop s, char *out, size_t max)
{
    uint32_t    n;
    uint32_t    i;

    if (!OM_is_object(s) || OM_pointer_bit(s))
        return 0;
    n = OM_fetch_byte_length(s);
    if (n >= max)
        return 0;
    for (i = 0; i < n; ++i)
        out[i] = (char) OM_fetch_byte(i, s);
    out[n] = '\0';
    return 1;
}

/*
 *  The file's descriptor, kept as a SmallInteger in the fd field.  A file
 *  that was never opened has nil there, which is not an error: close sends
 *  here with fd nil and expects to be told so.
 */
static int
posix_fd_of(st_oop file)
{
    st_oop  fd;

    if (!OM_is_object(file) || !OM_pointer_bit(file)
     || OM_fetch_word_length(file) <= POSIX_FD_FIELD)
        return -1;
    fd = OM_fetch_pointer(POSIX_FD_FIELD, file);
    if (!OM_is_int(fd))
        return -1;
    {
        long    n = (long) OM_int_value(fd);

        if (n < 0 || n >= POSIX_MAX_FD || !ST_load_relaxed(&fd_is_ours[n]))
            return -1;                  /*  not ours: from another life  */
        return (int) n;
    }
}

/*  Remember, or forget, a descriptor of our own.  */
static int
posix_own(int fd)
{
    if (fd < 0)
        return fd;
    if (fd >= POSIX_MAX_FD) {           /*  further than we can vouch for  */
        st_file_close(fd);
        posix_errno = EMFILE;
        return -1;
    }
    ST_store_relaxed(&fd_is_ours[fd], 1);
    return fd;
}

static void
posix_disown(int fd)
{
    if (fd >= 0 && fd < POSIX_MAX_FD)
        ST_store_relaxed(&fd_is_ours[fd], 0);
}

/*
 *  A descriptor for this file, opening it if nobody has yet.
 *
 *  1983 does not open before it asks.  Disk findKey: answers a File that has
 *  never been opened, and File>>size then goes straight to
 *  findLastPageNumber, which asks sizeOnDisk and does arithmetic on the
 *  answer.  Answering false there -- "no descriptor" -- means `false + 511',
 *  a doesNotUnderstand whose reporting is far more expensive than the
 *  question, which is why a one-page file took more than two hundred million
 *  bytecodes to fail to measure.
 *
 *  A file's size and contents are properties of its NAME, so the name is
 *  enough to answer with.  Opening on demand and caching the descriptor
 *  keeps every path working whether or not the image opened first.
 */
static int
posix_fd_for(st_oop file, int for_writing)
{
    int     fd = posix_fd_of(file);
    char    path[1024];

    if (fd >= 0)
        return fd;
    if (!OM_is_object(file) || !OM_pointer_bit(file)
     || OM_fetch_word_length(file) <= POSIX_FD_FIELD)
        return -1;
    if (!c_from_string(OM_fetch_pointer(FILE_NAME_FIELD, file),
                       path, sizeof path))
        return -1;
    fd = st_file_open(path, for_writing ? ST_OPEN_RDWR_CREATE : ST_OPEN_RDWR);
    if (fd < 0 && !for_writing)
        fd = st_file_open(path, ST_OPEN_RDONLY);  /*  readable is enough  */
    if (fd < 0) {
        posix_errno = errno;
        return -1;
    }
    if (posix_own(fd) < 0)
        return -1;
    OM_store_pointer(POSIX_FD_FIELD, file, OM_int_oop((st_int) fd));
    return fd;
}

/*
 *  primitive 130 -- PosixFile>>doPrimCommand:name:page:
 *
 *  The commands are the ones PosixFile sends: 0 read, 1 write, 2 truncate,
 *  3 size, 4 open, 5 close.  Answering false is how a command reports
 *  failure -- doCommand:name:page:error: tests for it and only then raises
 *  an error -- so a read past the end answers false and read: answers nil,
 *  which is exactly how a stream finds the end of a file.
 *
 *  6 is this system's: the time the file was last modified, in seconds on
 *  the Smalltalk epoch so that it compares with what primitive 98 answers.
 *  A server that loads a service from a Tonel file on first use reloads it
 *  when the file changes, and "has it changed" is this number.
 */
static int
primitive_file_command(void)
{
    st_oop      page = ST_stack_value(0);
    st_oop      name = ST_stack_value(1);
    st_oop      cmd  = ST_stack_value(2);
    st_oop      file = ST_stack_value(3);
    long        command;
    int         fd;
    st_oop      answer = ST_FALSE;

    if (!OM_is_int(cmd))
        return 0;
    command = (long) OM_int_value(cmd);
    fd = (command == 1 || command == 2) ? posix_fd_for(file, 1)
       : (command == 0 || command == 3 || command == 6) ? posix_fd_for(file, 0)
       : posix_fd_of(file);

    switch (command) {
    case 4: {                                   /*  open  */
        char    path[1024];

        if (!c_from_string(name, path, sizeof path))
            return 0;
        /*
         *  CREATING, because by the time the image opens a file it has
         *  already decided the file should exist: FileStream newFileNamed:
         *  and the snapshot writer both reach here through findOrAddKey:,
         *  and a reader has asked includesKey: first.  Refusing to create
         *  meant `snapshot.im open: No such file or directory' -- the file
         *  being made could never be made.
         */
        fd = st_file_open(path, ST_OPEN_RDWR_CREATE);
        if (fd < 0)
            fd = st_file_open(path, ST_OPEN_RDONLY);  /*  readable is enough  */
        if (fd < 0 || posix_own(fd) < 0) {
            if (fd >= 0)
                answer = ST_FALSE;
            else
                posix_errno = errno;
            answer = ST_FALSE;
            break;
        }
        OM_store_pointer(POSIX_FD_FIELD, file, OM_int_oop((st_int) fd));
        answer = ST_TRUE;
        break;
    }
    case 5:                                     /*  close  */
        if (fd >= 0) {
            st_file_close(fd);
            posix_disown(fd);
            OM_store_pointer(POSIX_FD_FIELD, file, ST_NIL);
        }
        answer = ST_TRUE;
        break;

    case 3: {                                   /*  size on disk  */
        int64_t size = fd < 0 ? -1 : st_file_size(fd);

        if (size < 0) {
            posix_errno = errno;
            answer = ST_FALSE;
            break;
        }
        ST_pop_n(4);
        ST_push(OM_int_oop((st_int) size));
        return 1;
    }
    case 6: {                                   /*  modification time  */
        int64_t when = fd < 0 ? -1 : st_file_mtime(fd);

        if (when < 0) {
            posix_errno = errno;
            answer = ST_FALSE;
            break;
        }
        when += ST_EPOCH_OFFSET_SEC;
#ifndef ST_OM_MT
        /*  Fifteen bits cannot hold a date; the Blue Book memory has none.  */
        answer = ST_FALSE;
        break;
#else
        ST_pop_n(4);
        ST_push(OM_int_oop((st_int) when));
        return 1;
#endif
    }
    case 0: {                                   /*  read a page  */
        st_oop      buffer;
        st_oop      number;
        long        n;
        int64_t     got;
        uint32_t    room;
        uint32_t    i;
        char        bytes[POSIX_PAGE_SIZE];

        if (fd < 0 || !OM_is_object(page) || !OM_pointer_bit(page)
         || OM_fetch_word_length(page) <= PAGE_BYTES_FIELD)
            return 0;
        number = OM_fetch_pointer(PAGE_NUMBER_FIELD, page);
        buffer = OM_fetch_pointer(PAGE_BUFFER_FIELD, page);
        if (!OM_is_int(number) || !OM_is_object(buffer)
         || OM_pointer_bit(buffer))
            return 0;
        n = (long) OM_int_value(number);
        if (n < 1)
            n = 1;
        got = st_file_pread(fd, bytes, sizeof bytes,
                            (int64_t) (n - 1) * POSIX_PAGE_SIZE);
        if (got < 0) {
            posix_errno = errno;
            answer = ST_FALSE;
            break;
        }
        /*
         *  Page one always exists, even in an empty file.
         *
         *  findLastPageNumber answers `... max: 1', so 1983 believes every
         *  file has at least one page, and File>>readOrAdd: reads page one
         *  rather than making it.  Answering false there -- "no such page"
         *  -- gave FileStream>>on: a nil page, and a newly created file
         *  could not be opened to be written to.  Past the last page is a
         *  real end and still answers false.
         */
        if (got == 0 && n > 1) {
            answer = ST_FALSE;                  /*  the end of the file  */
            break;
        }
        room = OM_fetch_byte_length(buffer);
        if ((uint32_t) got < room)
            room = (uint32_t) got;
        for (i = 0; i < room; ++i)
            OM_store_byte(i, buffer, (uint8_t) bytes[i]);
        OM_store_pointer(PAGE_BYTES_FIELD, page, OM_int_oop((st_int) room));
        answer = ST_TRUE;
        break;
    }
    case 1: {                                   /*  write a page  */
        st_oop      buffer;
        st_oop      number;
        st_oop      count;
        long        n;
        uint32_t    len;
        uint32_t    i;
        char        bytes[POSIX_PAGE_SIZE];

        if (fd < 0 || !OM_is_object(page) || !OM_pointer_bit(page)
         || OM_fetch_word_length(page) <= PAGE_BYTES_FIELD)
            return 0;
        number = OM_fetch_pointer(PAGE_NUMBER_FIELD, page);
        buffer = OM_fetch_pointer(PAGE_BUFFER_FIELD, page);
        count  = OM_fetch_pointer(PAGE_BYTES_FIELD, page);
        if (!OM_is_int(number) || !OM_is_object(buffer)
         || OM_pointer_bit(buffer))
            return 0;
        n   = (long) OM_int_value(number);
        len = OM_is_int(count) ? (uint32_t) OM_int_value(count)
                               : OM_fetch_byte_length(buffer);
        if (len > sizeof bytes)
            len = sizeof bytes;
        for (i = 0; i < len; ++i)
            bytes[i] = (char) OM_fetch_byte(i, buffer);
        if (n < 1)
            n = 1;
        if (st_file_pwrite(fd, bytes, len,
                           (int64_t) (n - 1) * POSIX_PAGE_SIZE)
            != (int64_t) len) {
            posix_errno = errno;
            answer = ST_FALSE;
            break;
        }
        answer = ST_TRUE;
        break;
    }
    case 2: {                                   /*  truncate after a page  */
        /*
         *  TO THE END OF THE DATA, not to the end of the page.
         *
         *  This used to truncate at `pageNumber * POSIX_PAGE_SIZE', which is
         *  where the last page ENDS rather than where its data does, so
         *  every file written through a stream was rounded up to a multiple
         *  of 512 and the difference arrived as trailing zero bytes.  A
         *  filed-out class came off the disk with up to 511 nulls stuck on
         *  the end of it, which an editor shows as ^@^@^@..., git calls a
         *  binary file, and a re-read has to skip past.
         *
         *  The right length is already known and is already used one case
         *  up: FileStream>>shorten sets `page dataEnd: position', which is
         *  PosixFilePage's bytesInPage, and the write above puts exactly
         *  that many bytes at (pageNumber - 1) * POSIX_PAGE_SIZE.  So the
         *  end of the file is where that write finished, and the two
         *  computations now agree by construction.
         *
         *  A nil page still means length zero -- File>>endFile: documents
         *  passing nil as "delete all of the receiver's pages", and that is
         *  what an empty file has to be able to become.
         */
        st_oop  number = OM_is_object(page) && OM_pointer_bit(page)
                       ? OM_fetch_pointer(PAGE_NUMBER_FIELD, page) : ST_NIL;
        st_oop  count  = OM_is_object(page) && OM_pointer_bit(page)
                       && OM_fetch_word_length(page) > PAGE_BYTES_FIELD
                       ? OM_fetch_pointer(PAGE_BYTES_FIELD, page) : ST_NIL;
        int64_t end = 0;

        if (fd < 0)
            return 0;
        if (OM_is_int(number)) {
            long    n = (long) OM_int_value(number);
            int64_t len = OM_is_int(count) ? (int64_t) OM_int_value(count)
                                           : POSIX_PAGE_SIZE;

            if (n < 1)
                n = 1;
            if (len < 0)
                len = 0;
            if (len > POSIX_PAGE_SIZE)
                len = POSIX_PAGE_SIZE;
            end = (int64_t) (n - 1) * POSIX_PAGE_SIZE + len;
        }
        if (st_file_truncate(fd, end) != 0) {
            posix_errno = errno;
            answer = ST_FALSE;
            break;
        }
        answer = ST_TRUE;
        break;
    }
    default:
        return 0;
    }
    ST_pop_n(4);
    ST_push(answer);
    return 1;
}

/*
 *  primitive 131 -- PosixFileDirectory>>doPrimitive:arg1:arg2:
 *
 *  1 removes a file, 2 renames one, 3 answers the names in a directory,
 *  4 answers whether a name is a directory.
 *  3 walks the directory the system was started in unless it is told
 *  otherwise: PosixFileDirectory new sets its directoryName to the empty
 *  string, and the empty string is where a relative name resolves.
 *
 *  4 is not 1983's.  1983's file systems had no directory inside a
 *  directory -- FileDirectory class>>directoryFromName:setFileName: answers
 *  `Disk' whatever it is handed, and means it -- so the protocol has no way
 *  to ask, and the walk in 3 reports a subdirectory as a name like any
 *  other.  A name that cannot be opened is worth listing and is not worth
 *  reading, and this is what lets the image tell the two apart.
 */
static int
primitive_directory_command(void)
{
    st_oop      arg2 = ST_stack_value(0);
    st_oop      arg1 = ST_stack_value(1);
    st_oop      code = ST_stack_value(2);
    long        what;
    st_oop      answer = ST_FALSE;
    char        a[1024];
    char        b[1024];

    if (!OM_is_int(code))
        return 0;
    what = (long) OM_int_value(code);

    switch (what) {
    case 1:                                     /*  remove  */
        if (!c_from_string(arg1, a, sizeof a))
            return 0;
        answer = remove(a) == 0 ? ST_TRUE : ST_FALSE;
        if (answer == ST_FALSE)
            posix_errno = errno;
        break;

    case 2:                                     /*  rename arg2 to arg1  */
        if (!c_from_string(arg1, b, sizeof b)
         || !OM_is_object(arg2) || !OM_pointer_bit(arg2)
         || OM_fetch_word_length(arg2) <= FILE_NAME_FIELD
         || !c_from_string(OM_fetch_pointer(FILE_NAME_FIELD, arg2),
                           a, sizeof a))
            return 0;
        answer = rename(a, b) == 0 ? ST_TRUE : ST_FALSE;
        if (answer == ST_FALSE)
            posix_errno = errno;
        break;

    case 3: {                                   /*  the names  */
        st_dir          dir;
        const char     *name;
        char           *names[4096];
        uint32_t        count = 0;
        uint32_t        i;
        st_oop          array;
        const char     *path = ".";
        int             failed = 0;

        if (OM_is_present(arg1)) {
            if (!c_from_string(arg1, a, sizeof a))
                return 0;
            path = a;
        }
        if (!st_dir_open(&dir, path)) {
            posix_errno = errno;
            return 0;
        }
        /*
         *  The names are copied in C first, and become Strings only once
         *  there is an Array on the stack to hold each one the moment it is
         *  made.  The first version made every String first and the Array
         *  last, holding the Strings in a C array with a count of zero; an
         *  allocation can run a full collection, which frees whatever no
         *  root reaches, and a C array is not a root.  The REST server
         *  lists its back-end directory on every request, and on thirty-one
         *  workers with the collector forced along a listing came back with
         *  a name that was no longer an object.
         */
        while ((name = st_dir_next(&dir)) != NULL && count < 4096) {
            size_t  n;

            if (name[0] == '.')                 /*  no dot files, no . or ..  */
                continue;
            n = strlen(name);
            names[count] = malloc(n + 1);
            if (!names[count])
                break;
            memcpy(names[count], name, n + 1);
            ++count;
        }
        st_dir_close(&dir);
        array = OM_instantiate_pointers(ST_CLASS_ARRAY, count);
        if (!OM_is_present(array))
            failed = 1;
        else {
            ST_push(array);
            for (i = 0; i < count; ++i) {
                st_oop  one = string_from_c(names[i], strlen(names[i]));

                if (!OM_is_present(one)) {
                    failed = 1;
                    break;
                }
                OM_store_pointer(i, array, one);
            }
            ST_pop_n(1);
        }
        for (i = 0; i < count; ++i)
            free(names[i]);
        if (failed)
            return 0;
        ST_pop_n(3);
        ST_push(array);
        return 1;
    }
    case 4:                                     /*  is arg1 a directory  */
        if (!c_from_string(arg1, a, sizeof a))
            return 0;
        answer = st_path_is_directory(a) ? ST_TRUE : ST_FALSE;
        break;

    default:
        return 0;
    }
    ST_pop_n(3);
    ST_push(answer);
    return 1;
}

/*
 *  primitives 128 and 97 -- beSnapshotFile, and the snapshot itself
 *
 *  SystemDictionary>>snapshotAs: marks a file with beSnapshotFile and then
 *  calls snapshotPrimitive, which is 97, and the primitive is expected to
 *  write the whole object memory into the marked file.  So 128 is not the
 *  no-op it looks like: it is how 97 learns where to write, and the two are
 *  one mechanism split across two sends.
 *
 *  The writer is the same OM_image_save the -o option uses, so an image
 *  saved from inside the running system is the same file a bootstrap would
 *  have written, and -run reads it back.
 *
 *  Answering nil is the success report snapshotAs: wants: it tests
 *  `self snapshotPrimitive isNil' to tell "I have just been written" from "I
 *  have just been resumed".  Failing the primitive outright is worse than
 *  useless here -- the fallback is `self primitiveFailed', and reporting
 *  that costs more than the snapshot would have.
 */
static char     snapshot_path[1024];

static int
primitive_be_snapshot_file(void)
{
    st_oop  file = ST_stack_top();
    st_oop  name;

    if (!OM_is_object(file) || !OM_pointer_bit(file)
     || OM_fetch_word_length(file) <= FILE_NAME_FIELD)
        return 0;
    name = OM_fetch_pointer(FILE_NAME_FIELD, file);
    if (!c_from_string(name, snapshot_path, sizeof snapshot_path))
        return 0;
    return 1;                           /*  the receiver is the answer  */
}

static int
primitive_snapshot(void)
{
#ifndef ST_OM_MT
    /*
     *  The Blue Book memory has no image writer -- OM_image_save is the
     *  64-bit one's -- and it never needs one: it exists to LOAD the 1983
     *  Xerox image and reproduce its traces.  Failing the primitive is the
     *  honest answer, and snapshotPrimitive's own fallback reports it.
     */
    return 0;
#else
    char    err[256];

    if (snapshot_path[0] == '\0')
        return 0;                       /*  nobody said where  */
    /*
     *  Two things before a byte is written, and the image is unusable
     *  without either.
     *
     *  THE REGISTERS ARE IN THE INTERPRETER, NOT IN THE CONTEXT.  st_vm
     *  holds the instruction and stack pointers while a process runs, and
     *  the context's own copies are whatever they were when the scheduler
     *  last parked it.  Snapshotting without writing them back saves a
     *  context that says the wrong place, and -run resumes there: it
     *  executes the middle of a bytecode, or the literal frame, and dies in
     *  lookup_method on a selector oop of two hundred million.  Every other
     *  caller that stops a process does this first -- worker.c and
     *  st_sched.c both do -- and this one is a process stopping itself.
     *
     *  And storing them is only half of it: -run resumes from the active
     *  PROCESS's suspendedContext, not from whatever context the VM last
     *  had.  Writing the registers into the context while leaving the
     *  process pointing at the one it was parked in saves a coherent
     *  context that nothing will run and a stale one that will.  Blake's
     *  image resumed at instruction pointer 9 of a method whose bytecodes
     *  begin at byte 40 -- executing its own literal frame, where the low
     *  byte of literal 0 happens to be 0xF6, "send literal 6 with two
     *  arguments", in a method that has four.  That is the whole of the
     *  segfault: a selector read from past the end of the frame.
     *
     *  SCHED_suspend_active does both, in this order, and has since the
     *  race it documents.  A process snapshotting itself is a process
     *  parking itself.
     *
     *  The collection that goes with it is in OM_image_save, so that every
     *  caller gets it and not only this one.
     */
    ST_store_active_context();
    {
        st_oop  self = SCHED_active_process();

        if (OM_is_object(self))
            OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, self,
                             st_vm.active_context);
    }
    if (OM_image_save(snapshot_path, err, sizeof err) != 0) {
        fprintf(stderr, "st80: snapshot to %s failed: %s\n",
                snapshot_path, err);
        return 0;
    }
    fprintf(stderr, "st80: wrote %s\n", snapshot_path);
    ST_pop_n(1);
    ST_push(ST_NIL);                    /*  "just written", not "resumed"  */
    return 1;
#endif
}

/*
 *  primitives 132 and 133 -- PosixFile>>lastError and >>errorString:
 *
 *  132 is also this VM's instVarsInclude:, which is not a clash that can be
 *  renumbered: sources/ is frozen and the Xerox image already uses 132 for
 *  the other one.  They are told apart by ARITY -- lastError takes no
 *  argument and instVarsInclude: takes one -- which is exact rather than a
 *  guess about the receiver.
 */
static int
primitive_last_error(void)
{
    ST_pop_n(0);
    ST_pop_n(1);
    ST_push(OM_int_oop((st_int) posix_errno));
    return 1;
}

static int
primitive_error_string(void)
{
    st_oop      code = ST_stack_value(0);
    const char *text;
    st_oop      s;

    if (!OM_is_int(code))
        return 0;
    text = strerror((int) OM_int_value(code));
    s = string_from_c(text, strlen(text));
    if (!OM_is_present(s))
        return 0;
    ST_pop_n(2);
    ST_push(s);
    return 1;
}

/*
 *  The host's own line ending, primitive 254.
 *
 *  A Smalltalk-80 line ends with Character cr, and that is not a convention
 *  this system is free to change: CompositionScanner stops on carriage
 *  return and on nothing else, so a linefeed inside a String is a character
 *  with no glyph and no line break.  Paragraph, CharacterScanner and
 *  String>>lines all agree with it.  The image keeps carriage returns and
 *  there is no argument about it.
 *
 *  A FILE is another matter.  The Alto wrote carriage returns because the
 *  Alto did, and nothing anybody now runs reads them: a filed-out class
 *  arrives in an editor, in git and in a diff as one line thousands of
 *  characters long.  So the ending is translated at the file's edge -- in on
 *  the way in, out on the way out -- and this is the only part of that which
 *  C has to answer, because the image has no way to ask what it is running
 *  on.
 *
 *  It answers a String rather than a flag, so the caller can write it
 *  without knowing how long it is -- which is the whole of the difference
 *  between the two platforms this builds for.
 */
static int
primitive_native_line_end(void)
{
#if defined(_WIN32)
    static const char   ending[] = "\r\n";
#else
    static const char   ending[] = "\n";
#endif
    st_oop  s = string_from_c(ending, sizeof ending - 1);

    if (!OM_is_present(s))
        return 0;
    ST_pop_n(1);
    ST_push(s);
    return 1;
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

/*
 *  The image moves the pointer -- primitive 91, InputState>>primCursorLocPut:.
 *
 *  This used to answer "the host owns the pointer" and do nothing, which is
 *  true of the pointer's SHAPE and false of its position.  1983 warps it
 *  constantly, and each warp is load-bearing:
 *
 *      PopUpMenu>>startUp:            Sensor cursorPoint: marker center
 *      StandardSystemController>>move Sensor cursorPoint: labelDisplayBox origin
 *      StandardSystemView>>getFrame   Sensor cursorPoint: minimumCorner
 *      Rectangle>>fromUser            Sensor cursorPoint: minCorner
 *
 *  The first is the one people notice.  A menu is displayed centred on the
 *  cursor and then puts the cursor ON its current marker, so that tracking
 *  starts from a selected item.  Ignore the warp and the pointer stays
 *  wherever it was -- frequently outside the menu's frame entirely, because
 *  the frame was just translated to fit on the screen -- so manageMarker
 *  tracks from outside, the selection is nothing, and releasing the button
 *  chooses nothing.  From the chair that is "my click did not register",
 *  and it comes right the moment you move the mouse into the menu, which is
 *  exactly the shape of the report.
 *
 *  Answering 0 for a non-integer Point is not a failure to handle: the
 *  image's own fallback is `^self primCursorLocPutAgain: aPoint rounded'.
 */
static int
primitive_cursor_loc_put(void)
{
    st_oop  point = ST_stack_value(0);
    st_oop  x;
    st_oop  y;

    if (!OM_is_object(point) || !OM_pointer_bit(point)
     || OM_fetch_word_length(point) < 2)
        return 0;
    x = OM_fetch_pointer(ST_POINT_X, point);
    y = OM_fetch_pointer(ST_POINT_Y, point);
    if (!OM_is_int(x) || !OM_is_int(y))
        return 0;               /*  let the image round it and come back  */
    GFX_warp_pointer((int) OM_int_value(x), (int) OM_int_value(y));
    ST_pop_n(1);
    return 1;                   /*  receiver remains as the result  */
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

/*
 *  The pointer becomes the Form the image asked for.
 *
 *  This used to answer "the host draws its own pointer" and throw the Form
 *  away, which is defensible right up until you notice what the cursor is
 *  FOR in this interface.  StandardSystemView>>getFrame is the whole of the
 *  case:
 *
 *      Sensor waitNoButton.
 *      Cursor origin showWhile:
 *          [[Sensor redButtonPressed] whileFalse: [Processor yield]].
 *
 *  Choose `browser' from the desktop menu and that is what runs.  It draws
 *  nothing, it prints nothing, and it waits for you -- for ever, if you let
 *  it.  The single signal that the system wants a rectangle framed is the
 *  cursor turning into a top-left corner.  Discard it and there is no signal
 *  at all: the screen is frozen, the menu is gone, and the only honest
 *  reading is that the thing has hung.  It cost two bug reports.
 */
static int
primitive_be_cursor(void)
{
    GFX_set_cursor(ST_stack_top());
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
    uint32_t    ticks = ST_time_ms_clock();

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

/*
 *  How wide a Float this build makes.
 *
 *  Chapter 30's Float is IEEE single precision, two 16-bit words, and the
 *  Blue Book memory must keep making those: it loads Xerox's own image,
 *  whose Floats are that shape, and answers trace2 byte for byte.
 *
 *  The 64-bit memory builds its own image and is under no such obligation,
 *  and single precision was costing it real answers.  Duration>>asDays is
 *  a chain of divisions, so (Duration weeks: 1) asDays came out as
 *  6.99999952 -- which prints as 7.0, compares unequal to 7, and had eight
 *  Chronology tests reporting "expected 7 but got 7.0".  Twenty-four bits
 *  of mantissa is simply not enough for date arithmetic, and every dialect
 *  since has used doubles.
 *
 *  The reader was already written for both widths (see float_value above),
 *  which is what makes this a two-line change rather than a format
 *  migration.
 */
#ifdef ST_OM_MT
#define FLOAT_WORDS     4
#else
#define FLOAT_WORDS     2
#endif

static st_oop
make_float(double value)
{
    st_oop      p = OM_instantiate_words(ST_CLASS_FLOAT, FLOAT_WORDS);
    int         i;

    if (!OM_is_present(p))
        return ST_OOP_INVALID;
    if (FLOAT_WORDS == 2) {
        union { float f; uint32_t u; } bits;

        bits.f = (float) value;
        OM_store_word(0, p, (uint16_t) (bits.u >> 16));
        OM_store_word(1, p, (uint16_t) (bits.u & 0xFFFF));
        return p;
    }
    {
        union { double d; uint64_t u; } bits;

        bits.d = value;
        /*  Most significant word first, which is how float_value reads.  */
        for (i = 0; i < 4; ++i)
            OM_store_word((uint32_t) i, p,
                          (uint16_t) ((bits.u >> (16 * (3 - i))) & 0xFFFF));
    }
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
        /*
         *  trunc, not a cast to long long.  The cast is undefined for any
         *  receiver outside the integer's range -- `1.0e100 fractionPart'
         *  reached it -- and trunc is exact for every double there is,
         *  including the ones with no fractional part left to speak of.
         */
        if (!float_value(ST_stack_value(0), &a))
            return 0;
        return answer_float(a - trunc(a), 1);
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

/*
 *  188: withArgs:executeMethod: -- run a CompiledMethod that is installed
 *  nowhere.  Pharo's number and Pharo's name.
 *
 *  1983's Compiler evaluates an expression by compiling it under the
 *  selector #DoIt, installing THAT in the receiver's class, sending it,
 *  and removing it again.  One slot in one method dictionary, for every
 *  evaluation in the image: eight workers evaluating `3 + 4' at once lost
 *  138 of 800 answers, each a worker whose DoIt another worker had already
 *  removed, and a nil from the doesNotUnderstand that followed.  Locking
 *  the three steps would serialize every evaluation behind every other
 *  and fail the moment an evaluated expression evaluated something.
 *  Reorganize instead: with this, the method is run from the compiler's
 *  hand and nothing shared is touched.
 *
 *  Fails, and so falls back to the Smalltalk body, when the arguments are
 *  not an Array, the method is not a CompiledMethod, or the count does
 *  not match what the method's header says -- the interpreter would stop
 *  the image on a mismatch, so it is refused here first.
 */
static int
primitive_execute_method(void)
{
    st_oop      arguments = ST_stack_value(1);
    st_oop      method    = ST_stack_value(0);
    uint32_t    count;
    uint32_t    i;

    if (st_vm.argument_count != 2)
        return 0;
    if (!OM_is_present(arguments) || !OM_is_present(method)
     || OM_fetch_class(arguments) != ST_CLASS_ARRAY
     || OM_fetch_class(method) != ST_CLASS_COMPILED_METHOD)
        return 0;
    count = OM_fetch_word_length(arguments);
    if (ST_method_argument_count(method) != count)
        return 0;
    ST_pop_n(2);
    for (i = 0; i < count; ++i)
        ST_push(OM_fetch_pointer(i, arguments));
    ST_execute_method(method, count);
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

/*
 *  ----------  What Pharo's Kernel asks for  ----------
 *
 *  Every one of these was named by "st80 -primitives" over Pharo's Kernel.
 *  That is what the report is for: it turned an unbounded question into a
 *  list, and a list can be worked down.
 *
 *  What is NOT here is as deliberate, and is in doc/PHARO-INTAKE.md by
 *  number: primitives this memory cannot honour, and primitives that are
 *  optimisations of Smalltalk that already works.
 */

/*
 *  58 and 59: ln and exp.
 *
 *  Not decoration.  Without them the image falls back to the Taylor series
 *  in the 1983 Float>>ln and Float>>exp, which stop at
 *  MathApproximationEpsilon and carry their own error on top of the
 *  representation's.  Pharo's doctest for "2 raisedTo: 1/12" is what
 *  noticed.
 */
static int
primitive_float_ln(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    if (!(value > 0.0))
        return 0;               /*  the image's own code raises for these  */
    return answer_float(log(value), 1);
}

static int
primitive_float_exp(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    return answer_float(exp(value), 1);
}

/*
 *  55, 56 and 57: sqrt, sin and arcTan -- and 223 and 224, cos and tan.
 *
 *  The same argument as 58 and 59 above, and the audit that forced it is
 *  worth stating.  Float class>>initialize in the 1983 source assigns
 *  `Pi _ 3.14159' and five-digit coefficient tables for sin, tan, exp and
 *  ln, which was right for the twenty-four bit mantissa Chapter 30 had.
 *  This memory's Float is a double, so once ln and exp moved onto
 *  primitives the system answered SEVENTEEN correct digits for some
 *  functions and SIX for others, with nothing to say which one a caller
 *  was getting: `1.0 cos' was 0.5403011272718037 where the answer is
 *  0.5403023058681398, and `Float pi' printed 3.14159 in a system whose
 *  printer now shows every digit it has.  Errors from 4e-8 to 4.7e-5, in
 *  a form that looks like precision.
 *
 *  55, 56 and 57 are the Blue Book's own numbers for these three, the same
 *  block 58 and 59 came from, so ported source that says `<primitive: 56>'
 *  means what it says here.
 *
 *  THE BLUE BOOK HAS NO NUMBER FOR COS, TAN, ARCSIN OR ARCCOS, and neither
 *  does Squeak or Pharo: all three derive them from sin and arcTan --
 *  `cos(x) = sin(x + pi/2)', `tan = sin/cos', `arcSin(x) = arcTan(x/sqrt(1
 *  - x*x))', `arcCos = pi/2 - arcSin'.  Those derivations were MEASURED
 *  before they were rejected, and each loses digits somewhere:
 *
 *    cos    at x = 1e6, sin(x + pi/2) answers 0.9367521275136697 where cos
 *           is 0.9367521275331447 -- nine digits instead of sixteen,
 *           because adding a rounded pi/2 spends what the argument's own
 *           exponent has already spent.  Angles accumulate, which is
 *           exactly when x gets large.
 *    tan    sin/cos is an ulp out at x = 1.0: 1.557407724654902 for
 *           1.5574077246549023.
 *    arcSin near the ends of its domain: at x = 0.99999999 the arcTan form
 *           answers 1.570654905438225 for 1.5706549054381862, ten digits.
 *    arcCos at x = 0.5, pi/2 - arcSin answers 1.0471975511965976 for
 *           1.0471975511965979.
 *
 *  So the four get numbers of their own -- 224 to 227, from the stretch
 *  above ST_PRIMITIVE_STRING_HASH that neither the Blue Book nor Squeak
 *  assigns -- chosen so no ported source can already mean something else
 *  by them, which is the rule the collision doc/PHARO-INTAKE.md records
 *  over primitive 249 exists to state.
 *
 *  Each fails for a receiver that is not a Float and, where the function
 *  has no answer, for a receiver outside its domain -- so the image's own
 *  1983 fallback runs and raises the sentence it always raised.
 */
static int
primitive_float_sqrt(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    if (!(value >= 0.0))
        return 0;               /*  negative, or a NaN: the image raises  */
    return answer_float(sqrt(value), 1);
}

static int
primitive_float_sin(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    return answer_float(sin(value), 1);
}

static int
primitive_float_arc_tan(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    return answer_float(atan(value), 1);
}

static int
primitive_float_cos(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    return answer_float(cos(value), 1);
}

static int
primitive_float_tan(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    return answer_float(tan(value), 1);
}

static int
primitive_float_arc_sin(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    if (!(value >= -1.0 && value <= 1.0))
        return 0;               /*  out of domain, or a NaN: the image raises  */
    return answer_float(asin(value), 1);
}

static int
primitive_float_arc_cos(void)
{
    double  value;

    if (!float_value(ST_stack_value(0), &value))
        return 0;
    if (!(value >= -1.0 && value <= 1.0))
        return 0;
    return answer_float(acos(value), 1);
}

/*
 *  Float>>raisedTo: a Float, which is C's pow.
 *
 *  1983 computes it as `(exponent * self ln) exp' -- two transcendentals
 *  where one would do -- so `10 raisedTo: 2.0' answered 100.00000000000004
 *  and `2 raisedTo: 0.5' was not `2 sqrt'.  Pharo does the same thing, so
 *  this is not a conformance failure; it is simply less accurate than the
 *  machine can be for free.  pow is correctly rounded for the exact cases
 *  on every libm worth the name, and no worse than the logarithm elsewhere.
 *
 *  It fails for a negative receiver with a fractional exponent, where the
 *  answer is not real, and for a receiver of zero with a negative one --
 *  the two cases where pow answers a NaN or an infinity.  The image's own
 *  1983 body then runs and raises whatever it always raised.
 */
static int
primitive_float_raised_to(void)
{
    double  base;
    double  exponent;
    double  answer;

    if (!float_value(ST_stack_value(1), &base))
        return 0;
    if (!float_value(ST_stack_value(0), &exponent))
        return 0;
    answer = pow(base, exponent);
    if (answer != answer)               /*  a NaN: not a real answer  */
        return 0;
    if (base == 0.0 && exponent < 0.0)
        return 0;
    return answer_float(answer, 2);
}

/*
 *  228: compile source into a CompiledMethod, in the running image.
 *
 *  The image had a second compiler -- 1983's, in Smalltalk -- and it did not
 *  agree with this one: it gives blocks Chapter 27's BlockContext where the
 *  C compiler gives them closures, so the same text meant one thing when it
 *  was compiled INTO the image and another when it was recompiled inside it.
 *  A Tonel service file reloaded on a running server is the case that made
 *  it matter.  This is the door that lets the image reach the code generator
 *  it was built with; see src/compiler/image_compile.c for the whole
 *  argument and for what the compile needs that the bootstrap's context
 *  cannot give it.
 *
 *  Five arguments, and none of them is a convenience:
 *
 *    source       the text, a String
 *    class        the Behavior it is being compiled for, or nil
 *    ivarNames    that class's allInstVarNames, computed by the IMAGE so
 *                 that what is in scope has one definition and not two
 *    association  an Association whose value is the class, for a super
 *                 send, or nil
 *    noPattern    true for a doIt: no pattern line, and the last
 *                 statement's value is the answer
 *
 *  Answers an Array of six, always, because a compile that fails is an
 *  ordinary outcome and not a primitive failure:
 *
 *    1  the CompiledMethod, or nil
 *    2  its selector as a Symbol, or nil
 *    3  the message, or nil when it succeeded
 *    4  the line the message is about, one-relative
 *    5  the CHARACTER POSITION, one-relative, which is what an editor
 *       needs -- Compiler>>notify:at: selects the text there
 *    6  the name, when the failure was an undeclared variable, so the
 *       caller can declare it and compile again -- which is what the
 *       image's own Encoder does when there is no editor to ask a person
 *
 *  THE CALLER MUST HOLD THE IMAGE'S SYMBOL LOCK.  Interning here goes
 *  through the bootstrap's table, which has no lock of its own, while the
 *  image's Symbol class>>intern: holds LibraryLocks' Symbol mutex; the two
 *  are serialised only if this side takes the same one.  Two workers
 *  interning one new selector make two Symbols, and a Symbol that is not
 *  identical to itself cannot be found as a selector.
 */
static int
primitive_compile_method(void)
{
    st_oop              source     = ST_stack_value(4);
    st_oop              class_oop  = ST_stack_value(3);
    st_oop              ivar_names = ST_stack_value(2);
    st_oop              assoc      = ST_stack_value(1);
    st_oop              no_pattern = ST_stack_value(0);
    st_compile_result   res;
    st_oop              guard;
    st_oop              answer;
    st_oop              text;
    int                 status;

    if (!OM_is_object(source) || OM_pointer_bit(source))
        return 0;

    /*
     *  The guard is pushed BEFORE the compile and stays until the answer is
     *  built, because everything either of them makes is otherwise held in
     *  C alone.  A context's stack is marked up to its stack pointer, so a
     *  push is all it takes to make one reachable.
     */
    guard = OM_instantiate_pointers(ST_CLASS_ARRAY, 256);
    if (!OM_is_object(guard))
        return 0;
    ST_push(guard);

    status = IMGC_compile(source, class_oop, ivar_names, assoc,
                          no_pattern == ST_TRUE, ST_DIALECT_CLOSURES,
                          guard, &res);

    answer = OM_instantiate_pointers(ST_CLASS_ARRAY, 6);
    if (!OM_is_object(answer)) {
        ST_pop_n(1);                /*  the guard  */
        return 0;
    }
    ST_push(answer);
    if (status == 0) {
        OM_store_pointer(0, answer, res.method);
        text = BOOT_intern_symbol(res.selector, NULL);
        OM_store_pointer(1, answer, text);
    }  else  {
        text = BOOT_make_string(res.error, NULL);
        OM_store_pointer(2, answer, text);
        OM_store_pointer(3, answer, OM_int_oop((st_int) res.error_line));
        OM_store_pointer(4, answer,
                         OM_int_oop((st_int) res.error_offset + 1));
        if (res.undeclared[0]) {
            text = BOOT_make_string(res.undeclared, NULL);
            OM_store_pointer(5, answer, text);
        }
    }
    /*
     *  The guard and the rooting push come off, then the receiver and its
     *  five arguments, and the answer goes where the receiver was -- which
     *  is what every primitive leaves behind.  Popping the arguments and
     *  not the receiver left TWO values where the send expected one, and
     *  the caller's stack was off by one from there on: the first symptom
     *  was addSelector:withMethod: sending #asOop to something that was not
     *  a Symbol.  Nothing is allocated between the pop and the push, so the
     *  answer needs no root across it.
     */
    ST_pop_n(2);                    /*  the answer and the guard  */
    ST_pop_n(6);                    /*  the arguments and the receiver  */
    ST_push(answer);
    return 1;
}

/*  132: does this object hold that one in one of its fields?  */
static int
primitive_inst_vars_include(void)
{
    st_oop      receiver = ST_stack_value(1);
    st_oop      wanted = ST_stack_value(0);
    uint32_t    n;
    uint32_t    i;

    if (!OM_is_object(receiver) || !OM_pointer_bit(receiver))
        return answer_boolean(0, 2);
    n = OM_fetch_word_length(receiver);
    for (i = 0; i < n; ++i) {
        if (OM_fetch_pointer(i, receiver) == wanted)
            return answer_boolean(1, 2);
    }
    return answer_boolean(0, 2);
}

/*  148: a shallow copy -- same class, same fields, new identity.  */
static int
primitive_shallow_copy(void)
{
    st_oop      receiver = ST_stack_value(0);
    st_oop      copy;
    uint32_t    i;

    if (!OM_is_object(receiver))
        return 0;               /*  a SmallInteger is already its own copy */
    if (OM_pointer_bit(receiver)) {
        uint32_t    n = OM_fetch_word_length(receiver);

        copy = OM_instantiate_pointers(OM_fetch_class(receiver), n);
        if (!OM_is_object(copy))
            return 0;
        for (i = 0; i < n; ++i)
            OM_store_pointer(i, copy, OM_fetch_pointer(i, receiver));
    }  else  {
        uint32_t    n = OM_fetch_byte_length(receiver);

        copy = OM_instantiate_bytes(OM_fetch_class(receiver), n);
        if (!OM_is_object(copy))
            return 0;
        for (i = 0; i < n; ++i)
            OM_store_byte(i, copy, OM_fetch_byte(i, receiver));
    }
    ST_pop_n(1);
    ST_push(copy);
    return 1;
}

/*
 *  159: hashMultiply, the scrambling step the Smalltalk hashes are built
 *  on.  Squeak's exact arithmetic -- 28 bits, times 1664525 -- because
 *  ported code compares hashes against values computed elsewhere.
 */
static int
primitive_hash_multiply(void)
{
    st_oop      receiver = ST_stack_value(0);
    uint32_t    value;

    if (!OM_is_int(receiver))
        return 0;
    value = (uint32_t) (OM_int_value(receiver) & 0x0FFFFFFF);
    value = (value * 1664525u) & 0x0FFFFFFFu;
    return answer_positive(value, 1);
}

/*
 *  223: the hash of a String, over every byte of it.  This system's own.
 *
 *  1983's String>>hash read three bytes -- the first character, the
 *  second-to-last and the length -- chosen so that the answer stayed a
 *  16-bit SmallInteger on the Alto.  On a Dictionary keyed by Strings that
 *  look alike it is not a hash at all: 'key1'..'key200' produce eleven
 *  distinct values, and filling a Dictionary with a thousand such keys took
 *  700ms, quadratically, which is how it was found (doc/JSON.md).
 *
 *  A primitive rather than a loop in bytecodes because a hashed collection
 *  hashes the key on EVERY at:, and a loop over the characters interpreted
 *  would cost more than the probe it exists to shorten.  The method in
 *  lib/Collections-Protocol/String.extension.st keeps the same loop in
 *  Smalltalk as its fallback, and the bootstrap keeps the same function in
 *  C to place symbols in the library's table; tests/unit/test_image.c
 *  holds all three equal, since a copy that is merely believed is a bug
 *  with a long fuse.
 *
 *  The function is FNV-1a over the bytes, 32 bits wide, then the high half
 *  folded into the low half and the top four bits dropped.
 *
 *  FNV-1a because it is short enough to write in Smalltalk EXACTLY: each
 *  step is a 32-bit value times a 24-bit prime, which is a SmallInteger
 *  here, so the fallback needs no LargePositiveInteger to agree with this.
 *  And because multiplying by an odd constant is a bijection modulo 2^32,
 *  two strings that differ only in their last byte never share a hash.
 *
 *  The fold because of where the hash goes.  1983's Set and Dictionary
 *  choose a bucket by `hash \\ basicSize', and basicSize starts at 32 and
 *  doubles -- a power of two, so only the LOW bits of the hash ever choose
 *  a bucket.  A multiply carries information upward and never down: the
 *  low five bits of the product depend on the low five bits of the operand
 *  alone.  Without the fold 'a' and 'A', which differ in bit 5 and nothing
 *  else, would share a bucket in every 32-slot Set, and so would any two
 *  strings whose last characters differ only in case.  Folding the top
 *  sixteen bits down puts every bit of every byte into the bits a bucket
 *  is chosen by.
 *
 *  28 bits, so that this and hashMultiply above agree about how wide a
 *  hash is; ported code combines the two.
 */
uint32_t
ST_string_hash_text(const void *bytes, size_t length)
{
    const uint8_t  *p = (const uint8_t *) bytes;
    uint32_t        h = 2166136261u;
    size_t          i;

    for (i = 0; i < length; ++i)
        h = (h ^ p[i]) * 16777619u;
    return (h ^ (h >> 16)) & 0x0FFFFFFFu;
}

uint32_t
ST_string_hash_object(st_oop string)
{
    uint32_t    length = OM_fetch_byte_length(string);
    uint32_t    h = 2166136261u;
    uint32_t    i;

    for (i = 0; i < length; ++i)
        h = (h ^ OM_fetch_byte(i, string)) * 16777619u;
    return (h ^ (h >> 16)) & 0x0FFFFFFFu;
}

/*
 *  Any byte-indexed receiver, not only a String: a Symbol is one, and
 *  Symbol>>stringhash is `^super hash'.  A pointer or word object fails,
 *  which runs the Smalltalk body, which sends at: and asciiValue and will
 *  say what is wrong in its own words.
 */
static int
primitive_string_hash(void)
{
    st_oop      receiver = ST_stack_value(0);
    om_shape    shape;

    if (!OM_is_object(receiver))
        return 0;
    shape = shape_of_class(OM_fetch_class(receiver));
    if (shape.pointers || shape.words || !shape.indexable)
        return 0;
    return answer_positive(ST_string_hash_object(receiver), 1);
}

/*
 *  163, 164, 183, 184: read-only objects, and pinned ones.
 *
 *  Spur keeps both in the object header.  This memory has neither, and the
 *  honest answer is the one that is TRUE here rather than the one that
 *  makes the caller happy: nothing is read-only and nothing is pinned.  So
 *  asking answers false, and SETTING either to false succeeds, because it
 *  is already so.  Setting one to true fails -- and a primitive that fails
 *  runs the image's own fallback, which is exactly where the decision about
 *  what to do instead belongs.
 */
static int
primitive_answer_false_of_receiver(void)
{
    return answer_boolean(0, 1);
}

static int
primitive_set_flag_false_only(void)
{
    if (ST_stack_value(0) != ST_FALSE)
        return 0;
    /*  Answers the old value, which was false as well.  */
    ST_pop_n(2);
    ST_push(ST_FALSE);
    return 1;
}

/*  168: a copy keeping only the first n indexed fields.  */
static int
primitive_copy_from(void)
{
    st_oop      receiver = ST_stack_value(1);
    st_oop      count = ST_stack_value(0);
    om_shape    shape;
    st_oop      copy;
    uint32_t    want;
    uint32_t    i;

    if (!OM_is_object(receiver) || !OM_is_int(count) || OM_int_value(count) < 0)
        return 0;
    want  = (uint32_t) OM_int_value(count);
    shape = shape_of_class(OM_fetch_class(receiver));
    if (OM_pointer_bit(receiver)) {
        if (want + shape.fixed > OM_fetch_word_length(receiver))
            return 0;
        copy = OM_instantiate_pointers(OM_fetch_class(receiver),
                                       shape.fixed + want);
        if (!OM_is_object(copy))
            return 0;
        for (i = 0; i < shape.fixed + want; ++i)
            OM_store_pointer(i, copy, OM_fetch_pointer(i, receiver));
    }  else  {
        if (want > OM_fetch_byte_length(receiver))
            return 0;
        copy = OM_instantiate_bytes(OM_fetch_class(receiver), want);
        if (!OM_is_object(copy))
            return 0;
        for (i = 0; i < want; ++i)
            OM_store_byte(i, copy, OM_fetch_byte(i, receiver));
    }
    ST_pop_n(2);
    ST_push(copy);
    return 1;
}

/*  169: not the same object.  */
static int
primitive_not_equivalent(void)
{
    return answer_boolean(ST_stack_value(1) != ST_stack_value(0), 2);
}

/*
 *  170 and 171: a Character from its code point, and back.
 *
 *  Every Character in this memory is a unique entry in CharacterTable,
 *  which is what makes "$a == $a" true -- so 170 is a table lookup and not
 *  an allocation, and that IS the contract rather than an optimisation of
 *  it.  A code point with no entry fails, and the image raises.
 */
static int
primitive_character_value(void)
{
    st_oop  code = ST_stack_value(0);
    st_int  value;

    if (!OM_is_int(code))
        return 0;
    value = OM_int_value(code);
    if (value < 0 || value > 255)
        return 0;
    ST_pop_n(2);                /*  the class and the code point  */
    ST_push(OM_fetch_pointer((uint32_t) value, ST_CHARACTER_TABLE));
    return 1;
}

static int
primitive_character_as_integer(void)
{
    st_oop  receiver = ST_stack_value(0);
    st_oop  value;

    if (!OM_is_object(receiver)
     || OM_fetch_class(receiver) != ST_CLASS_CHARACTER)
        return 0;
    value = OM_fetch_pointer(0, receiver);
    if (!OM_is_int(value))
        return 0;
    return answer_integer(OM_int_value(value), 1);
}

/*
 *  230: hand the processor back for a while.
 *
 *  Pharo's idle loop calls it with a microsecond count.  Sleeping is the
 *  whole point: the alternative is a worker spinning a core to no purpose,
 *  which on this system means spinning one core out of however many.
 */
static int
primitive_relinquish_processor(void)
{
    st_oop  micros = ST_stack_value(0);

    if (!OM_is_int(micros))
        return 0;
    if (OM_int_value(micros) > 0)
        ST_sleep_ns((int64_t) OM_int_value(micros) * 1000);
    ST_pop_n(1);                /*  answers the receiver  */
    return 1;
}

/*
 *  135 and 240: the clocks Delay is built on.
 *
 *  240 is the UTC microsecond clock, which Pharo's DelayMicrosecondTicker
 *  and Time class>>primUTCMicrosecondsClock both name.  It counts from the
 *  Smalltalk epoch, 1 January 1901, because that is what the image's own
 *  date arithmetic expects.
 *
 *  135 does NOT.  It is the same free-running counter primitive 99 answers
 *  -- see ST_time_ms_clock for why the two had to be made one, and what
 *  eight hours of disagreement between them did to every Delay.
 */
static int
primitive_millisecond_clock(void)
{
    return answer_positive(ST_time_ms_clock(), 1);
}

/*
 *  primitive 210 -- Clipboard class>>primCommand:with:
 *
 *  The system's clipboard, three ways: 0 answers whether there is one (a
 *  window is open), 1 answers its text as a String or nil when there is
 *  none, 2 puts a String on it and answers whether it could.  The
 *  editor's copy and cut send 2 and its paste sends 1, so that text moves
 *  between a workspace and the rest of the desktop -- which the 1983
 *  editor, whose clipboard was a class variable, could not do.
 *
 *  Bytes cross as they are.  The image's strings are bytes and the
 *  system's clipboard is UTF-8, and translating between them is a job for
 *  the image, which knows what it wants; lib/Clipboard translates the line
 *  ends and nothing else.  The primitive FAILS when called wrongly and
 *  answers nil or false when there is no window -- headless, or -serve --
 *  so the editor's own buffer is what is left, as before.
 */
static int
primitive_clipboard(void)
{
    st_oop  arg = ST_stack_value(0);
    st_oop  cmd = ST_stack_value(1);

    if (!OM_is_int(cmd))
        return 0;
    switch ((long) OM_int_value(cmd)) {

    case 0:
        ST_pop_n(3);
        ST_push(GFX_is_open() ? ST_TRUE : ST_FALSE);
        return 1;

    case 1: {
        char   *text = GFX_clipboard_text();
        st_oop  string;

        if (text == NULL) {
            ST_pop_n(3);
            ST_push(ST_NIL);
            return 1;
        }
        string = string_from_c(text, strlen(text));
        free(text);
        if (string == ST_OOP_INVALID)
            return 0;
        ST_pop_n(3);
        ST_push(string);
        return 1;
    }

    case 2: {
        uint32_t    n;
        uint32_t    i;
        char       *text;
        int         rc;

        if (!OM_is_object(arg) || OM_pointer_bit(arg))
            return 0;
        n    = OM_fetch_byte_length(arg);
        text = (char *) malloc((size_t) n + 1);
        if (text == NULL)
            return 0;
        for (i = 0; i < n; ++i)
            text[i] = (char) OM_fetch_byte(i, arg);
        text[n] = '\0';
        rc = GFX_clipboard_set(text);
        free(text);
        ST_pop_n(3);
        ST_push(rc == 0 ? ST_TRUE : ST_FALSE);
        return 1;
    }

    default:
        return 0;
    }
}

/*
 *  primitive 242 -- InputSensor>>wheelDelta
 *
 *  Whole notches since the last ask, positive away from the user, and zero
 *  afterwards.  Signed, so it is a plain SmallInteger rather than one of the
 *  promoting answers: nobody turns a wheel four thousand million times.
 *
 *  A poll and not an event.  ScrollController asks from its control loop --
 *  the loop that already spins on `Processor yield' -- so nothing has to
 *  arrive to wake it, and a notch cannot be lost by a controller that was
 *  not looking when it landed.
 */
static int
primitive_wheel_delta(void)
{
    ST_pop_n(1);                            /*  the receiver  */
    ST_push(OM_int_oop((st_int) GFX_wheel_take()));
    return 1;
}

/*
 *  100 and 136: signal that semaphore when the millisecond clock reaches
 *  that value.  One request outstanding, re-armed by each call and cancelled by
 *  a nil semaphore -- the image keeps the queue of waiting Delays itself
 *  and asks the VM only about the nearest one.
 *
 *  Answering the receiver rather than failing on a nil semaphore is what
 *  Delay class>>startTimerEventLoop wants: it disarms with
 *  `Processor signal: nil atTime: 0' before installing its own loop, and a
 *  primitive failure there sends the fallback code down a path that
 *  assumes a live timer.
 */
static int
primitive_signal_at_time(void)
{
    st_oop  when      = ST_stack_value(0);
    st_oop  semaphore = ST_stack_value(1);

    if (!OM_is_int(when))
        return 0;
    SCHED_signal_at_ms(semaphore, (uint32_t) OM_int_value(when));
    ST_pop_n(2);                /*  answers the receiver  */
    return 1;
}

/*
 *  100 is the same operation with the 1983 calling convention, and it is
 *  the one that matters here: ProcessorScheduler>>signal:atMilliseconds:
 *  is what sources/ actually sends, having taken the number apart into a
 *  four-byte indexable object, low order first.  136 is Squeak's later
 *  spelling with a plain SmallInteger.  Both are answered because both
 *  arrive -- the 1983 Delay through 100, anything ported through 136 --
 *  and the difference is entirely in how the argument is spelled.
 *
 *  Looking for 136 alone is why the first version of this changed nothing:
 *  the timer was implemented, correct, and never once armed.
 */
static int
primitive_signal_at_milliseconds(void)
{
    st_oop      bytes     = ST_stack_value(0);
    st_oop      semaphore = ST_stack_value(1);
    uint32_t    when      = 0;
    uint32_t    i;

    if (!OM_is_object(bytes) || OM_fetch_byte_length(bytes) < 4)
        return 0;
    for (i = 0; i < 4; ++i)
        when |= (uint32_t) OM_fetch_byte(i, bytes) << (8 * i);
    SCHED_signal_at_ms(semaphore, when);
    ST_pop_n(2);                /*  answers the receiver  */
    return 1;
}

/*
 *  255: the shortest decimal that reads back as this Float.
 *
 *  Printing a double is not a thing the image can do for itself here, and
 *  that was measured rather than assumed.  1983's absPrintOn:digits: builds
 *  its digits with Float arithmetic -- `10.0 raisedTo: exp', a fuzz term,
 *  repeated division -- which was accurate enough for the twenty-four bit
 *  mantissa it was written for and is not for fifty-three.  Asked for
 *  seventeen digits of 3.0/13.0 it answers 0.23076923076923074, and neither
 *  this system's compiler nor its own Number>>readFrom: reads that back as
 *  the number that produced it.  Seven of forty test values failed that way.
 *
 *  So the formatting is done where the exact answer is available.  The C
 *  library's %.*g is correctly rounded and strtod is its exact inverse, so
 *  trying fifteen, sixteen and seventeen digits and keeping the first that
 *  survives strtod gives the shortest decimal that round-trips -- which is
 *  the rule every modern dialect uses, and the reason 0.1 prints as 0.1
 *  rather than 0.10000000000000001.
 *
 *  Answering a String rather than writing to a stream keeps the primitive
 *  ignorant of how the image wants to print; Float>>printOn: puts it
 *  wherever it is going.
 */
static int
primitive_float_print_string(void)
{
    st_oop      receiver = ST_stack_top();
    double      value;
    char        text[64];
    int         digits;
    st_oop      result;
    uint32_t    i;
    uint32_t    n;

    if (!float_value(receiver, &value))
        return 0;
    /*
     *  A NaN prints as "nan", never as "-nan".
     *
     *  IEEE 754 gives a NaN a sign bit and says nothing about what it
     *  means; which one you get out of `infinity - infinity' is the
     *  hardware's business, and on x86 it is the negative one.  So
     *  `Float nan printString' answered '-nan' here and would answer 'nan'
     *  on a machine that chose differently -- a platform detail leaking
     *  into a printed form, and one that makes a test written against
     *  either answer fail on the other.  There is only one NaN as far as
     *  this system is concerned, so there is only one spelling of it.
     *
     *  An infinity keeps its sign, because there its sign means something.
     */
    if (value != value) {
        snprintf(text, sizeof text, "nan");
        digits = 0;
    } else {
        for (digits = 15; digits <= 17; ++digits) {
            snprintf(text, sizeof text, "%.*g", digits, value);
            if (strtod(text, NULL) == value)
                break;
        }
    }
    /*
     *  An infinity never compares equal to the text it printed, so the loop
     *  above always runs out for one.  Seventeen digits is what it leaves
     *  behind, and printing "inf" is better than failing the primitive and
     *  falling back on arithmetic that cannot represent it either.
     */
    /*
     *  %g writes an exponent C's way -- "1e+16", "1e-05", "1e+308" -- and
     *  the `+' is a spelling no Smalltalk scanner accepts.  Neither this
     *  system's nor Pharo's: the grammar has an optional MINUS after the
     *  `e' and nothing else.  So `1e+16' was not read back as a Float that
     *  differed in the last bit; it was read as THREE tokens, the Float 1.0
     *  and a binary #+ and the Integer 16, and
     *  `Compiler evaluate: (Array with: 1e16 with: 2.5) storeString'
     *  answered an Array of FOUR elements.  Sixteen of the sixty-one powers
     *  of ten from 1e-30 to 1e30 failed to round-trip this way -- all of
     *  them positive, because `-' happens to be the spelling both sides
     *  agree on.
     *
     *  It reached source as well as output: the Parser prints a parse tree
     *  back through Float>>printString, so a method holding `^1e16' filed
     *  out as `^1e+16', and reading THAT gave a method returning 17.0.
     *
     *  Leading zeros go too -- "1e-05" for "1e-5" -- which both scanners do
     *  accept, but 1983's own printer never wrote and no reader expects.
     */
    {
        char   *exponent = strpbrk(text, "eE");

        if (exponent != NULL) {
            char   *from = exponent + 1;
            char   *to   = exponent + 1;

            if (*from == '+')
                ++from;
            else if (*from == '-')
                *to++ = *from++;
            while (from[0] == '0' && from[1] != '\0')
                ++from;
            memmove(to, from, strlen(from) + 1);
        }
    }
    /*
     *  %g drops a trailing ".0", so 1.0 formats as "1" -- which reads back
     *  as an Integer, not as the Float that printed it.  A Float's printed
     *  form has to say it is one.
     *
     *  That holds for an exponent too, and there it is not merely tidiness.
     *  This dialect reads `1e16' as a Float, but Pharo and the ANSI grammar
     *  read a mantissa with no point as an INTEGER with a scale, so `2e3' is
     *  2000 the SmallInteger.  Writing the point makes the printed form say
     *  Float to every reader rather than only to this one, and it is what
     *  1983's absPrintOn:digits: wrote: "1.0e16".
     */
    if (!strpbrk(text, ".nN")) {
        char   *exponent = strpbrk(text, "eE");

        if (exponent == NULL) {
            size_t  used = strlen(text);

            if (used + 3 < sizeof text)
                snprintf(text + used, sizeof text - used, ".0");
        } else if (strlen(text) + 2 < sizeof text) {
            memmove(exponent + 2, exponent, strlen(exponent) + 1);
            exponent[0] = '.';
            exponent[1] = '0';
        }
    }
    n = (uint32_t) strlen(text);
    result = OM_instantiate_bytes(ST_CLASS_STRING, n);
    if (!OM_is_present(result))
        return 0;
    for (i = 0; i < n; ++i)
        OM_store_byte(i, result, (uint8_t) text[i]);
    ST_pop_n(1);
    ST_push(result);
    return 1;
}

static int
primitive_utc_microsecond_clock(void)
{
    return answer_positive((uint64_t) ST_time_smalltalk_ms() * 1000u, 1);
}

/*
 *  ----------  This system's own: parallel operations  ----------
 *
 *  Numbers matter here, and the range had to be chosen twice.  The plan
 *  reserved 240-245; Pharo turned out to use 240 and 242 for its clocks,
 *  249 for a bulk become: and 254 for VM parameters, so ported source
 *  names four of them and this system may not.  What is left in the top
 *  block, after Pharo and after 246-248 and 250-251 which are already
 *  ours, was written down once as 241, 243, 244, 245, 252, 253 and 255 and
 *  has drifted since: 241 and 243-248 and 250-253 are all in use now, and
 *  255 goes to Float>>printString below.  That leaves 249 and 254, which
 *  Pharo names, and nothing else.  A list of free numbers in a comment is a
 *  list that goes stale; the switch below is the only authority.
 *
 *  The header gives the primitive index eight bits, so there is nowhere
 *  else to go: every number this VM will ever have is below 256, and the
 *  ones ported source uses are spoken for.  That is the whole reason the
 *  block is documented rather than assumed.
 */

/*
 *  241: the process THIS worker is running.
 *
 *  Processor>>activeProcess reads one instance variable, and one variable
 *  cannot answer a question that now has a different answer per thread.
 *  The variable stays -- a snapshot carries it, and a freshly loaded image
 *  has nothing else -- but the primitive asks the caller.
 */
static int
primitive_active_process(void)
{
    st_oop  process = SCHED_active_process();

    if (!OM_is_present(process))
        return 0;
    ST_pop_n(1);
    ST_push(process);
    return 1;
}

/*
 *  243: which worker is asking, zero-based, for confining work to one.
 *
 *  A thread that is not a worker answers 0: right when there is no pool,
 *  since the one thread is then worker 0 of 1, and what the main thread
 *  gets while a pool is up, when it runs no Smalltalk.  Distinct per
 *  worker, in range and steady is a property tests/unit/test_parallel_lib
 *  checks from every worker at once, without printing anything -- a probe
 *  that printed the index to build keys once found 29 values on 31 workers
 *  and blamed this primitive, when it was printString that was shared.
 */
static int
primitive_active_worker_index(void)
{
    st_worker  *self = WORKER_self();

    return answer_integer(self ? (st_int) self->index : 0, 1);
}

/*  244: how many workers there are.  */
static int
primitive_worker_count(void)
{
    unsigned    count = WORKER_count();

    return answer_integer((st_int) (count ? count : 1), 1);
}

/*
 *  245: compareAndSwapSlot:from:to: -- the one operation a lock-free
 *  algorithm cannot be written without.
 *
 *  It answers whether the swap happened, rather than the old value,
 *  because that is what every caller tests and it leaves no room to forget
 *  the comparison.  The slot is a field index, one-relative as Smalltalk
 *  counts, and out-of-range fails rather than writing somewhere else.
 */
static int
primitive_compare_and_swap_slot(void)
{
    st_oop      receiver = ST_stack_value(3);
    st_oop      slot = ST_stack_value(2);
    st_oop      expected = ST_stack_value(1);
    st_oop      wanted = ST_stack_value(0);
    st_int      index;

    if (!OM_is_object(receiver) || !OM_pointer_bit(receiver)
     || !OM_is_int(slot))
        return 0;
    index = OM_int_value(slot);
    if (index < 1 || (uint32_t) index > OM_fetch_word_length(receiver))
        return 0;
    return answer_boolean(OM_compare_and_swap_pointer((uint32_t) (index - 1),
                                                      receiver, expected,
                                                      wanted), 4);
}

/*
 *  252 and 253: the two ready-list walks ProcessorScheduler used to do in
 *  Smalltalk, field by field, with no lock and no idea that another worker
 *  might be walking the same chain.
 */
static int
primitive_remove_ready_process(void)
{
    st_oop  process = ST_stack_value(0);

    if (!OM_is_object(process))
        return 0;
    return answer_boolean(SCHED_remove_ready_process(process), 2);
}

static int
primitive_first_ready_process_at(void)
{
    st_oop  priority = ST_stack_value(0);
    st_oop  first;

    if (!OM_is_int(priority))
        return 0;
    first = SCHED_first_ready_process_at(OM_int_value(priority));
    ST_pop_n(2);
    ST_push(OM_is_present(first) ? first : ST_NIL);
    return 1;
}

/*  ----------  Cryptography  ----------
 *
 *  primitive 209 -- Crypto class >> primCommand:with:with:with:
 *
 *  SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, random bytes and a
 *  constant-time compare, for lib/Crypto's PasswordHash: the stored form
 *  of a password is six hundred thousand HMAC rounds, a fifth of a second
 *  in C and minutes in the interpreter.  The arithmetic is
 *  src/crypto/st_crypto.c, which knows nothing about the object memory;
 *  this is the marshalling, in 208's shape -- a command number and three
 *  slots, arguments copied out before any call, answers built after --
 *  and under 208's contract: the primitive FAILS when it was called
 *  wrongly (an Integer where bytes were wanted, a count out of range) and
 *  the Smalltalk fallback raises; it answers NIL when the system could not
 *  do it (no entropy, no memory); and a value otherwise.
 *
 *  THE COPY OUT IS NOT AN OPTIMISATION.  A key derivation runs inside a
 *  WORKER_enter_native region, so that a worker spending a fifth of a
 *  second on a login does not hold up a collection on every other core
 *  (worker.h, `Blocking outside the object memory').  Inside the region
 *  the object memory may move; so the password, the salt and the message
 *  are copied into malloc'd buffers first and the digest is made into a
 *  String after, and the buffers that held key material are wiped before
 *  they are freed.  The hashes take the same road for the same reason: a
 *  64 MB upload's digest is a tenth of a second too.
 *
 *  Unlike 208 this is not under ST_OM_MT: nothing here needs a handle
 *  wider than fifteen bits or a worker, so the Blue Book memory computes
 *  the same answers.  Its fifteen-bit SmallInteger cannot hold 600,000,
 *  and a count that arrives as a LargePositiveInteger fails the
 *  primitive, which is the truth about that memory and the format.
 */

enum {
    CRYPTO_CMD_SHA256   = 1,    /*  bytes                      */
    CRYPTO_CMD_HMAC     = 2,    /*  key, message               */
    CRYPTO_CMD_PBKDF2   = 3,    /*  password, salt, iterations */
    CRYPTO_CMD_RANDOM   = 4,    /*  buffer                     */
    CRYPTO_CMD_EQUAL    = 5     /*  bytes, bytes               */
};

/*  Crypto class >> maxIterations says the same number; its comment says why.  */
#define CRYPTO_MAX_ITERATIONS   10000000

static int
crypto_is_bytes(st_oop p)
{
    return OM_is_object(p) && !OM_pointer_bit(p);
}

/*  A byte object's bytes in a fresh buffer -- one byte even for none, so
 *  that NULL means only that malloc said no.  */
static unsigned char *
crypto_copy_out(st_oop p, size_t *count)
{
    uint32_t        n = OM_fetch_byte_length(p);
    unsigned char  *buffer = (unsigned char *) malloc(n ? n : 1);
    uint32_t        i;

    if (buffer == NULL)
        return NULL;
    for (i = 0; i < n; ++i)
        buffer[i] = OM_fetch_byte(i, p);
    *count = n;
    return buffer;
}

static void
crypto_free(unsigned char *buffer, size_t count)
{
    if (buffer != NULL) {
        ST_crypto_wipe(buffer, count);
        free(buffer);
    }
}

/*  Answer, having popped the receiver and four arguments.  */
static int
crypto_answer(st_oop value)
{
    if (value == ST_OOP_INVALID)
        return 0;
    ST_pop_n(5);
    ST_push(value);
    return 1;
}

static int
primitive_crypto_command(void)
{
    st_oop          c   = ST_stack_value(0);
    st_oop          b   = ST_stack_value(1);
    st_oop          a   = ST_stack_value(2);
    st_oop          cmd = ST_stack_value(3);
    unsigned char   digest[ST_SHA256_DIGEST_BYTES];
    unsigned char  *first = NULL;
    unsigned char  *second = NULL;
    size_t          first_count = 0;
    size_t          second_count = 0;

    if (!OM_is_int(cmd))
        return 0;

    switch ((long) OM_int_value(cmd)) {

    case CRYPTO_CMD_SHA256:
        if (!crypto_is_bytes(a))
            return 0;
        first = crypto_copy_out(a, &first_count);
        if (first == NULL)
            return crypto_answer(ST_NIL);
        WORKER_enter_native();
        ST_sha256(first, first_count, digest);
        WORKER_leave_native();
        crypto_free(first, first_count);
        return crypto_answer(string_from_c((const char *) digest, sizeof digest));

    case CRYPTO_CMD_HMAC:
        if (!crypto_is_bytes(a) || !crypto_is_bytes(b))
            return 0;
        first  = crypto_copy_out(a, &first_count);
        second = crypto_copy_out(b, &second_count);
        if (first == NULL || second == NULL) {
            crypto_free(first, first_count);
            crypto_free(second, second_count);
            return crypto_answer(ST_NIL);
        }
        WORKER_enter_native();
        ST_hmac_sha256(first, first_count, second, second_count, digest);
        WORKER_leave_native();
        crypto_free(first, first_count);
        crypto_free(second, second_count);
        return crypto_answer(string_from_c((const char *) digest, sizeof digest));

    case CRYPTO_CMD_PBKDF2: {
        int64_t     iterations;
        int         rc;
        st_oop      key;

        if (!crypto_is_bytes(a) || !crypto_is_bytes(b) || !OM_is_int(c))
            return 0;
        iterations = (int64_t) OM_int_value(c);
        if (iterations < 1 || iterations > CRYPTO_MAX_ITERATIONS)
            return 0;
        first  = crypto_copy_out(a, &first_count);
        second = crypto_copy_out(b, &second_count);
        if (first == NULL || second == NULL) {
            crypto_free(first, first_count);
            crypto_free(second, second_count);
            return crypto_answer(ST_NIL);
        }
        WORKER_enter_native();
        rc = ST_pbkdf2_hmac_sha256(first, first_count, second, second_count,
                                   (uint32_t) iterations, digest, sizeof digest);
        WORKER_leave_native();
        crypto_free(first, first_count);
        crypto_free(second, second_count);
        if (rc != 0)
            return crypto_answer(ST_NIL);
        key = string_from_c((const char *) digest, sizeof digest);
        ST_crypto_wipe(digest, sizeof digest);
        return crypto_answer(key);
    }

    case CRYPTO_CMD_RANDOM: {
        uint32_t    n;
        uint32_t    i;

        if (!crypto_is_bytes(a))
            return 0;
        n = OM_fetch_byte_length(a);
        first = (unsigned char *) malloc(n ? n : 1);
        if (first == NULL)
            return crypto_answer(ST_NIL);
        if (n > 0 && NET_random_bytes(first, n) != 0) {
            crypto_free(first, n);
            return crypto_answer(ST_NIL);
        }
        for (i = 0; i < n; ++i)
            OM_store_byte(i, a, first[i]);
        crypto_free(first, n);
        return crypto_answer(ST_TRUE);
    }

    case CRYPTO_CMD_EQUAL: {
        int     equal;

        if (!crypto_is_bytes(a) || !crypto_is_bytes(b))
            return 0;
        first  = crypto_copy_out(a, &first_count);
        second = crypto_copy_out(b, &second_count);
        if (first == NULL || second == NULL) {
            crypto_free(first, first_count);
            crypto_free(second, second_count);
            return crypto_answer(ST_NIL);
        }
        equal = first_count == second_count
             && ST_constant_time_equal(first, second, first_count);
        crypto_free(first, first_count);
        crypto_free(second, second_count);
        return crypto_answer(equal ? ST_TRUE : ST_FALSE);
    }

    default:
        return 0;
    }
}

/*  ----------  The network  ----------
 *
 *  primitive 208 -- Socket class >> primCommand:with:with:with:
 *
 *  One primitive with a command number, for the reason 129 and 130 are:
 *  a subsystem that grows should grow inside its own number.  The shape
 *  is 129's exactly -- four fixed slots, arguments copied out before any
 *  call, answers built after -- and so is the contract: the primitive
 *  FAILS when it was called wrongly, which is a bug in the Socket class
 *  and reaches its fallback code; it answers NIL when the operating system
 *  said no, with Socket class>>lastError carrying the reason; and it
 *  answers FALSE when a non-blocking call could not complete yet, which is
 *  the ordinary case and is what tells the caller to arm and wait.
 *
 *  What is NOT here is any blocking.  Every descriptor is non-blocking and
 *  a worker never waits in this primitive; it waits on a Semaphore that
 *  the network's own thread signals.  See src/net/st_socket.h.
 *
 *  Bytes cross in a per-thread buffer of 64 KiB: RECV reads into it and
 *  copies into the caller's byte object afterwards, SEND copies out first.
 *  That is the file primitive's pattern, and it keeps every pointer into
 *  an object's body out of the network layer, which the parking of
 *  getaddrinfo requires and which costs nothing next to a system call.
 */

/*  The command numbers, as Socket's class-side methods name them.  */
enum {
    NET_CMD_AVAILABLE       =  0,
    NET_CMD_LAST_ERROR      =  1,
    NET_CMD_LAST_ERRNO      =  2,
    NET_CMD_LISTEN          =  3,   /*  host, port, backlog        */
    NET_CMD_ACCEPT          =  4,   /*  listener                   */
    NET_CMD_RECV            =  5,   /*  handle, buffer, max        */
    NET_CMD_SEND            =  6,   /*  handle, bytes, start       */
    NET_CMD_CLOSE           =  7,   /*  handle                     */
    NET_CMD_SHUTDOWN_WRITE  =  8,   /*  handle                     */
    NET_CMD_SET_SEMAPHORES  =  9,   /*  handle, read, write        */
    NET_CMD_ARM             = 10,   /*  handle, mask               */
    NET_CMD_CONNECT         = 11,   /*  host, port                 */
    NET_CMD_CONNECT_RESULT  = 12,   /*  handle                     */
    NET_CMD_LOCAL_PORT      = 13,   /*  handle                     */
    NET_CMD_PEER_ADDRESS    = 14,   /*  handle                     */
    NET_CMD_SET_OPTION      = 15,   /*  handle, option, value      */
    NET_CMD_RANDOM_BYTES    = 16,   /*  buffer                     */
    NET_CMD_ARGUMENTS       = 17,
    NET_CMD_STOP_REQUESTED  = 18,
    NET_CMD_OPEN_COUNT      = 19,
    NET_CMD_ADDRESS_COUNT   = 20,   /*  host, port                 */
    NET_CMD_TLS_AVAILABLE   = 21,
    NET_CMD_TLS_START       = 22,   /*  handle, host name          */
    NET_CMD_TLS_HANDSHAKE   = 23,   /*  handle                     */
    NET_CMD_IS_TLS          = 24,   /*  handle                     */
    NET_CMD_ENVIRONMENT     = 25    /*  name                       */
};

#define NET_SCRATCH_BYTES   65536

/*
 *  0 fresh, 1 initialising, 2 up.  A compare-and-swap, so two workers
 *  opening their first sockets together set the scheduler's hooks once.
 */
static st_atomic_int        net_prim_state;

int
ST_net_init(void)
{
    int expected = 0;

    if (ST_cas_strong(&net_prim_state, &expected, 1)) {
        /*
         *  The async queue's lock before the thread that will post to it,
         *  and the wait hook before any worker can idle on a socket.
         */
        SCHED_async_init();
        if (NET_init(SCHED_signal_token) != 0) {
            fprintf(stderr, "st80: cannot initialise the network: %s\n",
                    NET_last_error());
            ST_store_release(&net_prim_state, 0);
            return -1;
        }
        SCHED_set_external_wait_hook(NET_waits_pending);
        ST_store_release(&net_prim_state, 2);
        return 0;
    }
    while (ST_load_acquire(&net_prim_state) == 1)
        ST_spin_hint();
    return ST_load_acquire(&net_prim_state) == 2 ? 0 : -1;
}

/*  Answer, having popped the receiver and four arguments.  */
static int
net_answer(st_oop value)
{
    if (value == ST_OOP_INVALID)
        return 0;
    ST_pop_n(5);
    ST_push(value);
    return 1;
}

#ifdef ST_OM_MT

static _Thread_local unsigned char  net_scratch[NET_SCRATCH_BYTES];

/*  A handle argument: a SmallInteger, or -1.  */
static int64_t
net_handle_arg(st_oop p)
{
    if (!OM_is_int(p))
        return -1;
    return (int64_t) OM_int_value(p);
}

/*  A byte object, or not.  */
static int
net_is_bytes(st_oop p)
{
    return OM_is_object(p) && !OM_pointer_bit(p);
}

/*  A Semaphore or nil, or not.  */
static int
net_is_semaphore_or_nil(st_oop p)
{
    return p == ST_NIL
        || (OM_is_object(p) && OM_fetch_class(p) == ST_CLASS_SEMAPHORE);
}

/*
 *  What a call answered: a count, would-block, or a failure.  The two
 *  other waits -- a TLS read that must first write, a TLS write that
 *  must first read, a handshake that is either -- are answered as the
 *  SmallIntegers -1 (wait readable) and -2 (wait writable), which no
 *  count can be; false stays "wait the obvious way", so that a plain
 *  socket's caller reads exactly what it always read.
 */
static st_oop
net_result(long n)
{
    if (n == NET_WOULD_BLOCK)
        return ST_FALSE;
    if (n == NET_WANT_READ)
        return OM_int_oop(-1);
    if (n == NET_WANT_WRITE)
        return OM_int_oop(-2);
    if (n < 0)
        return ST_NIL;
    return OM_int_oop((st_int) n);
}

#endif  /*  ST_OM_MT  */

static int
primitive_net_command(void)
{
    st_oop  c   = ST_stack_value(0);
    st_oop  b   = ST_stack_value(1);
    st_oop  a   = ST_stack_value(2);
    st_oop  cmd = ST_stack_value(3);
    long    command;

    if (!OM_is_int(cmd))
        return 0;
    command = (long) OM_int_value(cmd);

#ifndef ST_OM_MT
    /*
     *  The Blue Book memory has fifteen-bit SmallIntegers, which cannot
     *  hold a generation-tagged handle, and no worker pool, which is what
     *  a server is for.  It can say so, and nothing else.
     */
    switch (command) {
    case NET_CMD_AVAILABLE:
    case NET_CMD_TLS_AVAILABLE:
        return net_answer(ST_FALSE);
    case NET_CMD_LAST_ERROR: {
        static const char text[] = "this build has no sockets: the Blue "
                                   "Book memory cannot hold a handle";

        return net_answer(string_from_c(text, sizeof text - 1));
    }
    case NET_CMD_ENVIRONMENT: {
        /*  The one command with no socket in it works in every build.  */
        char        name[256];
        const char *value;

        if (!c_from_string(a, name, sizeof name))
            return 0;
        value = NET_environment(name);
        (void) b; (void) c;
        return net_answer(value ? string_from_c(value, strlen(value)) : ST_NIL);
    }
    default:
        (void) a; (void) b; (void) c;
        return 0;
    }
#else
    if (command != NET_CMD_AVAILABLE && ST_net_init() != 0)
        return net_answer(ST_NIL);

    switch (command) {

    case NET_CMD_AVAILABLE:
        return net_answer(NET_available() ? ST_TRUE : ST_FALSE);

    case NET_CMD_LAST_ERROR: {
        const char *text = NET_last_error();

        return net_answer(string_from_c(text, strlen(text)));
    }

    case NET_CMD_LAST_ERRNO:
        return net_answer(OM_int_oop((st_int) NET_last_errno()));

    case NET_CMD_LISTEN: {
        char        host[256];
        int         port;
        int         backlog;
        int64_t     handle;

        if (a == ST_NIL)
            host[0] = '\0';
        else if (!c_from_string(a, host, sizeof host))
            return 0;
        if (!OM_is_int(b))
            return 0;
        port    = (int) OM_int_value(b);
        backlog = OM_is_int(c) ? (int) OM_int_value(c) : 0;
        handle  = NET_listen(host, port, backlog);
        return net_answer(handle < 0 ? ST_NIL : OM_int_oop((st_int) handle));
    }

    case NET_CMD_ACCEPT: {
        int64_t handle = net_handle_arg(a);
        int64_t got;

        if (handle < 0)
            return 0;
        got = NET_accept(handle);
        if (got == NET_WOULD_BLOCK)
            return net_answer(ST_FALSE);
        return net_answer(got < 0 ? ST_NIL : OM_int_oop((st_int) got));
    }

    case NET_CMD_RECV: {
        int64_t     handle = net_handle_arg(a);
        uint32_t    room;
        size_t      want;
        long        n;
        long        i;

        if (handle < 0 || !net_is_bytes(b) || !OM_is_int(c))
            return 0;
        room = OM_fetch_byte_length(b);
        want = (size_t) OM_int_value(c);
        if (want > room)
            want = room;
        if (want > NET_SCRATCH_BYTES)
            want = NET_SCRATCH_BYTES;
        n = NET_recv(handle, net_scratch, want);
        for (i = 0; i < n; ++i)
            OM_store_byte((uint32_t) i, b, net_scratch[i]);
        return net_answer(net_result(n));
    }

    case NET_CMD_SEND: {
        int64_t     handle = net_handle_arg(a);
        uint32_t    length;
        uint32_t    start;
        size_t      count;
        size_t      i;

        if (handle < 0 || !net_is_bytes(b) || !OM_is_int(c)
         || OM_int_value(c) < 1)
            return 0;
        length = OM_fetch_byte_length(b);
        start  = (uint32_t) OM_int_value(c) - 1;       /*  1-based in  */
        if (start >= length)
            return net_answer(OM_int_oop(0));
        count = length - start;
        if (count > NET_SCRATCH_BYTES)
            count = NET_SCRATCH_BYTES;
        for (i = 0; i < count; ++i)
            net_scratch[i] = OM_fetch_byte(start + (uint32_t) i, b);
        return net_answer(net_result(NET_send(handle, net_scratch, count)));
    }

    case NET_CMD_CLOSE: {
        int64_t     handle = net_handle_arg(a);
        uintptr_t   old_read;
        uintptr_t   old_write;
        int         rc;

        if (handle < 0)
            return 0;
        rc = NET_close(handle, &old_read, &old_write);
        /*
         *  The counts the Semaphores held for the table, released now that
         *  the slot is gone and no thread can hand them to the scheduler.
         */
        if (old_read)
            OM_decrease_ref((st_oop) old_read);
        if (old_write)
            OM_decrease_ref((st_oop) old_write);
        return net_answer(rc == 0 ? ST_TRUE : ST_NIL);
    }

    case NET_CMD_SHUTDOWN_WRITE: {
        int64_t handle = net_handle_arg(a);

        if (handle < 0)
            return 0;
        return net_answer(NET_shutdown_write(handle) == 0 ? ST_TRUE : ST_NIL);
    }

    case NET_CMD_SET_SEMAPHORES: {
        int64_t     handle = net_handle_arg(a);
        uintptr_t   old_read;
        uintptr_t   old_write;
        int         rc;

        if (handle < 0 || !net_is_semaphore_or_nil(b)
         || !net_is_semaphore_or_nil(c))
            return 0;
        /*
         *  Counted BEFORE the table holds them, released AFTER it lets
         *  them go, so that at no instant does the I/O thread hold a token
         *  whose object could be reclaimed.
         */
        if (b != ST_NIL)
            OM_increase_ref(b);
        if (c != ST_NIL)
            OM_increase_ref(c);
        rc = NET_set_tokens(handle,
                            b == ST_NIL ? 0 : (uintptr_t) b,
                            c == ST_NIL ? 0 : (uintptr_t) c,
                            &old_read, &old_write);
        if (rc != 0) {
            if (b != ST_NIL)
                OM_decrease_ref(b);
            if (c != ST_NIL)
                OM_decrease_ref(c);
            return net_answer(ST_NIL);
        }
        if (old_read)
            OM_decrease_ref((st_oop) old_read);
        if (old_write)
            OM_decrease_ref((st_oop) old_write);
        return net_answer(ST_TRUE);
    }

    case NET_CMD_ARM: {
        int64_t handle = net_handle_arg(a);

        if (handle < 0 || !OM_is_int(b))
            return 0;
        return net_answer(NET_arm(handle, (int) OM_int_value(b)) == 0
                          ? ST_TRUE : ST_NIL);
    }

    case NET_CMD_CONNECT: {
        char        host[256];
        int64_t     handle;
        int         index;

        if (!c_from_string(a, host, sizeof host) || !OM_is_int(b))
            return 0;
        /*  c: which of the host's addresses, from 0; nil is the first.  */
        index = OM_is_int(c) ? (int) OM_int_value(c) : 0;
        if (index < 0)
            return 0;
        handle = NET_connect(host, (int) OM_int_value(b), index);
        return net_answer(handle < 0 ? ST_NIL : OM_int_oop((st_int) handle));
    }

    case NET_CMD_ADDRESS_COUNT: {
        char        host[256];
        int         count;

        if (!c_from_string(a, host, sizeof host) || !OM_is_int(b))
            return 0;
        count = NET_address_count(host, (int) OM_int_value(b));
        return net_answer(count < 0 ? ST_NIL : OM_int_oop((st_int) count));
    }

    case NET_CMD_TLS_AVAILABLE:
        return net_answer(NET_tls_available() ? ST_TRUE : ST_FALSE);

    case NET_CMD_TLS_START: {
        int64_t handle = net_handle_arg(a);
        char    host[256];

        if (handle < 0 || !c_from_string(b, host, sizeof host))
            return 0;
        return net_answer(NET_tls_start(handle, host) == 0 ? ST_TRUE : ST_NIL);
    }

    case NET_CMD_TLS_HANDSHAKE: {
        /*  true when done; -1 or -2 to wait, as net_result spells them.  */
        int64_t handle = net_handle_arg(a);
        int     rc;

        if (handle < 0)
            return 0;
        rc = NET_tls_handshake(handle);
        return net_answer(rc == 0 ? ST_TRUE : net_result(rc));
    }

    case NET_CMD_IS_TLS: {
        int64_t handle = net_handle_arg(a);

        if (handle < 0)
            return 0;
        return net_answer(NET_is_tls(handle) ? ST_TRUE : ST_FALSE);
    }

    case NET_CMD_ENVIRONMENT: {
        char        name[256];
        const char *value;

        if (!c_from_string(a, name, sizeof name))
            return 0;
        value = NET_environment(name);
        return net_answer(value ? string_from_c(value, strlen(value)) : ST_NIL);
    }

    case NET_CMD_CONNECT_RESULT: {
        int64_t handle = net_handle_arg(a);
        int     rc;

        if (handle < 0)
            return 0;
        rc = NET_connect_result(handle);
        return net_answer(rc == 0 ? ST_TRUE
                        : rc == NET_WOULD_BLOCK ? ST_FALSE : ST_NIL);
    }

    case NET_CMD_LOCAL_PORT: {
        int64_t handle = net_handle_arg(a);
        int     port;

        if (handle < 0)
            return 0;
        port = NET_local_port(handle);
        return net_answer(port < 0 ? ST_NIL : OM_int_oop((st_int) port));
    }

    case NET_CMD_PEER_ADDRESS: {
        int64_t handle = net_handle_arg(a);
        char    text[128];

        if (handle < 0)
            return 0;
        if (NET_peer_address(handle, text, sizeof text) != 0)
            return net_answer(ST_NIL);
        return net_answer(string_from_c(text, strlen(text)));
    }

    case NET_CMD_SET_OPTION: {
        int64_t handle = net_handle_arg(a);

        if (handle < 0 || !OM_is_int(b))
            return 0;
        return net_answer(NET_set_option(handle, (int) OM_int_value(b),
                                         c == ST_TRUE) == 0
                          ? ST_TRUE : ST_NIL);
    }

    case NET_CMD_RANDOM_BYTES: {
        uint32_t    n;
        uint32_t    i;

        if (!net_is_bytes(a))
            return 0;
        n = OM_fetch_byte_length(a);
        if (n > NET_SCRATCH_BYTES)
            n = NET_SCRATCH_BYTES;
        if (NET_random_bytes(net_scratch, n) != 0)
            return net_answer(ST_NIL);
        for (i = 0; i < n; ++i)
            OM_store_byte(i, a, net_scratch[i]);
        return net_answer(ST_TRUE);
    }

    case NET_CMD_ARGUMENTS: {
        int     n = NET_argument_count();
        int     i;
        st_oop  array = OM_instantiate_pointers(ST_CLASS_ARRAY, (uint32_t) n);

        if (!OM_is_present(array))
            return 0;
        /*
         *  On the stack while the strings are made: a collection during
         *  one of those allocations sees the array only through this
         *  context, and a reference held only in C protects nothing.
         */
        ST_push(array);
        for (i = 0; i < n; ++i) {
            const char *text = NET_argument(i);
            st_oop      s    = string_from_c(text, strlen(text));

            if (!OM_is_present(s)) {
                ST_pop_n(1);
                return 0;
            }
            OM_store_pointer((uint32_t) i, array, s);
        }
        ST_pop_n(1);
        return net_answer(array);
    }

    case NET_CMD_STOP_REQUESTED:
        return net_answer(SCHED_stop_requested() ? ST_TRUE : ST_FALSE);

    case NET_CMD_OPEN_COUNT:
        return net_answer(OM_int_oop((st_int) NET_open_count()));

    default:
        return 0;
    }
#endif
}

/*  ----------  The database  ----------
 *
 *  primitive 129 -- Odbc class >> primCommand:with:with:with:
 *
 *  ONE primitive with a command number, not thirty numbered primitives, for
 *  the reason primitive 130 is one primitive for the whole file system: a
 *  subsystem that grows should grow inside its own number rather than eat
 *  its way through a space of 255 that the Blue Book, Squeak, Pharo and this
 *  system are all already spending.  Adding a command here costs a case;
 *  adding a primitive costs a number that can never be given back.
 *
 *  Four argument slots, fixed, so that the hot path -- read one column of
 *  one row -- allocates nothing to make the call.  A command needing more
 *  than three operands packs the extras into an Array in the third, which is
 *  only ever the catalogue calls and the date binders, none of them hot.
 *
 *  ARGUMENTS ARE COPIED OUT OF THE OBJECT MEMORY BEFORE THE CALL, and results
 *  are built after it.  This is not tidiness.  ST_odbc_* parks the worker for
 *  the duration of anything that can block, which lets the collector run
 *  while this thread is inside the driver -- so an OOP survives, being an
 *  object-table index, but a pointer into an object's bytes does not.  Every
 *  string below is therefore copied to the C heap or a local buffer first,
 *  and every answer is made from C values afterwards.
 */

/*  The command numbers, as Odbc's class-side methods name them.  */
enum {
    ODBC_AVAILABLE          =  0,
    ODBC_LAST_ERROR         =  1,
    ODBC_CONNECT            =  2,
    ODBC_DISCONNECT         =  3,
    ODBC_IS_CONNECTED       =  4,
    ODBC_SET_AUTOCOMMIT     =  5,
    ODBC_SET_READ_ONLY      =  6,
    ODBC_COMMIT             =  7,
    ODBC_ROLLBACK           =  8,
    ODBC_INFO_STRING        =  9,
    ODBC_SET_SCHEMA         = 10,
    ODBC_GET_SCHEMA         = 11,
    ODBC_PREPARE            = 12,
    ODBC_CLOSE_STATEMENT    = 13,
    ODBC_CLEAR_PARAMETERS   = 14,
    ODBC_BIND               = 15,
    ODBC_BIND_NULL          = 16,
    ODBC_EXECUTE            = 17,
    ODBC_EXECUTE_DIRECT     = 18,
    ODBC_FETCH              = 19,
    ODBC_ROW_COUNT          = 20,
    ODBC_COLUMN_COUNT       = 21,
    ODBC_DESCRIBE_COLUMN    = 22,
    ODBC_GET_VALUE          = 23,
    ODBC_TABLES             = 24,
    ODBC_COLUMNS            = 25,
    ODBC_PRIMARY_KEYS       = 26,
    ODBC_IMPORTED_KEYS      = 27,
    ODBC_BIND_DATE          = 28,
    ODBC_BIND_TIME          = 29,
    ODBC_BIND_TIMESTAMP     = 30
};

/*
 *  A Smalltalk byte object as a C string on the heap, or NULL.
 *
 *  On the heap rather than in a buffer because the longest thing that comes
 *  through here is SQL, and QueryBuilder writes SQL as long as the query is.
 *  A fixed buffer would work for years and then silently truncate somebody's
 *  forty-table join into a syntax error.
 *
 *  nil answers NULL with *ok set, which is how "no schema was named" reaches
 *  ODBC as the pattern that matches any -- see the note on pattern() in
 *  st_odbc.c, where an empty string would instead match nothing.
 */
static char *
odbc_string(st_oop s, int *ok)
{
    uint32_t    n;
    uint32_t    i;
    char       *text;

    *ok = 0;
    if (s == ST_NIL) {
        *ok = 1;
        return NULL;
    }
    if (!OM_is_object(s) || OM_pointer_bit(s))
        return NULL;
    n = OM_fetch_byte_length(s);
    text = malloc((size_t) n + 1);
    if (!text)
        return NULL;
    for (i = 0; i < n; ++i)
        text[i] = (char) OM_fetch_byte(i, s);
    text[n] = '\0';
    *ok = 1;
    return text;
}

/*
 *  An integer answer, whatever its size.
 *
 *  The Blue Book memory's SmallInteger holds fifteen bits, so on that build
 *  an ordinary row id is already too big and the LargePositiveInteger path
 *  is the common one rather than the exception.  A large NEGATIVE value is
 *  the one case answered as text: LargeNegativeInteger is not among the
 *  guaranteed object pointers, so the class cannot be named from here
 *  without a global lookup that would have to work in a resumed image too.
 *  The caller knows the column's type and sends asNumber, which is exact,
 *  and the case is a negative BIGINT beyond 2^62 -- rare enough that paying
 *  for it in text costs nothing real.
 */
static st_oop
odbc_integer(int64_t value)
{
    st_oop      big;
    unsigned    bytes = 0;
    uint64_t    scan;
    unsigned    i;

    if (value >= (int64_t) ST_INT_MIN && value <= (int64_t) ST_INT_MAX)
        return OM_int_oop((st_int) value);
    if (value < 0) {
        char    text[32];

        snprintf(text, sizeof text, "%lld", (long long) value);
        return string_from_c(text, strlen(text));
    }
    scan = (uint64_t) value;
    while (scan) {
        ++bytes;
        scan >>= 8;
    }
    big = OM_instantiate_bytes(ST_CLASS_LARGE_POSITIVE_INTEGER, bytes);
    if (!OM_is_present(big))
        return ST_OOP_INVALID;
    for (i = 0; i < bytes; ++i)
        OM_store_byte(i, big, (uint8_t) (((uint64_t) value >> (i * 8)) & 0xFF));
    return big;
}

/*  A SmallInteger argument, or -1 for anything else.  */
static int
odbc_int_arg(st_oop p)
{
    if (!OM_is_int(p))
        return -1;
    return (int) OM_int_value(p);
}

/*
 *  Element `index' of an Array argument, or nil.
 *
 *  Answers nil rather than failing for a short array so that the caller's
 *  own checking is the only checking: a date is three elements and a
 *  timestamp is seven, and a Smalltalk-side mistake should arrive as a
 *  refused bind with a message, not as a primitive failure whose fallback
 *  code has to guess what went wrong.
 */
static st_oop
odbc_element(st_oop array, uint32_t index)
{
    if (!OM_is_object(array) || !OM_pointer_bit(array)
     || OM_fetch_word_length(array) <= index)
        return ST_NIL;
    return OM_fetch_pointer(index, array);
}

/*  Answer, having popped the receiver and four arguments.  */
static int
odbc_answer(st_oop value)
{
    if (value == ST_OOP_INVALID)
        return 0;
    ST_pop_n(5);
    ST_push(value);
    return 1;
}

/*
 *  Bind one value, choosing the SQL type from the Smalltalk object's class.
 *
 *  Dates, times and timestamps are NOT here.  An Array of three integers is
 *  a date to a reader and an array of three integers to this code, and a
 *  guess that is right most of the time is the worst kind: it fails on the
 *  row where somebody stores three numbers in an array column.  So the
 *  caller names those explicitly, with their own commands.
 */
static int
odbc_bind_value(int statement, int index, st_oop value)
{
    if (value == ST_NIL)
        return ST_odbc_bind_null(statement, index, 0);
    if (value == ST_TRUE)
        return ST_odbc_bind_boolean(statement, index, 1);
    if (value == ST_FALSE)
        return ST_odbc_bind_boolean(statement, index, 0);
    if (OM_is_int(value))
        return ST_odbc_bind_int(statement, index,
                                (int64_t) OM_int_value(value));
    if (!OM_is_object(value))
        return -1;
    if (OM_fetch_class(value) == ST_CLASS_FLOAT) {
        double  d;

        if (!float_value(value, &d))
            return -1;
        return ST_odbc_bind_double(statement, index, d);
    }
    if (OM_fetch_class(value) == ST_CLASS_LARGE_POSITIVE_INTEGER) {
        uint32_t    n = OM_fetch_byte_length(value);
        uint64_t    v = 0;
        uint32_t    i;

        if (n > 8)                      /*  wider than the database has  */
            return -1;
        for (i = 0; i < n; ++i)
            v |= (uint64_t) OM_fetch_byte(i, value) << (i * 8);
        if (v > (uint64_t) INT64_MAX)
            return -1;
        return ST_odbc_bind_int(statement, index, (int64_t) v);
    }
    if (!OM_pointer_bit(value)) {
        /*
         *  Any other byte object: String, Symbol, ByteArray, and the
         *  LargeNegativeInteger the caller has already turned into digits.
         *  Bound as characters, which every database will convert from.
         */
        char   *text;
        int     ok;
        int     result;

        text = odbc_string(value, &ok);
        if (!ok)
            return -1;
        result = ST_odbc_bind_string(statement, index, text ? text : "",
                                     text ? strlen(text) : 0);
        free(text);
        return result;
    }
    return -1;
}

/*
 *  One column of the current row, as a Smalltalk object.
 *
 *  DECIMAL and NUMERIC arrive as their digits and are answered as a String.
 *  That is deliberate and is explained in st_odbc.h: the caller knows the
 *  column's type, sends asNumber, and gets a Fraction -- exact, where a
 *  Float would have rounded somebody's money.  Bytes are answered as a
 *  String too, for the same reason large negatives are: ByteArray is not a
 *  guaranteed object pointer, and asByteArray on the Smalltalk side is one
 *  send against a global lookup here that would have to survive a snapshot.
 */
static st_oop
odbc_value_object(const st_odbc_value *value)
{
    st_oop  array;

    switch (value->kind) {
    case ST_ODBC_NULL:
        return ST_NIL;
    case ST_ODBC_BOOLEAN:
        return value->i ? ST_TRUE : ST_FALSE;
    case ST_ODBC_INT:
        return odbc_integer(value->i);
    case ST_ODBC_DOUBLE:
        return make_float(value->d);
    case ST_ODBC_STRING:
    case ST_ODBC_DECIMAL:
    case ST_ODBC_BYTES:
        return string_from_c(value->text, value->length);
    case ST_ODBC_DATE:
        array = OM_instantiate_pointers(ST_CLASS_ARRAY, 3);
        if (!OM_is_present(array))
            return ST_OOP_INVALID;
        OM_store_pointer(0, array, OM_int_oop((st_int) value->year));
        OM_store_pointer(1, array, OM_int_oop((st_int) value->month));
        OM_store_pointer(2, array, OM_int_oop((st_int) value->day));
        return array;
    case ST_ODBC_TIME:
        array = OM_instantiate_pointers(ST_CLASS_ARRAY, 4);
        if (!OM_is_present(array))
            return ST_OOP_INVALID;
        OM_store_pointer(0, array, OM_int_oop((st_int) value->hour));
        OM_store_pointer(1, array, OM_int_oop((st_int) value->minute));
        OM_store_pointer(2, array, OM_int_oop((st_int) value->second));
        OM_store_pointer(3, array, odbc_integer(value->nanosecond));
        return array;
    case ST_ODBC_TIMESTAMP:
        array = OM_instantiate_pointers(ST_CLASS_ARRAY, 7);
        if (!OM_is_present(array))
            return ST_OOP_INVALID;
        OM_store_pointer(0, array, OM_int_oop((st_int) value->year));
        OM_store_pointer(1, array, OM_int_oop((st_int) value->month));
        OM_store_pointer(2, array, OM_int_oop((st_int) value->day));
        OM_store_pointer(3, array, OM_int_oop((st_int) value->hour));
        OM_store_pointer(4, array, OM_int_oop((st_int) value->minute));
        OM_store_pointer(5, array, OM_int_oop((st_int) value->second));
        OM_store_pointer(6, array, odbc_integer(value->nanosecond));
        return array;
    }
    return ST_NIL;
}

static int
primitive_odbc_command(void)
{
    st_oop  c   = ST_stack_value(0);
    st_oop  b   = ST_stack_value(1);
    st_oop  a   = ST_stack_value(2);
    st_oop  cmd = ST_stack_value(3);
    long    command;

    if (!OM_is_int(cmd))
        return 0;
    command = (long) OM_int_value(cmd);

    switch (command) {

    case ODBC_AVAILABLE:
        return odbc_answer(ST_odbc_available() ? ST_TRUE : ST_FALSE);

    case ODBC_LAST_ERROR: {
        const char *text = ST_odbc_last_error();

        return odbc_answer(string_from_c(text, strlen(text)));
    }

    case ODBC_CONNECT: {
        char   *text;
        int     ok;
        int     handle;

        text = odbc_string(a, &ok);
        if (!ok || !text) {
            free(text);
            return 0;
        }
        handle = ST_odbc_connect(text);
        free(text);
        return odbc_answer(handle < 0 ? ST_NIL : OM_int_oop((st_int) handle));
    }

    case ODBC_DISCONNECT:
        return odbc_answer(ST_odbc_disconnect(odbc_int_arg(a)) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_IS_CONNECTED:
        return odbc_answer(ST_odbc_is_connected(odbc_int_arg(a))
                           ? ST_TRUE : ST_FALSE);

    case ODBC_SET_AUTOCOMMIT:
        return odbc_answer(ST_odbc_set_autocommit(odbc_int_arg(a),
                                                  b == ST_TRUE) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_SET_READ_ONLY:
        return odbc_answer(ST_odbc_set_read_only(odbc_int_arg(a),
                                                 b == ST_TRUE) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_COMMIT:
        return odbc_answer(ST_odbc_commit(odbc_int_arg(a)) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_ROLLBACK:
        return odbc_answer(ST_odbc_rollback(odbc_int_arg(a)) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_INFO_STRING: {
        char    text[512];

        if (ST_odbc_info_string(odbc_int_arg(a), odbc_int_arg(b),
                                text, sizeof text) != 0)
            return odbc_answer(ST_NIL);
        return odbc_answer(string_from_c(text, strlen(text)));
    }

    case ODBC_SET_SCHEMA: {
        char   *text;
        int     ok;
        int     result;

        text = odbc_string(b, &ok);
        if (!ok)
            return 0;
        result = ST_odbc_set_schema(odbc_int_arg(a), text ? text : "");
        free(text);
        return odbc_answer(result == 0 ? ST_TRUE : ST_FALSE);
    }

    case ODBC_GET_SCHEMA: {
        char    text[512];

        if (ST_odbc_get_schema(odbc_int_arg(a), text, sizeof text) != 0)
            return odbc_answer(ST_NIL);
        return odbc_answer(string_from_c(text, strlen(text)));
    }

    case ODBC_PREPARE: {
        char   *sql;
        int     ok;
        int     handle;

        sql = odbc_string(b, &ok);
        if (!ok || !sql) {
            free(sql);
            return 0;
        }
        handle = ST_odbc_prepare(odbc_int_arg(a), sql);
        free(sql);
        return odbc_answer(handle < 0 ? ST_NIL : OM_int_oop((st_int) handle));
    }

    case ODBC_CLOSE_STATEMENT:
        return odbc_answer(ST_odbc_close_statement(odbc_int_arg(a)) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_CLEAR_PARAMETERS:
        return odbc_answer(ST_odbc_clear_parameters(odbc_int_arg(a)) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_BIND:
        return odbc_answer(odbc_bind_value(odbc_int_arg(a), odbc_int_arg(b),
                                           c) == 0 ? ST_TRUE : ST_FALSE);

    case ODBC_BIND_NULL:
        return odbc_answer(ST_odbc_bind_null(odbc_int_arg(a), odbc_int_arg(b),
                                             OM_is_int(c)
                                             ? (int) OM_int_value(c) : 0) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_BIND_DATE:
        return odbc_answer(ST_odbc_bind_date(odbc_int_arg(a), odbc_int_arg(b),
                                             odbc_int_arg(odbc_element(c, 0)),
                                             odbc_int_arg(odbc_element(c, 1)),
                                             odbc_int_arg(odbc_element(c, 2)))
                           == 0 ? ST_TRUE : ST_FALSE);

    case ODBC_BIND_TIME:
        return odbc_answer(ST_odbc_bind_time(odbc_int_arg(a), odbc_int_arg(b),
                                             odbc_int_arg(odbc_element(c, 0)),
                                             odbc_int_arg(odbc_element(c, 1)),
                                             odbc_int_arg(odbc_element(c, 2)),
                                             0) == 0 ? ST_TRUE : ST_FALSE);

    case ODBC_BIND_TIMESTAMP:
        return odbc_answer(ST_odbc_bind_timestamp(
                               odbc_int_arg(a), odbc_int_arg(b),
                               odbc_int_arg(odbc_element(c, 0)),
                               odbc_int_arg(odbc_element(c, 1)),
                               odbc_int_arg(odbc_element(c, 2)),
                               odbc_int_arg(odbc_element(c, 3)),
                               odbc_int_arg(odbc_element(c, 4)),
                               odbc_int_arg(odbc_element(c, 5)),
                               (uint32_t) odbc_int_arg(odbc_element(c, 6)))
                           == 0 ? ST_TRUE : ST_FALSE);

    case ODBC_EXECUTE:
        return odbc_answer(ST_odbc_execute(odbc_int_arg(a)) == 0
                           ? ST_TRUE : ST_FALSE);

    case ODBC_EXECUTE_DIRECT: {
        char   *sql;
        int     ok;
        int64_t rows = 0;
        int     result;

        sql = odbc_string(b, &ok);
        if (!ok || !sql) {
            free(sql);
            return 0;
        }
        result = ST_odbc_execute_direct(odbc_int_arg(a), sql, &rows);
        free(sql);
        return odbc_answer(result == 0 ? odbc_integer(rows) : ST_NIL);
    }

    case ODBC_FETCH: {
        int     got = ST_odbc_fetch(odbc_int_arg(a));

        /*
         *  Three answers, not two.  A row, the end of the result set, and a
         *  failure are three different pieces of news, and a caller shown
         *  false for the last two treats a broken connection as an empty
         *  table -- which is a report that runs, and is wrong.
         */
        return odbc_answer(got < 0 ? ST_NIL : got ? ST_TRUE : ST_FALSE);
    }

    case ODBC_ROW_COUNT: {
        int64_t rows = 0;

        if (ST_odbc_row_count(odbc_int_arg(a), &rows) != 0)
            return odbc_answer(ST_NIL);
        return odbc_answer(odbc_integer(rows));
    }

    case ODBC_COLUMN_COUNT: {
        int     count = ST_odbc_column_count(odbc_int_arg(a));

        return odbc_answer(count < 0 ? ST_NIL : OM_int_oop((st_int) count));
    }

    case ODBC_DESCRIBE_COLUMN: {
        char        name[128];
        int         sql_type = 0;
        int64_t     size = 0;
        int         digits = 0;
        int         nullable = 0;
        st_oop      array;
        st_oop      name_string;

        if (ST_odbc_describe_column(odbc_int_arg(a), odbc_int_arg(b),
                                    name, sizeof name, &sql_type, &size,
                                    &digits, &nullable) != 0)
            return odbc_answer(ST_NIL);
        /*  The Array first, and on the stack, before the String it holds:
         *  the second allocation can collect, and only a root keeps the
         *  first one alive through it.  See command 3 of the directory
         *  primitive.  */
        array = OM_instantiate_pointers(ST_CLASS_ARRAY, 5);
        if (!OM_is_present(array))
            return 0;
        ST_push(array);
        name_string = string_from_c(name, strlen(name));
        ST_pop_n(1);
        if (!OM_is_present(name_string))
            return 0;
        OM_store_pointer(0, array, name_string);
        OM_store_pointer(1, array, OM_int_oop((st_int) sql_type));
        OM_store_pointer(2, array, odbc_integer(size));
        OM_store_pointer(3, array, OM_int_oop((st_int) digits));
        OM_store_pointer(4, array, OM_int_oop((st_int) nullable));
        return odbc_answer(array);
    }

    case ODBC_GET_VALUE: {
        st_odbc_value   value;

        if (ST_odbc_get(odbc_int_arg(a), odbc_int_arg(b), &value) != 0)
            return 0;                   /*  the fallback code raises  */
        return odbc_answer(odbc_value_object(&value));
    }

    case ODBC_TABLES:
    case ODBC_COLUMNS:
    case ODBC_PRIMARY_KEYS:
    case ODBC_IMPORTED_KEYS: {
        char   *schema;
        char   *first;
        char   *second = NULL;
        int     ok;
        int     handle;
        int     connection = odbc_int_arg(a);

        schema = odbc_string(b, &ok);
        if (!ok)
            return 0;
        first = odbc_string(odbc_element(c, 0), &ok);
        if (!ok) {
            free(schema);
            return 0;
        }
        if (command == ODBC_TABLES || command == ODBC_COLUMNS) {
            second = odbc_string(odbc_element(c, 1), &ok);
            if (!ok) {
                free(schema);
                free(first);
                return 0;
            }
        }
        handle = command == ODBC_TABLES
                     ? ST_odbc_tables(connection, schema, first, second)
               : command == ODBC_COLUMNS
                     ? ST_odbc_columns(connection, schema, first, second)
               : command == ODBC_PRIMARY_KEYS
                     ? ST_odbc_primary_keys(connection, schema, first)
                     : ST_odbc_imported_keys(connection, schema, first);
        free(schema);
        free(first);
        free(second);
        return odbc_answer(handle < 0 ? ST_NIL
                                      : OM_int_oop((st_int) handle));
    }

    default:
        break;
    }
    return 0;
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
    case 249: return primitive_elements_forward_identity();
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
    case 167: return SCHED_primitive_yield();
    case 86:  return SCHED_primitive_wait();
    case 87:  return SCHED_primitive_resume();
    case 88:  return SCHED_primitive_suspend();
    case 89:  return 1;         /*  flushCache: we keep no method cache yet  */
    case 90:  return primitive_mouse_point();
    case 91:  return primitive_cursor_loc_put();
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
    case 195: return primitive_find_next_unwind_up_to();
    case 197: return primitive_find_next_handler();
    case 246: return primitive_context_return();
    case 248: return primitive_report_on_standard_error();
    case 241: return primitive_active_process();
    case 243: return primitive_active_worker_index();
    case 244: return primitive_worker_count();
    case 245: return primitive_compare_and_swap_slot();
    case 251: return primitive_context_restart();
    case 252: return primitive_remove_ready_process();
    case 253: return primitive_first_ready_process_at();
    case 250: return primitive_full_collect();
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
    case 207: {
        /*
         *  BlockClosure>>asContext.  The one thing forking needs that
         *  cannot be said in Smalltalk: a context for this closure, with a
         *  nil sender, that Process>>forContext: can resume.
         */
        st_oop  ctx = ST_closure_as_context(ST_stack_top());

        if (!OM_is_object(ctx))
            return 0;
        ST_pop_n(1);
        ST_push(ctx);
        return 1;
    }
    case 221: return primitive_closure_value(0);
    case 222: return primitive_closure_value(1);
    case ST_PRIMITIVE_STRING_HASH: return primitive_string_hash();

    /*
     *  What Pharo's Kernel names.  See doc/PHARO-INTAKE.md for the ones
     *  deliberately absent and why.
     */
    case  55: return primitive_float_sqrt();
    case  56: return primitive_float_sin();
    case  57: return primitive_float_arc_tan();
    case  58: return primitive_float_ln();
    case  59: return primitive_float_exp();
    case 224: return primitive_float_cos();
    case 225: return primitive_float_tan();
    case 226: return primitive_float_arc_sin();
    case 227: return primitive_float_arc_cos();
    case 229: return primitive_float_raised_to();
    case 228: return primitive_compile_method();
    /*  Told apart by arity; see primitive_last_error.  */
    case 132: return st_vm.argument_count == 0
                     ? primitive_last_error()
                     : primitive_inst_vars_include();
    case 97:  return primitive_snapshot();
    case 128: return primitive_be_snapshot_file();
    case 130: return primitive_file_command();
    case 131: return primitive_directory_command();
    case 129: return primitive_odbc_command();
    case 208: return primitive_net_command();
    case 209: return primitive_crypto_command();
    case 133: return primitive_error_string();
    case 254: return primitive_native_line_end();
    case 100: return primitive_signal_at_milliseconds();
    case 135: return primitive_millisecond_clock();
    case 255: return primitive_float_print_string();
    case 136: return primitive_signal_at_time();
    case 148: return primitive_shallow_copy();
    case 159: return primitive_hash_multiply();
    case 163: return primitive_answer_false_of_receiver();  /*  isReadOnly  */
    case 164: return primitive_set_flag_false_only();
    case 168: return primitive_copy_from();
    case 169: return primitive_not_equivalent();
    case 170: return primitive_character_value();
    case 171: return primitive_character_as_integer();
    /*  173 and 174 are 73 and 74 by another number in Squeak.  */
    case 173: return primitive_inst_var_at();
    case 174: return primitive_inst_var_at_put();
    case 183: return primitive_answer_false_of_receiver();  /*  isPinned  */
    case 184: return primitive_set_flag_false_only();
    case 188: return primitive_execute_method();
    case 230: return primitive_relinquish_processor();
    case 242: return primitive_wheel_delta();
    case 210: return primitive_clipboard();
    case 240: return primitive_utc_microsecond_clock();

    case 110: return primitive_equivalent();
    case 111: return primitive_class();
    default:  return 0;
    }
}

/*
 *  ----------  The table, beside the switch  ----------
 *
 *  This says the same thing the dispatch switch above says, in a form a
 *  report can read.  It is maintained by hand and it sits here, immediately
 *  after the switch, because the only thing keeping the two in step is that
 *  they are impossible to read apart -- a table in another file would drift
 *  within a month.
 *
 *  test_primitives walks 1..255 and checks that every number the table
 *  claims is one the compiler will accept and that the four deliberate
 *  cases are still the four deliberate cases, which catches the edits that
 *  matter without pretending to catch every one.
 */
typedef struct {
    unsigned            number;
    st_primitive_status status;
    const char         *name;
} primitive_entry;

static const primitive_entry primitive_table[] = {
    {   1, ST_PRIM_PRESENT,  "SmallInteger +"                   },
    {   2, ST_PRIM_PRESENT,  "SmallInteger -"                   },
    {   3, ST_PRIM_PRESENT,  "SmallInteger <"                   },
    {   4, ST_PRIM_PRESENT,  "SmallInteger >"                   },
    {   5, ST_PRIM_PRESENT,  "SmallInteger <="                  },
    {   6, ST_PRIM_PRESENT,  "SmallInteger >="                  },
    {   7, ST_PRIM_PRESENT,  "SmallInteger ="                   },
    {   8, ST_PRIM_PRESENT,  "SmallInteger ~="                  },
    {   9, ST_PRIM_PRESENT,  "SmallInteger *"                   },
    {  10, ST_PRIM_PRESENT,  "SmallInteger /"                   },
    {  11, ST_PRIM_PRESENT,  "SmallInteger \\\\"                  },
    {  12, ST_PRIM_PRESENT,  "SmallInteger //"                  },
    {  13, ST_PRIM_PRESENT,  "SmallInteger quo:"                },
    {  14, ST_PRIM_PRESENT,  "SmallInteger bitAnd:"             },
    {  15, ST_PRIM_PRESENT,  "SmallInteger bitOr:"              },
    {  16, ST_PRIM_PRESENT,  "SmallInteger bitXor:"             },
    {  17, ST_PRIM_PRESENT,  "SmallInteger bitShift:"           },
    {  18, ST_PRIM_PRESENT,  "Number @"                         },
    {  40, ST_PRIM_PRESENT,  "SmallInteger asFloat"             },
    {  41, ST_PRIM_PRESENT,  "Float +"                          },
    {  42, ST_PRIM_PRESENT,  "Float -"                          },
    {  43, ST_PRIM_PRESENT,  "Float <"                          },
    {  44, ST_PRIM_PRESENT,  "Float >"                          },
    {  45, ST_PRIM_PRESENT,  "Float <="                         },
    {  46, ST_PRIM_PRESENT,  "Float >="                         },
    {  47, ST_PRIM_PRESENT,  "Float ="                          },
    {  48, ST_PRIM_PRESENT,  "Float ~="                         },
    {  49, ST_PRIM_PRESENT,  "Float *"                          },
    {  50, ST_PRIM_PRESENT,  "Float /"                          },
    {  51, ST_PRIM_PRESENT,  "Float truncated"                  },
    {  52, ST_PRIM_PRESENT,  "Float fractionPart"               },
    {  53, ST_PRIM_PRESENT,  "Float exponent"                   },
    {  54, ST_PRIM_PRESENT,  "Float timesTwoPower:"             },
    {  60, ST_PRIM_PRESENT,  "Object at:"                       },
    {  61, ST_PRIM_PRESENT,  "Object at:put:"                   },
    {  62, ST_PRIM_PRESENT,  "Object size"                      },
    {  63, ST_PRIM_PRESENT,  "String at:"                       },
    {  64, ST_PRIM_PRESENT,  "String at:put:"                   },
    {  68, ST_PRIM_PRESENT,  "CompiledMethod objectAt:"         },
    {  69, ST_PRIM_PRESENT,  "CompiledMethod objectAt:put:"     },
    {  70, ST_PRIM_PRESENT,  "Behavior new"                     },
    {  71, ST_PRIM_PRESENT,  "Behavior new:"                    },
    {  72, ST_PRIM_PRESENT,  "Object become:"                   },
    { 249, ST_PRIM_PRESENT,  "Array elementsForwardIdentityTo:" },
    {  73, ST_PRIM_PRESENT,  "Object instVarAt:"                },
    {  74, ST_PRIM_PRESENT,  "Object instVarAt:put:"            },
    {  75, ST_PRIM_PRESENT,  "Object identityHash"              },
    {  77, ST_PRIM_PRESENT,  "Behavior someInstance"            },
    {  78, ST_PRIM_PRESENT,  "Object nextInstance"              },
    {  79, ST_PRIM_PRESENT,  "Behavior newMethod:header:"       },
    {  80, ST_PRIM_PRESENT,  "ContextPart blockCopy:"           },
    {  81, ST_PRIM_PRESENT,  "BlockContext value"               },
    {  82, ST_PRIM_PRESENT,  "BlockContext valueWithArguments:" },
    {  83, ST_PRIM_PRESENT,  "Object perform:"                  },
    {  84, ST_PRIM_PRESENT,  "Object perform:withArguments:"    },
    {  85, ST_PRIM_PRESENT,  "Semaphore signal"                 },
    {  86, ST_PRIM_PRESENT,  "Semaphore wait"                   },
    {  87, ST_PRIM_PRESENT,  "Process resume"                   },
    {  88, ST_PRIM_PRESENT,  "Process suspend"                  },
    {  89, ST_PRIM_ACCEPTED, "flushCache -- there is no method cache yet" },
    {  90, ST_PRIM_PRESENT,  "InputSensor mousePoint"           },
    {  91, ST_PRIM_ACCEPTED, "cursorLocPut: -- the host owns the pointer" },
    {  92, ST_PRIM_ACCEPTED, "cursorLink:"                      },
    {  93, ST_PRIM_PRESENT,  "InputSensor primInputSemaphore:"  },
    {  94, ST_PRIM_ACCEPTED, "sampleInterval:"                  },
    {  95, ST_PRIM_PRESENT,  "InputSensor inputWord"            },
    {  96, ST_PRIM_PRESENT,  "BitBlt copyBits"                  },
    {  98, ST_PRIM_PRESENT,  "Time primSecondsClockInto:"       },
    {  99, ST_PRIM_PRESENT,  "Time primMillisecondClockInto:"   },
    { 101, ST_PRIM_PRESENT,  "Cursor beCursor"                  },
    { 102, ST_PRIM_PRESENT,  "DisplayScreen beDisplay"          },
    { 105, ST_PRIM_PRESENT,  "String replaceFrom:to:with:startingAt:" },
    { 110, ST_PRIM_PRESENT,  "Object =="                        },
    { 111, ST_PRIM_PRESENT,  "Object class"                     },
    { 112, ST_PRIM_PRESENT,  "SystemDictionary coreLeft"        },
    { 113, ST_PRIM_PRESENT,  "SystemDictionary quitPrimitive"   },
    { 115, ST_PRIM_PRESENT,  "SystemDictionary oopsLeft"        },
    { 116, ST_PRIM_ACCEPTED, "signal:atOopsLeft:wordsLeft: -- no low-space "
                             "warning yet"                      },
    {  55, ST_PRIM_PRESENT,  "Float sqrt"                       },
    {  56, ST_PRIM_PRESENT,  "Float sin"                        },
    {  57, ST_PRIM_PRESENT,  "Float arcTan"                     },
    {  58, ST_PRIM_PRESENT,  "Float ln"                         },
    {  59, ST_PRIM_PRESENT,  "Float exp"                        },
    { 224, ST_PRIM_PRESENT,  "Float cos -- ours; no Blue Book number" },
    { 225, ST_PRIM_PRESENT,  "Float tan -- ours; no Blue Book number" },
    { 226, ST_PRIM_PRESENT,  "Float arcSin -- ours; no Blue Book number" },
    { 227, ST_PRIM_PRESENT,  "Float arcCos -- ours; no Blue Book number" },
    { 229, ST_PRIM_PRESENT,  "Float raisedTo: -- ours; no Blue Book number" },
    { 228, ST_PRIM_PRESENT,  "Compiler class compile -- ours"    },
    { 132, ST_PRIM_PRESENT,  "Object instVarsInclude:"          },
    { 100, ST_PRIM_PRESENT,  "signal a semaphore at a time"     },
    { 135, ST_PRIM_PRESENT,  "millisecond clock"                },
    { 254, ST_PRIM_PRESENT,  "FileStream class nativeLineEnd -- ours" },
    { 129, ST_PRIM_PRESENT,  "Odbc primCommand:with:with:with: -- ours" },
    { 208, ST_PRIM_PRESENT,  "Socket primCommand:with:with:with: -- ours" },
    { 255, ST_PRIM_PRESENT,  "Float>>printString"               },
    { 136, ST_PRIM_PRESENT,  "signal a semaphore at a time"     },
    { 148, ST_PRIM_PRESENT,  "Object shallowCopy / clone"       },
    { 159, ST_PRIM_PRESENT,  "Integer hashMultiply"             },
    { 167, ST_PRIM_PRESENT,  "ProcessorScheduler yield -- Squeak's number" },
    { 163, ST_PRIM_PRESENT,  "Object isReadOnly -- always false here" },
    { 164, ST_PRIM_PRESENT,  "Object setIsReadOnly: -- false only"    },
    { 168, ST_PRIM_PRESENT,  "Object copyFrom:"                 },
    { 169, ST_PRIM_PRESENT,  "Object ~~"                        },
    { 170, ST_PRIM_PRESENT,  "Character class value:"           },
    { 171, ST_PRIM_PRESENT,  "Character asInteger"              },
    { 173, ST_PRIM_PRESENT,  "Object instVarAt: -- 73 renumbered" },
    { 174, ST_PRIM_PRESENT,  "Object instVarAt:put: -- 74 renumbered" },
    { 183, ST_PRIM_PRESENT,  "Object isPinnedInMemory -- always false here" },
    { 184, ST_PRIM_PRESENT,  "Object setPinnedInMemory: -- false only" },
    { 188, ST_PRIM_PRESENT,  "Object withArgs:executeMethod: -- Pharo's number" },
    { 195, ST_PRIM_PRESENT,  "ContextPart findNextUnwindContextUpTo:" },
    { 197, ST_PRIM_PRESENT,  "ContextPart findNextHandlerContext"     },
    { 198, ST_PRIM_TAG,      "unwind mark -- ensure:/ifCurtailed:, read by "
                             "a walk up the sender chain"       },
    { 199, ST_PRIM_TAG,      "handler mark -- on:do:, read by a walk up "
                             "the sender chain"                 },
    { 201, ST_PRIM_PRESENT,  "BlockClosure value"               },
    { 202, ST_PRIM_PRESENT,  "BlockClosure value:"              },
    { 203, ST_PRIM_PRESENT,  "BlockClosure value:value:"        },
    { 204, ST_PRIM_PRESENT,  "BlockClosure value:value:value:"  },
    { 205, ST_PRIM_PRESENT,  "BlockClosure value:value:value:value:" },
    { 206, ST_PRIM_PRESENT,  "BlockClosure valueWithArguments:" },
    { 221, ST_PRIM_PRESENT,  "BlockClosure valueNoContextSwitch" },
    { 222, ST_PRIM_PRESENT,  "BlockClosure valueNoContextSwitch:" },
    { ST_PRIMITIVE_STRING_HASH, ST_PRIM_PRESENT,
                             "String hash, over every byte -- ours"   },
    { 230, ST_PRIM_PRESENT,  "ProcessorScheduler class "
                             "relinquishProcessorForMicroseconds:" },
    { 240, ST_PRIM_PRESENT,  "UTC microsecond clock"            },
    { 241, ST_PRIM_PRESENT,  "Processor activeProcess -- this worker's" },
    { 242, ST_PRIM_PRESENT,  "InputSensor wheelDelta -- this system's own" },
    { 210, ST_PRIM_PRESENT,  "Clipboard -- the system's, this system's own" },
    { 243, ST_PRIM_PRESENT,  "Processor activeWorkerIndex -- our own"    },
    { 244, ST_PRIM_PRESENT,  "Processor workerCount -- our own"          },
    { 245, ST_PRIM_PRESENT,  "Object compareAndSwapSlot:from:to: -- ours" },
    { 246, ST_PRIM_PRESENT,  "ContextPart return: -- this system's own"  },
    { 247, ST_PRIM_PRESENT,  "ContextPart resume: -- this system's own"  },
    { 248, ST_PRIM_PRESENT,  "Object reportOnStandardError -- this "
                             "system's own"                     },
    { 251, ST_PRIM_PRESENT,  "ContextPart restartAndJump -- this system's "
                             "own"                              },
    { 252, ST_PRIM_PRESENT,  "Processor primRemoveReadyProcess: -- ours" },
    { 253, ST_PRIM_PRESENT,  "Processor primFirstReadyProcessAt: -- ours" },
    { 250, ST_PRIM_PRESENT,  "SystemDictionary garbageCollect -- this "
                             "system's own"                     }
};

st_primitive_status
ST_primitive_status_of(unsigned index, const char **name)
{
    size_t  i;

    for (i = 0; i < sizeof primitive_table / sizeof primitive_table[0]; ++i) {
        if (primitive_table[i].number == index) {
            if (name)
                *name = primitive_table[i].name;
            return primitive_table[i].status;
        }
    }
    if (name)
        *name = NULL;
    return ST_PRIM_ABSENT;
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

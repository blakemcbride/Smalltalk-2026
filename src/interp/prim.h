/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Primitive methods: Blue Book Chapter 29.
 *
 *  There are three ways into a primitive, and a VM must implement all of
 *  them separately:
 *
 *      1.  A numbered primitive named by a method's header extension.
 *      2.  The arithmetic and special-selector bytecodes 176 to 207, which
 *          run a primitive with no method lookup and no primitive index.
 *      3.  The quick returns, flag values 5 and 6 in a method header, which
 *          the interpreter handles without entering this file at all.
 *
 *  A primitive that succeeds pops its arguments and pushes its result, and
 *  returns 1.  A primitive that fails must leave the stack exactly as it
 *  found it and return 0, whereupon the interpreter runs the method's
 *  Smalltalk body instead.  That fallback is not an error path: it is how
 *  SmallInteger arithmetic promotes to LargePositiveInteger on overflow.
 */

#ifndef ST_PRIM_H
#define ST_PRIM_H

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

/*  Run a numbered primitive.  Returns 1 if it answered, 0 if it failed.  */
int     ST_primitive_dispatch(unsigned index);

/*
 *  Run the primitive implied by a special-selector bytecode (176 to 207).
 *  Returns 1 if it answered, in which case no send takes place.
 */
int     ST_special_selector_primitive(uint8_t code);

/*
 *  ----------  What this VM actually implements  ----------
 *
 *  A porting effort's first question about a body of Smalltalk source is
 *  which primitives it names that this VM does not answer, and that
 *  question is only useful if the answer distinguishes four cases rather
 *  than two.  A primitive that is ACCEPTED and does nothing is not the same
 *  as one that is implemented: the image's fallback code never runs, so a
 *  method relying on the effect fails silently instead of loudly.  And a
 *  primitive that is deliberately absent -- 198 and 199 are LABELS, read by
 *  a walk up the sender chain, and would break the exception system if they
 *  ever started succeeding -- must never appear on a list of things to go
 *  and implement.
 */
typedef enum {
    ST_PRIM_ABSENT = 0,     /*  not implemented; the method body runs      */
    ST_PRIM_PRESENT,        /*  implemented                                */
    ST_PRIM_ACCEPTED,       /*  succeeds and does nothing                  */
    ST_PRIM_TAG             /*  must fail: it is a mark, not an operation  */
} st_primitive_status;

/*
 *  What this VM does with a primitive number.  `name` is filled with a
 *  short description when it is not NULL, and left alone for ST_PRIM_ABSENT.
 */
st_primitive_status ST_primitive_status_of(unsigned index, const char **name);

/*  Report a non-boolean where the compiler guaranteed one.  */
void    ST_must_be_boolean(st_oop value);

/*  Stack helpers shared with the interpreter.  */
st_oop  ST_stack_value(uint32_t from_top);
st_oop  ST_stack_top(void);
void    ST_stack_put(uint32_t from_top, st_oop value);
void    ST_push(st_oop value);
st_oop  ST_pop(void);
void    ST_pop_n(uint32_t n);
void    ST_unpop(uint32_t n);
void    ST_send_selector(st_oop selector, uint32_t argc);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_PRIM_H  */

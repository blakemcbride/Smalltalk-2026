/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The object memory interface.
 *
 *  Exactly one implementation is compiled into any given build, selected by
 *  the OM= make variable, and it is pulled in here as a header full of
 *  static inline accessors rather than reached through function pointers.
 *  The interpreter therefore compiles against one fully inlined object
 *  memory with no indirect calls in the hot path, yet exists in only one
 *  copy of the source.
 *
 *      OM=bb   src/om/om_bb.h   16-bit Blue Book, the validation harness
 *      OM=mt   src/om/om_mt.h   64-bit threaded, the real system
 *
 *  That is the central trick of this project.  The Blue Book build loads the
 *  1983 Xerox image and must reproduce Xerox's own execution traces byte for
 *  byte; because the interpreter source is shared, passing that gate
 *  validates the interpreter the parallel system runs.
 */

#ifndef ST_OM_H
#define ST_OM_H

#if defined(ST_OM_BB)
#include "om_bb.h"
#elif defined(ST_OM_MT)
#include "om_mt.h"
#else
#error "No object memory selected: build with -DST_OM_BB or -DST_OM_MT"
#endif

/*
 *  Is there really an object here?
 *
 *  OM_is_object answers whether a pointer denotes a live object, and nil is
 *  a live object -- so it answers yes for nil.  Almost every caller means
 *  something narrower: an empty slot in Smalltalk holds nil, so "absent" and
 *  "not an object" are different questions.  Confusing the two let a failed
 *  method lookup return nil and be executed as a method, and let a return
 *  off the bottom of the stack dereference nil.  Use this wherever nil means
 *  absent.
 */
static inline int
OM_is_present(st_oop p)
{
    return p != ST_NIL && OM_is_object(p);
}

#endif  /*  ST_OM_H  */

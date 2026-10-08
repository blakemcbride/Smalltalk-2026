/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Compiling inside a running image.
 *
 *  The compiler in compiler.c needs no object memory of its own: everything
 *  it builds, it builds through the factories in an st_compile_context, and
 *  everything it resolves, it resolves through that context's lookup_global.
 *  The BOOTSTRAP supplies a context that reads its own C tables, which is
 *  right while the image is being made and wrong afterwards -- a class
 *  defined at run time is in the image's Smalltalk and not in those tables.
 *
 *  This is the other context: the one that reads the IMAGE.  It is what lets
 *  the Browser, TonelReader and Compiler>>evaluate: reach the same code
 *  generator the image was built with, instead of the second one written in
 *  Smalltalk that means something else by the same source.  See the comment
 *  at the head of image_compile.c for what that difference cost.
 */

#ifndef ST_IMAGE_COMPILE_H
#define ST_IMAGE_COMPILE_H

#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Compile source for a class that is in the image.
 *
 *    source              a String
 *    class_oop           the Behavior the method is for, or nil for a doIt
 *                        with no class context
 *    ivar_names          an Array of Strings -- the class's allInstVarNames,
 *                        computed by the image so that there is one
 *                        definition of what is in scope and not two
 *    class_association   an Association whose value is class_oop, for a
 *                        super send, or nil
 *    no_pattern          non-zero for a doIt: no pattern line, and the last
 *                        statement's value is the answer
 *
 *  THE METHOD ANSWERED IS HELD IN C ALONE until the caller stores it, so
 *  the caller must allocate nothing before it does -- make the Array it
 *  goes into first, and push that.  The literals the compile builds on the
 *  way are the compiler's own concern: it roots the compile in flight
 *  (COMPILE_visit_roots), which is what the 256-slot guard Array this once
 *  took did for the first 256 of them and not the rest (Bugs6 COMP-2).
 *
 *  THE CALLER MUST HOLD THE IMAGE'S SYMBOL LOCK.  Interning goes through the
 *  bootstrap's table, which is not itself guarded, and the image's own
 *  Symbol class>>intern: holds LibraryLocks' Symbol mutex -- so the two are
 *  serialised only if this side takes the same one.  Two workers interning
 *  the same new selector at once make two Symbols, and a Symbol that is not
 *  identical to itself cannot be found as a selector.
 *
 *  Answers 0 on success with out->method set, -1 on failure with
 *  out->error, out->error_line and out->error_offset set.
 */
int     IMGC_compile(st_oop source, st_oop class_oop, st_oop ivar_names,
                     st_oop class_association, int no_pattern, int dialect,
                     st_compile_result *out);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_IMAGE_COMPILE_H  */

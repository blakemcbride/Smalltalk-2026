/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Primitive 234: move a class's instances to a new shape while the world
 *  is stopped.
 *
 *  Its own file rather than another thousand lines in prim.c because the
 *  whole of it is one argument -- see migrate.c -- and because the fault it
 *  answers is not a primitive's ordinary business but the object memory's:
 *  a reshape that is a sequence of stores where it has to be one act.
 */

#ifndef ST_MIGRATE_H
#define ST_MIGRATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Primitive 234, dispatched from prim.c.
 *
 *  Answers 1 having popped the four arguments and left the receiver as the
 *  answer, or 0 having touched nothing at all.
 */
int ST_prim_migrate_instances(void);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_MIGRATE_H  */

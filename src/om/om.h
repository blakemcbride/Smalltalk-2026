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

/*
 *  VM state a snapshot has to carry.
 *
 *  Two of the VM's connections to the image are held in C, not in any
 *  object's instance variable: the semaphore primitive 93 installed for
 *  input, and the Form primitive 102 made the display.  Objects are all a
 *  snapshot stores, so both connections were silently dropped by a save and
 *  reload, and the image came back up with no way to be told about a key or
 *  a mouse button -- the events were queued and the semaphore they signalled
 *  was nobody's.
 *
 *  Smalltalk-80 reconnects by sending Smalltalk install on resume, which is
 *  what SystemDictionary>>install exists for; but that is the image putting
 *  the VM back together, and it cannot run before the VM can run it.  These
 *  slots are the VM remembering its own connections, which is what they are.
 */
/*
 *  Closures added two more, and for a different reason worth recording.
 *
 *  BlockClosure and the selector aboutToReturn:through: have to be reachable
 *  from C, and the obvious place -- a new guaranteed object pointer -- is
 *  not available: the Blue Book's table ends at 56, and the OM=bb build
 *  loads a real 1983 image in which 58 and upward are ordinary objects.
 *  Putting them here instead has a second effect that turns out to be the
 *  whole coexistence story: in a bb build these slots are nil, so every
 *  closure primitive fails and the closure bytecodes are unreachable.  The
 *  1983 image cannot see closures at all, which is what keeps trace2 exact.
 */
/*
 *  And a fifth, for the same reason as the two above and one more.
 *
 *  #outOfMemory is what the interpreter sends when it cannot allocate the
 *  context for a method it has already found.  That used to print a line and
 *  clear st_vm.running -- no send, so nothing for `on: Error do:' to catch,
 *  and one runaway recursion took every other worker's work down with it.
 *  Sending needs a Symbol reachable from C, and the Blue Book's guaranteed
 *  pointers end at 56, so it goes here.
 *
 *  A profile that never binds it leaves the slot nil, and the interpreter
 *  stops exactly as it did before -- which is what the bb build does.
 */
/*
 *  And a sixth, #recursionDepthExceeded.
 *
 *  A method that does not stop calling itself used to take the whole
 *  process with it: about five million frames, 12 GB resident, and then
 *  `out of memory activating a method' and exit -- every worker, every open
 *  connection, every request in flight.  The interpreter now stops a stack
 *  at ST_MAX_CALL_DEPTH frames and sends this instead, which is an Error a
 *  handler can catch, so one runaway method costs one request.
 *
 *  A separate selector rather than #outOfMemory because the two are
 *  different faults with different fixes: one is a program that needs more
 *  room and the other is a program with a bug in it, and a handler that
 *  wants to retry on the first must not retry on the second.
 */
#define ST_VM_STATE_SLOTS               6
#define ST_VM_INPUT_SEMAPHORE           0
#define ST_VM_DISPLAY                   1
#define ST_VM_CLASS_BLOCK_CLOSURE       2
#define ST_VM_SELECTOR_ABOUT_TO_RETURN  3
#define ST_VM_SELECTOR_OUT_OF_MEMORY    4
#define ST_VM_SELECTOR_DEPTH_EXCEEDED   5

extern st_oop   st_om_vm_state[ST_VM_STATE_SLOTS];

#endif  /*  ST_OM_H  */

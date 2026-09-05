/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The 64-bit object memory -- the one that ships.
 *
 *  Same interface as the Blue Book memory, same object-pointer encoding, no
 *  16-bit ceilings.  Because the tagging convention is preserved, the
 *  interpreter compiles against this file without a single change, and the
 *  guaranteed object pointers keep the values Chapter 27 gives them: nil is
 *  still 2, true still 6, Array still 16.
 *
 *      pointer & 1 == 1    a SmallInteger, value in the upper 63 bits
 *      pointer & 1 == 0    twice an index into the object table
 *
 *  Retaining the object table is deliberate, and threading is the reason.
 *  Spur and every other modern VM went to direct pointers because an
 *  indirection costs a load; under concurrency the trade inverts:
 *
 *      become:      one atomic swap of two table entries, versus a
 *                   stop-the-world scan of the whole heap
 *      pinning      free, because the entry is the identity, so SDL and
 *                   foreign code can hold a body pointer safely
 *      compaction   move the body, update one entry, touch no references
 *      per-object   the entry is the natural home for a lock word and the
 *      metadata     shared/owned flags Phase 7 needs
 *
 *  We pay one indirection to make identity mutation atomic.  In a system
 *  where a developer recompiles a class while eight threads are inside its
 *  methods, that is an architectural choice rather than a micro-optimization.
 */

#ifndef ST_OM_MT_H
#define ST_OM_MT_H

#include <stdint.h>
#include <stddef.h>

#include "st_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*  ----------  Types  ----------  */

typedef uint64_t    st_oop;
typedef int64_t     st_int;

#define ST_OOP_INVALID      ((st_oop) 0)

/*  ----------  Geometry  ----------  */

#define ST_HEADER_SIZE      0       /*  headers live beside the body, not in it */

/*  SmallInteger keeps one tag bit, so 63 bits of value.  */
#define ST_INT_MIN          (-(INT64_C(1) << 62))
#define ST_INT_MAX          ((INT64_C(1) << 62) - 1)

/*  Reference counts are 32 bits here, so saturation is effectively absent.  */
#define ST_HUGE_SIZE        0x7FFFFFFF

/*  ----------  Guaranteed object pointers  ----------
 *
 *  Identical to the Blue Book, so that code written against either memory
 *  names the same objects.
 */
#define ST_NIL                          2
#define ST_FALSE                        4
#define ST_TRUE                         6
#define ST_SCHEDULER_ASSOCIATION        8
#define ST_CLASS_SMALL_INTEGER          12
#define ST_CLASS_STRING                 14
#define ST_CLASS_ARRAY                  16
#define ST_SMALLTALK                    18
#define ST_CLASS_FLOAT                  20
/*
 *  Two context fields the COLLECTOR has to know, because a context's stack
 *  above its stack pointer is dead and marking it keeps garbage alive.
 *  interp.h names the same two as ST_CTX_SP and ST_CTX_TEMP_FRAME_START and
 *  is the authority; a _Static_assert there fails if these ever drift.
 */
#define OM_CTX_SP_FIELD                 2
#define OM_CTX_TEMP_FRAME_START         6

#define ST_CLASS_METHOD_CONTEXT         22
#define ST_CLASS_BLOCK_CONTEXT          24
#define ST_CLASS_POINT                  26
#define ST_CLASS_LARGE_POSITIVE_INTEGER 28
#define ST_CLASS_DISPLAY_BITMAP         30
#define ST_CLASS_MESSAGE                32
#define ST_CLASS_COMPILED_METHOD        34
#define ST_THE_INTERPRETER              36
#define ST_CLASS_SEMAPHORE              38
#define ST_CLASS_CHARACTER              40
#define ST_SELECTOR_DOES_NOT_UNDERSTAND 42
#define ST_SELECTOR_CANNOT_RETURN       44
#define ST_PROCESS_SIGNALING_LOW_SPACE  46
#define ST_SPECIAL_SELECTORS            48
#define ST_CHARACTER_TABLE              50
#define ST_SELECTOR_MUST_BE_BOOLEAN     52
#define ST_CLASS_DISPLAY_SCREEN         54
#define ST_SELECTOR_CANNOT_INTERPRET    56

/*  ----------  Object bodies  ----------  */

#define ST_FMT_POINTERS     0x0001  /*  fields are object pointers  */
#define ST_FMT_WORDS        0x0002  /*  fields are 16-bit words     */
#define ST_FMT_BYTES        0x0004  /*  fields are bytes            */
#define ST_FMT_MARKED       0x0100  /*  collector mark bit          */
#define ST_FMT_FREE         0x0200  /*  table entry is unused       */
/*
 *  Weak: the collector does not follow this object's INDEXED fields, and
 *  nils any of them that nothing else keeps alive.  The named fields at the
 *  front stay strong, which is the Squeak arrangement and the useful one --
 *  a WeakArray's own instance variables are not the point of it.
 */
#define ST_FMT_WEAK         0x0400
/*
 *  An ephemeron: field 0 is the KEY, and every field is strong exactly when
 *  the key is reachable some other way.  Not a weak object with a different
 *  name -- a weak slot is decided by one pass and this one needs a fixed
 *  point, which is why the collector walks ephemerons separately.
 */
#define ST_FMT_EPHEMERON    0x0800

typedef struct {
    st_oop          class_oop;
    uint32_t        size;           /*  fields, words or bytes, per format  */
    uint32_t        flags;
    /*
     *  The count is mutated by every thread that stores a pointer, so it is
     *  atomic rather than merely volatile.  Relaxed ordering is enough for
     *  the increment -- what matters is that no update is lost -- but the
     *  decrement that reaches zero must synchronise with the other threads'
     *  releases before the object is torn down, so it is acquire-release.
     */
    uint32_t        hash;           /*  identity hash, stable across moves  */
} om_header;

/*
 *  Reference counts live BESIDE the objects, not inside them.
 *
 *  A count is the one field of an object that every worker writes, and the
 *  rest of the header and the first words of the body are read by every
 *  worker on the hot path -- a CompiledMethod's literal frame is read on
 *  every activation while its count is driven up and down on every
 *  activation.  In one allocation those share a cache line, so counting
 *  invalidates the line the interpreter is reading, and the reading is what
 *  costs.  perf c2c put 75% of all contention in the intervals kernel on
 *  exactly that line.
 *
 *  Measured: padding the header until the body starts on a fresh line is
 *  worth 7% of the eight-worker cycles and NOTHING at one worker, which is
 *  what says the effect is sharing rather than alignment.  This is the same
 *  separation at four bytes an object instead of forty.
 *
 *  Indexed by table index, so a count is one load from a dense array rather
 *  than a chase through the table -- and the counts of unrelated objects
 *  share lines, which is the traffic that was there before and no more.
 */
extern st_atomic_uint  *st_om_refcounts;

static inline st_atomic_uint *
OM_refcount_of(st_oop p)
{
    return &st_om_refcounts[p >> 1];
}

/*
 *  A table entry is the header immediately followed by the body, in one
 *  allocation.  Keeping them adjacent means one pointer chase to reach
 *  either, and lets a future collector move the pair together.
 */
/*
 *  How big the table STARTS.  It is no longer how big it can get.
 *
 *  This was a ceiling, and it was the wrong one.  A table of four million
 *  entries is four million live objects however much memory the machine
 *  has, and the message a program got when it crossed the line said so
 *  itself: "out of memory activating a method: 282316026 words and 0 object
 *  table entries free" -- two hundred and eighty-two million words of heap
 *  still free, and the process gone.  Two ordinary mistakes reach it:
 *  recursion about five million frames deep, and printString of a structure
 *  that contains itself.
 *
 *  It grows now, and the reason it could not before is real and is answered
 *  rather than argued away.  Reallocation MOVES the array while other
 *  threads are indexing it without a lock -- taking one on every field
 *  access would defeat the point of an object table -- so the growth is
 *  done at a SAFEPOINT, where every other worker is parked at a bytecode
 *  boundary and none of them is holding a pointer into it.  That is the
 *  same mechanism OM_forward_identity and OM_next_instance_after already
 *  use, and for the same reason.
 *
 *  A segmented table -- a fixed directory of fixed-size chunks, so growth
 *  adds a chunk and never moves one -- is still the shape to want, because
 *  it needs no safepoint at all.  It is a change to the hot path in
 *  OM_table_get; this is not, and the wall is gone either way.
 *
 *  Reserving the range up front costs eight bytes of address space per
 *  possible object and nothing resident until an entry is touched, because
 *  the pages are only mapped on first write.
 */
#define ST_OM_MAX_OBJECTS   (4u * 1024u * 1024u)

/*
 *  And how big it may GET, which is a variable rather than a constant so
 *  that a runaway is bounded without being bounded at four million.
 *
 *  A ceiling is still wanted.  Growing until malloc says no means a
 *  recursion with no base case, or a printString of a structure that
 *  contains itself, takes the machine rather than the image -- and on a
 *  server that is other people's processes, not only this one's.  Sixteen
 *  times the starting size is the default: the case Bugs1.md measured died
 *  with four million entries used and 282 million words of heap free, and
 *  this clears that by an order of magnitude while still stopping.
 *
 *  Costs nothing until it is used.  The table is lazily mapped, so the
 *  ceiling is address space, not memory.  ST_MAX_OBJECTS in the environment
 *  moves it, and the message printed when it is reached names it.
 */
#define ST_OM_MAX_OBJECTS_CEILING   (64u * 1024u * 1024u)

extern uint32_t         st_om_table_max;    /*  entries this run may reach  */

/*
 *  The table's slots are atomic because they are written under table_lock
 *  but READ without it, on every dereference.  Publishing a new entry is
 *  ordered by st_om_table_limit's release, but slot REUSE is not: handing a
 *  freed index to the next allocation stores NULL and then a new body into a
 *  slot another thread may be reading at that moment.  A plain pointer there
 *  is a data race, which the thread sanitizer reports about one run in four.
 */
extern st_atomic_ptr   *st_om_table;
extern uint32_t         st_om_table_size;   /*  entries reserved  */

/*
 *  Grow the table -- and the counts beside it -- to hold at least that many
 *  entries.  Answers zero if the memory is not there.
 *
 *  ONE function for both arrays, deliberately.  They are indexed by the same
 *  number and they were not grown together: the image loader doubled
 *  st_om_table until it covered the image's own limit and left
 *  st_om_refcounts at the size OM_init gave it, so an image with more than
 *  four million objects would have written past the end of the counts.  It
 *  was unreachable only because the ceiling above stopped such an image from
 *  being built.  Now that the ceiling is gone, the two limits are the same
 *  variable and there is one place that changes it.
 *
 *  Safe to call with workers running: it parks them first.
 */
int     OM_grow_table_to(uint32_t entries);

/*
 *  Lift the ceiling by an emergency reserve so the image can raise an error
 *  about having run out of room.  Answers zero if it is already spent.  The
 *  collector re-arms it once the image is comfortably back underneath.
 */
int     OM_release_table_reserve(void);
void    OM_rearm_table_reserve(void);
extern st_atomic_uint   st_om_table_limit;  /*  first index past the used range */

static inline om_header *
OM_head(st_oop p)
{
    return (om_header *) ST_load_acquire(&st_om_table[p >> 1]);
}

static inline void *
OM_body(st_oop p)
{
    return (void *) (OM_head(p) + 1);
}

/*  Publishing a slot: pairs with the acquire in OM_head.  */
static inline void
OM_table_set(uint32_t index, om_header *head)
{
    ST_store_release(&st_om_table[index], (uintptr_t) head);
}

static inline om_header *
OM_table_get(uint32_t index)
{
    return (om_header *) ST_load_acquire(&st_om_table[index]);
}

/*  ----------  SmallIntegers  ----------  */

static inline int
OM_is_int(st_oop p)
{
    return (int) (p & 1);
}

static inline st_int
OM_int_value(st_oop p)
{
    return (st_int) p >> 1;
}

static inline st_oop
OM_int_oop(st_int v)
{
    return (st_oop) (((uint64_t) v << 1) | 1);
}

static inline int
OM_int_fits(st_int v)
{
    return v >= ST_INT_MIN && v <= ST_INT_MAX;
}

/*  ----------  Object access  ----------  */

int     OM_is_object(st_oop p);

static inline st_oop
OM_class_of_object(st_oop p)
{
    return OM_head(p)->class_oop;
}

static inline void
OM_set_class_of_object(st_oop p, st_oop c)
{
    OM_head(p)->class_oop = c;
}

static inline st_oop
OM_fetch_class(st_oop p)
{
    if (OM_is_int(p))
        return ST_CLASS_SMALL_INTEGER;
    return OM_class_of_object(p);
}

static inline unsigned
OM_pointer_bit(st_oop p)
{
    return (OM_head(p)->flags & ST_FMT_POINTERS) ? 1u : 0u;
}

static inline unsigned
OM_weak_bit(st_oop p)
{
    return (OM_head(p)->flags & ST_FMT_WEAK) ? 1u : 0u;
}

static inline unsigned
OM_free_bit(st_oop p)
{
    return (OM_head(p)->flags & ST_FMT_FREE) ? 1u : 0u;
}

static inline unsigned
OM_count_bits(st_oop p)
{
    return (unsigned) ST_load_relaxed(OM_refcount_of(p));
}

static inline uint32_t
OM_fetch_word_length(st_oop p)
{
    return OM_head(p)->size;
}

static inline uint32_t
OM_fetch_byte_length(st_oop p)
{
    return OM_head(p)->size;
}

/*  Total footprint in words, for reporting parity with the Blue Book.  */
static inline uint32_t
OM_size_bits(st_oop p)
{
    om_header  *head = OM_head(p);

    if (head->flags & ST_FMT_POINTERS)
        return head->size * (uint32_t) (sizeof(st_oop) / 2);
    if (head->flags & ST_FMT_WORDS)
        return head->size;
    return (head->size + 1) / 2;
}

/*  Byte objects never need an odd-length flag here; size is exact.  */
static inline unsigned
OM_odd_bit(st_oop p)
{
    (void) p;
    return 0;
}

/*
 *  Object fields are read and written by several threads at once, so the
 *  accesses are atomic even though the ordering is relaxed.  Relaxed is the
 *  right strength: what must not happen is a torn value -- half of one
 *  pointer and half of another, which would be neither object -- and that is
 *  all a relaxed atomic promises.  Ordering between fields is the
 *  program's business, and doc/CONCURRENCY.md says so.
 *
 *  The cast is the usual pragmatic one: st_oop is pointer-sized and every
 *  field is naturally aligned, so the access is lock-free on every platform
 *  we target.
 */
static inline st_oop
ST_oop_load(const st_oop *slot)
{
    return (st_oop) atomic_load_explicit(
        (const _Atomic st_oop *) slot, memory_order_relaxed);
}

static inline void
ST_oop_store(st_oop *slot, st_oop value)
{
    atomic_store_explicit((_Atomic st_oop *) slot, value,
                          memory_order_relaxed);
}

static inline st_oop
OM_fetch_pointer(uint32_t field, st_oop p)
{
    return ST_oop_load(&((st_oop *) OM_body(p))[field]);
}

static inline uint16_t
OM_fetch_word(uint32_t field, st_oop p)
{
    return ((uint16_t *) OM_body(p))[field];
}

static inline void
OM_store_word(uint32_t field, st_oop p, uint16_t v)
{
    ((uint16_t *) OM_body(p))[field] = v;
}

static inline uint8_t
OM_fetch_byte(uint32_t index, st_oop p)
{
    return ((uint8_t *) OM_body(p))[index];
}

static inline void
OM_store_byte(uint32_t index, st_oop p, uint8_t v)
{
    ((uint8_t *) OM_body(p))[index] = v;
}

/*  Bulk access, for BitBlt.  Valid until the next allocation.  */
static inline uint16_t *
OM_word_base(st_oop p)
{
    return (uint16_t *) OM_body(p);
}

/*  ----------  Reference counting and lifetime  ----------  */

/*
 *  The slow halves, for something that really is an object.
 */
void    OM_increase_ref_object(st_oop p);
void    OM_decrease_ref_object(st_oop p);

/*
 *  The guaranteed pointers, which are created once at bootstrap and never
 *  freed: nil, true, false, the fixed classes and the fixed selectors.
 *
 *  Nothing may reference-count them, and that is not an optimisation --
 *  it is the difference between this system scaling and not.  A comparison
 *  answers a BOOLEAN, so "a < b" pushes true or false; every iteration of
 *  every loop in the image therefore does one atomic increment and one
 *  atomic decrement on one of two objects.  With eight workers that is one
 *  cache line carrying a locked read-modify-write from eight cores at once,
 *  and perf c2c put 99.81% of all cross-core stalls on exactly that line.
 *
 *  Skipping them is safe because it is SYMMETRIC -- neither the increment
 *  nor the decrement happens -- so a count that is never raised can never
 *  be lowered to zero and freed.  The collector recounts from the roots
 *  anyway, and these are roots.
 */
#define ST_LAST_IMMORTAL_OOP    ST_SELECTOR_CANNOT_INTERPRET

/*
 *  Reference counting, with the tag test INLINE.
 *
 *  These two are the hottest pair of calls in the system: every push, pop
 *  and field store goes through them, and for a SmallInteger -- which is
 *  most of what an interpreter moves around -- the whole job is to notice
 *  the tag bit and return.  Out of line that costs a call and a return per
 *  stack operation, and a profile of a loop doing nothing but SmallInteger
 *  arithmetic put OM_increase_ref at six times the cost of the entire
 *  interpreter loop it was called from.
 *
 *  The odd bit is the tag, so the test is one instruction and needs no
 *  memory at all.  Only a real object reaches the call.
 */
static inline void
OM_increase_ref(st_oop p)
{
    if (p > ST_LAST_IMMORTAL_OOP && p != ST_OOP_INVALID && (p & 1) == 0)
        OM_increase_ref_object(p);
}

static inline void
OM_decrease_ref(st_oop p)
{
    if (p > ST_LAST_IMMORTAL_OOP && p != ST_OOP_INVALID && (p & 1) == 0)
        OM_decrease_ref_object(p);
}

/*
 *  A counted store whose old value is released exactly once, however many
 *  workers store into the same slot at once.
 *
 *  OM_store_pointer reads the old value, stores the new one and releases
 *  the old one as three steps, which is right for a slot one thread owns
 *  and wrong for one they all write: two workers that read the same old
 *  value both release it, and the value one of them stored in between is
 *  never released at all.  The scheduler's activeProcess slot is written by
 *  every worker on every switch, and the Delay timing process -- the one
 *  process every worker runs in turn -- lost a count that way about once a
 *  minute under thirty-one workers, and was freed while it was linked on a
 *  semaphore.  Here the slot is exchanged atomically, so each old value
 *  belongs to one storer.
 */
static inline void
OM_exchange_pointer(uint32_t field, st_oop p, st_oop value)
{
    st_oop *slot = &((st_oop *) OM_body(p))[field];
    st_oop  old;

    OM_increase_ref(value);
    old = (st_oop) ST_exchange_acq_rel((_Atomic st_oop *) slot, value);
    OM_decrease_ref(old);
}
void    OM_store_pointer(uint32_t field, st_oop p, st_oop value);
void    OM_deallocate(st_oop p);

/*
 *  Hand out identity hashes above this one from now on.  The image loader
 *  calls it with the highest hash the file carried; see om_mt.c.
 */
void    OM_continue_identity_hashes_after(uint32_t hash);

st_oop  OM_instantiate_pointers(st_oop class_pointer, uint32_t size);
/*  As above, but the indexed fields past `fixed` are weak.  */
st_oop  OM_instantiate_weak(st_oop class_pointer, uint32_t size,
                            uint32_t fixed);
/*  As above, but the object is an ephemeron keyed on its first field.  */
st_oop  OM_instantiate_ephemeron(st_oop class_pointer, uint32_t size);
st_oop  OM_instantiate_words(st_oop class_pointer, uint32_t size);
st_oop  OM_instantiate_bytes(st_oop class_pointer, uint32_t size);

/*  Two-way identity exchange: one swap of table entries.  */
/*  Answers whether the swap happened; see OM_can_swap_identities.  */
int     OM_can_swap_identities(st_oop a, st_oop b);

int     OM_swap_identities(st_oop a, st_oop b);
/*  Bootstrap only: exchanges without the guard, for the guaranteed
    pointers whose bodies are being built.  */
void    OM_swap_identities_at_boot(st_oop a, st_oop b);

/*
 *  One-way identity forwarding: every reference to `from' becomes a
 *  reference to `to', and references to `to' are left alone.
 *
 *  This one cannot be a table swap, and the asymmetry is the reason.  A
 *  swap moves two bodies between two identities and both identities remain
 *  usable; forwarding has to leave `from' meaning what `to' means, and with
 *  an object table the only ways to do that are to point two table entries
 *  at one body -- which gives two identities that are == to neither each
 *  other nor themselves consistently, and one body two entries will each
 *  try to free -- or to find and rewrite the references.  This finds and
 *  rewrites them.
 *
 *  So it is a full sweep of the object table at a safepoint, followed by a
 *  collection to rebuild the counts.  That is expensive and it is meant to
 *  be: MethodDictionary>>grow is the caller that matters and it runs when a
 *  class doubles its method table.
 *
 *  Answers 0 without changing anything if the forward cannot be done: an
 *  immortal oop, or a `from' that some part of C holds in a place this
 *  cannot rewrite -- a running context, the method being executed, the
 *  display form.  A primitive failure is the right answer there, because
 *  the alternative is a dangling pointer in the interpreter's own
 *  registers.
 */
/*
 *  Whether that forward would be accepted, asked without doing it.  A bulk
 *  become checks every pair with this before moving any of them: forwarding
 *  three of five and then refusing leaves an image in a state no caller
 *  asked for and none can undo.
 */
int     OM_can_forward_identity(st_oop from, st_oop to);
int     OM_forward_identity(st_oop from, st_oop to);

/*
 *  Store `value` in a field only if it currently holds `expected`, and say
 *  whether it did.  The one operation a lock-free algorithm cannot be
 *  written without.
 *
 *  The reference counting is the hard part, and it is why this is here
 *  rather than an atomic_compare_exchange at the call site: the counts may
 *  only move if the swap HAPPENED, and they must move while nobody else
 *  can observe the slot half-updated.  A count taken before a failed swap
 *  is a leak; a count released before a successful one is a use-after-free
 *  under a concurrent collector.
 */
int     OM_compare_and_swap_pointer(uint32_t field, st_oop p,
                                    st_oop expected, st_oop value);

/*  ----------  Enumeration  ----------  */

st_oop  OM_first_object(void);
st_oop  OM_next_object(st_oop p);

/*  ----------  Lifecycle  ----------  */

int     OM_init(void);
void    OM_shutdown(void);

uint32_t    OM_core_left(void);
uint32_t    OM_oops_left(void);

typedef void (*om_visit_fn)(st_oop object);
typedef void (*om_root_provider)(om_visit_fn visit);

void        OM_set_root_provider(om_root_provider provider);

/*
 *  The other side of OM_forward_identity: whoever holds references in C
 *  says here how to rewrite the ones it can, and which oops it holds
 *  somewhere it cannot.  Both are asked at a safepoint with every worker
 *  parked.
 */
typedef void (*om_root_forwarder)(st_oop from, st_oop to);
typedef int  (*om_root_pin_fn)(st_oop p);

void        OM_set_root_forwarder(om_root_forwarder forwarder,
                                  om_root_pin_fn pinned);

/*
 *  What a two-way become: may not touch: an object some C register has
 *  cached a raw pointer into.  Narrower than the forwarder's pin above --
 *  see ST_interp_swap_forbidden -- because a swap exchanges bodies and
 *  leaves every object pointer naming a live object, so a C array of oops
 *  is as valid afterwards as it was before.
 */
void        OM_set_swap_guard(om_root_pin_fn pinned);
uint32_t    OM_collect(void);

/*
 *  Publish this worker's epoch, release what that makes safe, and advance
 *  the world if everyone has caught up.  The interpreter calls it every
 *  1024 bytecodes; see the reclamation note in om_mt.c.
 */
void        OM_epoch_step(void);

/*
 *  The next live instance of a class after `after`, or ST_OOP_INVALID.
 *
 *  Pass ST_OOP_INVALID to start.  The order is the object table's, which is
 *  arbitrary but stable, and that is all someInstance/nextInstance promise.
 */
st_oop  OM_next_instance_after(st_oop after, st_oop class_oop);


extern uint32_t st_om_collections;
extern uint32_t st_om_reclaimed;
/*  How many weak references have been nilled, over the run.  */
extern uint32_t st_om_weak_cleared;
/*  Ephemerons whose key died and that were queued for #mourn.  */
extern uint32_t st_om_ephemerons_mourned;

/*
 *  Set live_objects and live_bytes from what the table actually holds.  For
 *  the one caller that fills the table without going through the allocator:
 *  the image loader.  See the comment on the definition.
 */
void    OM_recount_live(void);

/*  Set once the fixed objects are in place, so the collector can start.  */
int     OM_image_load(const char *path, char *errbuf, size_t errlen);
int     OM_image_save(const char *path, char *errbuf, size_t errlen);

extern uint32_t st_om_image_object_words;
extern uint32_t st_om_image_ot_words;

#ifdef __cplusplus
}
#endif

/*
 *  The identity hash, as the image sees it.
 *
 *  IdentityDictionary>>findKeyOrNil: starts probing at "key asOop \\ length
 *  + 1", so anything built in C that the image will later look up has to
 *  agree with this exactly -- a method dictionary filled from slot zero is
 *  perfectly good to the interpreter, which scans, and invisible to the
 *  image, which hashes.  Primitive 75 answers this and nothing else may
 *  compute it differently.
 *
 *  Thirty bits of the allocation number, not fourteen.  The mask was
 *  0x3FFF, the width of the 1983 oop the hash stands in for, and it made
 *  every identity-hashed collection in the system quadratic past a few
 *  thousand elements: a hundred thousand `Object new' had 16,384 distinct
 *  hashes between them, and Object>>hash, Symbol>>hash, a Set of classes
 *  or of Processes are all this hash (Bugs3 B33).  Thirty rather than
 *  thirty-two so that a hash multiplied by a thirty-bit mixing constant
 *  still fits a 62-bit SmallInteger -- 2^60 does, 2^62 does not -- and
 *  the Blue Book memory keeps its fourteen, because there the hash IS the
 *  16-bit oop.  The header field is what the snapshot stores, so a hash
 *  survives a save and a reload unchanged, and swap_identities_unguarded
 *  keeps it with the identity across a become:.
 */
#define OM_identity_hash(p)     (OM_head(p)->hash & 0x3FFFFFFF)

#endif  /*  ST_OM_MT_H  */

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
    st_atomic_uint  refcount;
    uint32_t        hash;           /*  identity hash, stable across moves  */
} om_header;

/*
 *  A table entry is the header immediately followed by the body, in one
 *  allocation.  Keeping them adjacent means one pointer chase to reach
 *  either, and lets a future collector move the pair together.
 */
/*
 *  The table is allocated once, at its maximum, and never moved.
 *
 *  That is a threading requirement rather than a simplification.  A growable
 *  table has to be reallocated, and reallocation moves it -- while other
 *  threads are indexing it without a lock, since taking one on every field
 *  access would defeat the point of the object table.  Reserving the whole
 *  range up front costs eight bytes of address space per possible object and
 *  nothing in resident memory until an entry is touched, because the pages
 *  are only mapped on first write.
 *
 *  The growth path, when four million objects is no longer enough, is a
 *  segmented table: a fixed directory of fixed-size chunks, so growth adds a
 *  chunk and never moves an existing one.
 */
#define ST_OM_MAX_OBJECTS   (4u * 1024u * 1024u)

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
    return (unsigned) ST_load_relaxed(&OM_head(p)->refcount);
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
void    OM_store_pointer(uint32_t field, st_oop p, st_oop value);
void    OM_deallocate(st_oop p);

st_oop  OM_instantiate_pointers(st_oop class_pointer, uint32_t size);
/*  As above, but the indexed fields past `fixed` are weak.  */
st_oop  OM_instantiate_weak(st_oop class_pointer, uint32_t size,
                            uint32_t fixed);
st_oop  OM_instantiate_words(st_oop class_pointer, uint32_t size);
st_oop  OM_instantiate_bytes(st_oop class_pointer, uint32_t size);

/*  Two-way identity exchange: one swap of table entries.  */
void    OM_swap_identities(st_oop a, st_oop b);

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
uint32_t    OM_collect(void);

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
 */
#define OM_identity_hash(p)     (OM_head(p)->hash & 0x3FFF)

#endif  /*  ST_OM_MT_H  */

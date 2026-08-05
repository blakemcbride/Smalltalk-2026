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

typedef struct {
    st_oop      class_oop;
    uint32_t    size;               /*  fields, words or bytes, per format  */
    uint32_t    flags;
    uint32_t    refcount;
    uint32_t    hash;               /*  identity hash, stable across moves  */
} om_header;

/*
 *  A table entry is the header immediately followed by the body, in one
 *  allocation.  Keeping them adjacent means one pointer chase to reach
 *  either, and lets a future collector move the pair together.
 */
extern om_header  **st_om_table;
extern uint32_t     st_om_table_size;   /*  entries allocated  */
extern uint32_t     st_om_table_limit;  /*  first index past the used range */

static inline void *
OM_body(st_oop p)
{
    return (void *) (st_om_table[p >> 1] + 1);
}

static inline om_header *
OM_head(st_oop p)
{
    return st_om_table[p >> 1];
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
OM_free_bit(st_oop p)
{
    return (OM_head(p)->flags & ST_FMT_FREE) ? 1u : 0u;
}

static inline unsigned
OM_count_bits(st_oop p)
{
    return OM_head(p)->refcount;
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

static inline st_oop
OM_fetch_pointer(uint32_t field, st_oop p)
{
    return ((st_oop *) OM_body(p))[field];
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

void    OM_increase_ref(st_oop p);
void    OM_decrease_ref(st_oop p);
void    OM_store_pointer(uint32_t field, st_oop p, st_oop value);
void    OM_deallocate(st_oop p);

st_oop  OM_instantiate_pointers(st_oop class_pointer, uint32_t size);
st_oop  OM_instantiate_words(st_oop class_pointer, uint32_t size);
st_oop  OM_instantiate_bytes(st_oop class_pointer, uint32_t size);

/*  Two-way identity exchange: one swap of table entries.  */
void    OM_swap_identities(st_oop a, st_oop b);

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

extern uint32_t st_om_collections;
extern uint32_t st_om_reclaimed;

/*  Set once the fixed objects are in place, so the collector can start.  */
int     OM_image_load(const char *path, char *errbuf, size_t errlen);
int     OM_image_save(const char *path, char *errbuf, size_t errlen);

extern uint32_t st_om_image_object_words;
extern uint32_t st_om_image_ot_words;

#ifdef __cplusplus
}
#endif

#endif  /*  ST_OM_MT_H  */

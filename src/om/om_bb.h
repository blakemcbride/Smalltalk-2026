/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Blue Book object memory: Smalltalk-80: The Language and Its
 *  Implementation, Chapter 30.
 *
 *  16-bit object pointers, an object table, reference counting.  32,768
 *  objects and a 2 MB heap -- which is exactly why this implementation is
 *  never shipped.  It exists so that the interpreter, which is written once
 *  against this interface, can load the original 1983 Xerox virtual image
 *  and reproduce Xerox's own execution traces byte for byte.  Passing that
 *  gate validates the same interpreter source the parallel system runs.
 *
 *  Bit numbering warning: the Blue Book numbers bits with 0 as the MOST
 *  significant bit of a 16-bit word.  Every accessor below converts to
 *  ordinary least-significant-bit-zero numbering, so Blue Book "bits 0 to 7"
 *  becomes a shift right by 8.  Get this backwards and nothing works.
 */

#ifndef ST_OM_BB_H
#define ST_OM_BB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  ----------  Types  ----------  */

typedef uint16_t    st_oop;
typedef int32_t     st_int;      /*  a SmallInteger's value, comfortably  */

#define ST_OOP_INVALID      ((st_oop) 0)

/*  ----------  Geometry  ----------  */

#define ST_HEADER_SIZE          2               /*  words: size, class  */
#define ST_SEGMENT_WORDS        65536
#define ST_HEAP_SEGMENTS        16
#define ST_HEAP_WORDS           ((uint32_t) ST_HEAP_SEGMENTS * ST_SEGMENT_WORDS)
#define ST_OT_WORDS             65536
#define ST_OT_ENTRIES           (ST_OT_WORDS / 2)

/*  Reference counts saturate here and the object becomes permanent.  */
#define ST_HUGE_SIZE            256

/*  SmallInteger range: 15 bits, two's complement.  */
#define ST_INT_MIN              (-16384)
#define ST_INT_MAX              16383

/*  ----------  Guaranteed object pointers (Blue Book Chapter 27)  ----------  */

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

/*  ----------  Memory  ----------  */

/*
 *  Word memory and the object table.  Exposed so the accessors below can be
 *  inlined; nothing outside the object memory should touch them directly.
 */
extern uint16_t    *st_om_words;
extern uint16_t    *st_om_ot;
extern uint32_t     st_om_ot_limit;     /*  first oop past the loaded table  */

/*  ----------  SmallIntegers  ----------  */

/*
 *  A pointer with its low bit set is not a pointer at all: the upper 15 bits
 *  are the value in two's complement.  The cast to int16_t before shifting
 *  is what sign-extends; an unsigned shift would turn -1 into 32767.
 */

static inline int
OM_is_int(st_oop p)
{
    return p & 1;
}

static inline st_int
OM_int_value(st_oop p)
{
    return (st_int) ((int16_t) p >> 1);
}

static inline st_oop
OM_int_oop(st_int v)
{
    return (st_oop) (((uint16_t) v << 1) | 1);
}

static inline int
OM_int_fits(st_int v)
{
    return v >= ST_INT_MIN && v <= ST_INT_MAX;
}

/*  ----------  Object table entry fields  ----------  */

/*
 *  An object pointer is a WORD OFFSET into the object table, not an index.
 *  Entries are two words, so live pointers are always even and entry number
 *  n lives at offset 2n.
 */

static inline unsigned
OM_count_bits(st_oop p)
{
    return st_om_ot[p] >> 8;            /*  Blue Book bits 0..7   */
}

static inline unsigned
OM_odd_bit(st_oop p)
{
    return (st_om_ot[p] >> 7) & 1;      /*  Blue Book bit 8       */
}

static inline unsigned
OM_pointer_bit(st_oop p)
{
    return (st_om_ot[p] >> 6) & 1;      /*  Blue Book bit 9       */
}

static inline unsigned
OM_free_bit(st_oop p)
{
    return (st_om_ot[p] >> 5) & 1;      /*  Blue Book bit 10      */
}

static inline unsigned
OM_segment_bits(st_oop p)
{
    return st_om_ot[p] & 0x0F;          /*  Blue Book bits 12..15 */
}

static inline uint16_t
OM_location(st_oop p)
{
    return st_om_ot[p + 1];
}

static inline void
OM_set_count_bits(st_oop p, unsigned v)
{
    st_om_ot[p] = (uint16_t) ((st_om_ot[p] & 0x00FF) | ((v & 0xFF) << 8));
}

static inline void
OM_set_odd_bit(st_oop p, unsigned v)
{
    st_om_ot[p] = (uint16_t) ((st_om_ot[p] & ~(1u << 7)) | ((v & 1u) << 7));
}

static inline void
OM_set_pointer_bit(st_oop p, unsigned v)
{
    st_om_ot[p] = (uint16_t) ((st_om_ot[p] & ~(1u << 6)) | ((v & 1u) << 6));
}

static inline void
OM_set_free_bit(st_oop p, unsigned v)
{
    st_om_ot[p] = (uint16_t) ((st_om_ot[p] & ~(1u << 5)) | ((v & 1u) << 5));
}

static inline void
OM_set_segment_bits(st_oop p, unsigned v)
{
    st_om_ot[p] = (uint16_t) ((st_om_ot[p] & ~0x0Fu) | (v & 0x0Fu));
}

static inline void
OM_set_location(st_oop p, uint16_t v)
{
    st_om_ot[p + 1] = v;
}

/*  ----------  Heap access  ----------  */

/*
 *  A 20-bit word address: four bits of segment above sixteen bits of
 *  location.  The heap is one flat array so this is a plain index.
 */
static inline uint32_t
OM_chunk_addr(st_oop p)
{
    return ((uint32_t) OM_segment_bits(p) << 16) | OM_location(p);
}

static inline uint16_t
OM_heap_word(uint32_t addr)
{
    return st_om_words[addr];
}

static inline void
OM_heap_word_put(uint32_t addr, uint16_t v)
{
    st_om_words[addr] = v;
}

/*  Word 0 of the object's own header: its size in words, header included.  */
static inline uint16_t
OM_size_bits(st_oop p)
{
    return st_om_words[OM_chunk_addr(p)];
}

static inline void
OM_set_size_bits(st_oop p, uint16_t v)
{
    st_om_words[OM_chunk_addr(p)] = v;
}

/*  Word 1: the class.  */
static inline st_oop
OM_class_of_object(st_oop p)
{
    return st_om_words[OM_chunk_addr(p) + 1];
}

static inline void
OM_set_class_of_object(st_oop p, st_oop c)
{
    st_om_words[OM_chunk_addr(p) + 1] = c;
}

/*  The class of anything, including immediates.  */
static inline st_oop
OM_fetch_class(st_oop p)
{
    if (OM_is_int(p))
        return ST_CLASS_SMALL_INTEGER;
    return OM_class_of_object(p);
}

/*  ----------  Lengths  ----------  */

static inline uint16_t
OM_fetch_word_length(st_oop p)
{
    return (uint16_t) (OM_size_bits(p) - ST_HEADER_SIZE);
}

/*
 *  Byte objects pack two bytes per word; the odd bit says the final word
 *  carries only one.
 */
static inline uint32_t
OM_fetch_byte_length(st_oop p)
{
    return (uint32_t) OM_fetch_word_length(p) * 2 - OM_odd_bit(p);
}

/*  ----------  Fields  ----------  */

static inline st_oop
OM_fetch_pointer(uint32_t field, st_oop p)
{
    return st_om_words[OM_chunk_addr(p) + ST_HEADER_SIZE + field];
}

static inline uint16_t
OM_fetch_word(uint32_t field, st_oop p)
{
    return st_om_words[OM_chunk_addr(p) + ST_HEADER_SIZE + field];
}

static inline void
OM_store_word(uint32_t field, st_oop p, uint16_t v)
{
    st_om_words[OM_chunk_addr(p) + ST_HEADER_SIZE + field] = v;
}

/*
 *  Byte order inside a word is Blue Book, not host: byte 0 is the HIGH half
 *  of word 0.  These operate on word values, never on raw memory, so they
 *  are correct on a little-endian host once the image has been swapped into
 *  native word order at load time.
 */
static inline uint8_t
OM_fetch_byte(uint32_t byte_index, st_oop p)
{
    uint16_t    w = OM_fetch_word(byte_index / 2, p);

    if (byte_index & 1)
        return (uint8_t) (w & 0xFF);
    return (uint8_t) (w >> 8);
}

static inline void
OM_store_byte(uint32_t byte_index, st_oop p, uint8_t v)
{
    uint32_t    field = byte_index / 2;
    uint16_t    w     = OM_fetch_word(field, p);

    if (byte_index & 1)
        w = (uint16_t) ((w & 0xFF00) | v);
    else
        w = (uint16_t) ((w & 0x00FF) | ((uint16_t) v << 8));
    OM_store_word(field, p, w);
}

/*  ----------  Allocation  ----------  */

/*
 *  Chunks of ST_BIG_SIZE words or more share one free list; smaller chunks
 *  get a list per exact size, so the common small allocations never search.
 */
#define ST_BIG_SIZE     21

/*
 *  Instantiate.  Each returns ST_OOP_INVALID if the heap or the object table
 *  is exhausted.  `size` counts fields, not including the two-word header.
 *
 *      ..._pointers    fields are object pointers, initialized to nil
 *      ..._words       fields are raw 16-bit words, zeroed
 *      ..._bytes       byte-indexable; size is in bytes, zeroed
 */
st_oop  OM_instantiate_pointers(st_oop class_pointer, uint32_t size);
st_oop  OM_instantiate_words(st_oop class_pointer, uint32_t size);
st_oop  OM_instantiate_bytes(st_oop class_pointer, uint32_t size);

/*  Release an object and recursively release what it referred to.  */
void    OM_deallocate(st_oop p);

/*
 *  Two-way identity exchange -- primitive 72.  With an object table this is
 *  a swap of two entries and nothing in the heap moves or is rewritten.
 */
void    OM_swap_identities(st_oop a, st_oop b);

/*  Words currently available across every segment.  Blue Book coreLeft.  */
uint32_t    OM_core_left(void);

/*  Unused object table entries.  The other exhaustible resource.  */
uint32_t    OM_oops_left(void);

/*
 *  ----------  Garbage collection  ----------
 *
 *  Reference counting alone cannot reclaim a cycle, and Smalltalk makes them
 *  constantly: a BlockContext points at its home, and the home's stack holds
 *  the block.  The Blue Book therefore specifies a marking collector as well
 *  (Chapter 30), and this is it.
 *
 *  The collector zeroes every count, walks the reachable graph incrementing
 *  as it goes, and frees whatever still stands at zero.  That both reclaims
 *  cycles and rebuilds exact counts, so a run repairs any drift as a side
 *  effect.
 *
 *  Roots the object memory cannot know about -- the interpreter's registers,
 *  the display, the scheduler -- are supplied by a provider the VM installs.
 */
typedef void (*om_visit_fn)(st_oop object);
typedef void (*om_root_provider)(om_visit_fn visit);

void        OM_set_root_provider(om_root_provider provider);

/*  Returns the number of objects reclaimed.  */
uint32_t    OM_collect(void);

/*
 *  The next live instance of a class after `after`, or ST_OOP_INVALID.
 *
 *  Pass ST_OOP_INVALID to start.  The order is the object table's, which is
 *  arbitrary but stable, and that is all someInstance/nextInstance promise.
 */
st_oop  OM_next_instance_after(st_oop after, st_oop class_oop);


/*  Collection statistics, for reporting and for spotting thrash.  */
extern uint32_t st_om_collections;
extern uint32_t st_om_reclaimed;

/*
 *  Derive the free lists and the object-table free chain from the object
 *  table by sweeping.  Called automatically after an image loads.
 */
int     OM_rebuild_free_lists(void);

/*
 *  Direct access to an object's data words.
 *
 *  For bulk operations -- BitBlt above all -- going through the object table
 *  once and then indexing raw words is the difference between a usable
 *  display and a slideshow.  The pointer is valid only until the next
 *  allocation, since that may compact; no caller may hold it across one.
 */
static inline uint16_t *
OM_word_base(st_oop p)
{
    return &st_om_words[OM_chunk_addr(p) + ST_HEADER_SIZE];
}

/*  ----------  Reference counting  ----------  */

void    OM_increase_ref(st_oop p);
void    OM_decrease_ref(st_oop p);

/*  Store with the correct count adjustments; the interpreter uses this.  */
void    OM_store_pointer(uint32_t field, st_oop p, st_oop value);

/*  ----------  Enumeration  ----------  */

/*
 *  Walk every live object.  Returns ST_OOP_INVALID when exhausted.  Used by
 *  the census that validates a freshly loaded image against Xerox's own
 *  reference dumps.
 */
st_oop  OM_first_object(void);
st_oop  OM_next_object(st_oop p);

/*  Is this pointer a live, allocated object?  False for immediates.  */
int     OM_is_object(st_oop p);

/*  ----------  Lifecycle  ----------  */

int     OM_init(void);
void    OM_shutdown(void);

/*  ----------  Image  ----------  */

/*
 *  Load a Xerox Smalltalk-80 version 2 snapshot.  Returns 0 on success and
 *  -1 on failure, leaving a message in the buffer if one is supplied.
 */
int     OM_image_load(const char *path, char *errbuf, size_t errlen);

/*  Counts filled in by the loader, for reporting and for tests.  */
extern uint32_t     st_om_image_object_words;
extern uint32_t     st_om_image_ot_words;

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
#define OM_identity_hash(p)     (((uint32_t) (p) >> 1) & 0x3FFF)

#endif  /*  ST_OM_BB_H  */

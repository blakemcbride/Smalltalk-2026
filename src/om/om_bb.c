/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Blue Book object memory.  See om_bb.h for the contract.
 */

#include "om_bb.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

uint16_t   *st_om_words;
uint16_t   *st_om_ot;
uint32_t    st_om_ot_limit;

static uint16_t free_lists[ST_HEAP_SEGMENTS][ST_BIG_SIZE + 1];
static st_oop   ot_free_head;

/*  ----------  Lifecycle  ----------  */

int
OM_init(void)
{
    OM_shutdown();
    st_om_words = (uint16_t *) calloc(ST_HEAP_WORDS, sizeof(uint16_t));
    st_om_ot    = (uint16_t *) calloc(ST_OT_WORDS,  sizeof(uint16_t));
    if (!st_om_words || !st_om_ot) {
        OM_shutdown();
        return -1;
    }
    st_om_ot_limit = 0;
    memset(free_lists, 0, sizeof free_lists);
    ot_free_head = ST_OOP_INVALID;
    return 0;
}

void
OM_shutdown(void)
{
    free(st_om_words);
    free(st_om_ot);
    st_om_words    = NULL;
    st_om_ot       = NULL;
    st_om_ot_limit = 0;
}

/*  ----------  Object identity  ----------  */

int
OM_is_object(st_oop p)
{
    if (OM_is_int(p))
        return 0;
    if (p == ST_OOP_INVALID || p >= st_om_ot_limit)
        return 0;
    if (p & 1)
        return 0;                   /*  odd offsets are not entry starts  */
    return !OM_free_bit(p);
}

/*  ----------  Enumeration  ----------  */

st_oop
OM_first_object(void)
{
    return OM_next_object(ST_OOP_INVALID);
}

st_oop
OM_next_object(st_oop p)
{
    uint32_t    next;

    /*
     *  Entries are two words wide, so step by two.  Starting from the
     *  invalid pointer 0 yields the first candidate, 2.
     */
    for (next = (uint32_t) p + 2; next < st_om_ot_limit; next += 2) {
        if (!OM_free_bit((st_oop) next))
            return (st_oop) next;
    }
    return ST_OOP_INVALID;
}

/*  ----------  Reference counting  ----------  */

/*
 *  Counts saturate at ST_HUGE_SIZE.  Once an object's count reaches that
 *  ceiling the Blue Book never adjusts it again -- the object becomes
 *  permanent as far as counting is concerned, and only the marking
 *  collector can reclaim it.  This is the classic reference-counting escape
 *  hatch for a field too narrow to hold the true count.
 */

void
OM_increase_ref(st_oop p)
{
    unsigned    count;

    if (OM_is_int(p) || !OM_is_object(p))
        return;
    count = OM_count_bits(p);
    if (count < ST_HUGE_SIZE - 1)
        OM_set_count_bits(p, count + 1);
}

void
OM_decrease_ref(st_oop p)
{
    unsigned    count;

    if (OM_is_int(p) || !OM_is_object(p))
        return;
    count = OM_count_bits(p);
    if (count >= ST_HUGE_SIZE - 1)
        return;                     /*  stuck at the ceiling  */
    if (count == 0)
        return;
    OM_set_count_bits(p, count - 1);
    /*
     *  Reaching zero means nothing refers to this object any more, so it and
     *  everything it alone kept alive go back to the free lists.
     *
     *  This recurses through OM_deallocate.  Depth is bounded by the length
     *  of the longest chain of objects each holding the last reference to
     *  the next -- in a 2 MB heap of four-word objects that is a worst case
     *  around 130,000 frames, which no real object graph approaches but
     *  which a marking collector would sidestep entirely.  Revisit if a
     *  workload ever proves otherwise.
     */
    if (count == 1)
        OM_deallocate(p);
}

void
OM_store_pointer(uint32_t field, st_oop p, st_oop value)
{
    st_oop  old = OM_fetch_pointer(field, p);

    /*  Count up before down, so a self-store cannot transiently hit zero.  */
    OM_increase_ref(value);
    OM_store_word(field, p, value);
    OM_decrease_ref(old);
}

/*
 *  The same as the threaded memory's, and trivially so: this memory has one
 *  mutator, so "compare and swap" is compare and then swap with nothing
 *  able to intervene.  It exists here so that a primitive does not have to
 *  ask which memory it is running on, and so that Smalltalk written against
 *  it loads in both builds.
 */
int
OM_compare_and_swap_pointer(uint32_t field, st_oop p, st_oop expected,
                            st_oop value)
{
    if (OM_fetch_pointer(field, p) != expected)
        return 0;
    OM_store_pointer(field, p, value);
    return 1;
}

/*  ----------  Free storage  ----------  */

/*
 *  Free chunks are threaded through the heap itself, exactly as the Blue
 *  Book does it: word 0 of a free chunk holds its size and word 1 holds the
 *  location of the next chunk on the same list, with 0 terminating.  Only
 *  the list heads live outside the heap.
 *
 *  We rebuild these lists by sweeping at load time rather than trusting the
 *  free-list heads the saving VM left in the image.  A sweep derives the
 *  truth from the object table, which we have already validated against
 *  Xerox's own dumps, so it cannot inherit a stale or foreign layout.
 */

#define FREE_NEXT(seg, loc)     st_om_words[((uint32_t) (seg) << 16) | ((loc) + 1)]
#define FREE_SIZE(seg, loc)     st_om_words[((uint32_t) (seg) << 16) | (loc)]

/*  Location 0 cannot start a chunk, so it doubles as the list terminator.  */
#define FREE_END        0

static unsigned
list_for_size(uint32_t words)
{
    if (words >= ST_BIG_SIZE)
        return 0;
    return (unsigned) words;
}

static void
free_chunk_add(unsigned segment, uint16_t location, uint32_t words)
{
    unsigned    list = list_for_size(words);

    FREE_SIZE(segment, location) = (uint16_t) words;
    FREE_NEXT(segment, location) = free_lists[segment][list];
    free_lists[segment][list]    = location;
}

/*
 *  Hand the words in [from, to) of a segment to the free lists, honouring
 *  the two rules a chunk must obey: it cannot start at location 0, which is
 *  the list terminator, and it needs at least two words to hold its own size
 *  and link.  A chunk longer than a word can express is split.
 */
static void
release_gap(unsigned segment, uint32_t from, uint32_t to)
{
    if (from == 0)
        ++from;
    while (to > from + 1) {
        uint32_t    words = to - from;

        if (words > 65534)
            words = 65534;
        free_chunk_add(segment, (uint16_t) from, words);
        from += words;
    }
}

/*
 *  Rebuild the free lists and the object-table free chain from the loaded
 *  image.  Sorting live objects by address per segment turns the gaps
 *  between them into the free chunks.
 */
static int
compare_u32(const void *a, const void *b)
{
    uint32_t    x = *(const uint32_t *) a;
    uint32_t    y = *(const uint32_t *) b;

    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

int
OM_rebuild_free_lists(void)
{
    uint32_t   *starts;
    uint32_t    count = 0;
    uint32_t    entry;
    unsigned    segment;
    uint32_t    i;

    memset(free_lists, 0, sizeof free_lists);
    ot_free_head = ST_OOP_INVALID;

    starts = (uint32_t *) malloc(ST_OT_ENTRIES * sizeof *starts);
    if (!starts)
        return -1;

    /*  Collect every live chunk as (address, size) packed by address.  */
    for (entry = 2; entry < st_om_ot_limit; entry += 2) {
        st_oop  p = (st_oop) entry;

        if (OM_free_bit(p)) {
            /*  Thread the unused table entry onto the free chain.  */
            OM_set_location(p, ot_free_head);
            ot_free_head = p;
            continue;
        }
        starts[count++] = OM_chunk_addr(p);
    }
    qsort(starts, count, sizeof *starts, compare_u32);

    /*
     *  Walk each segment's live chunks in address order and hand the space
     *  between them to the free lists.
     *
     *  Location 0 is the list terminator, so no chunk may start there; a gap
     *  that begins at 0 gives up its first word.  A chunk also needs two
     *  words to hold its own size and link, so anything smaller is simply
     *  left stranded -- the Blue Book does the same.
     */
    for (segment = 0; segment < ST_HEAP_SEGMENTS; ++segment) {
        uint32_t    base = (uint32_t) segment << 16;
        uint32_t    next_free = 0;

        for (i = 0; i < count; ++i) {
            uint32_t    addr = starts[i];

            if (addr < base || addr >= base + ST_SEGMENT_WORDS)
                continue;
            if (addr > base + next_free)
                release_gap(segment, next_free, addr - base);
            next_free = addr - base + st_om_words[addr];
        }
        /*  Everything past the last object in this segment is free.  */
        release_gap(segment, next_free, ST_SEGMENT_WORDS);
    }
    free(starts);
    return 0;
}

uint32_t
OM_core_left(void)
{
    unsigned    segment;
    unsigned    list;
    uint32_t    total = 0;

    for (segment = 0; segment < ST_HEAP_SEGMENTS; ++segment) {
        for (list = 0; list <= ST_BIG_SIZE; ++list) {
            uint16_t    loc = free_lists[segment][list];

            while (loc != FREE_END) {
                total += FREE_SIZE(segment, loc);
                loc = FREE_NEXT(segment, loc);
            }
        }
    }
    return total;
}

uint32_t
OM_oops_left(void)
{
    st_oop      p = ot_free_head;
    uint32_t    n = 0;

    while (p != ST_OOP_INVALID) {
        ++n;
        p = OM_location(p);
    }
    return n;
}

/*  ----------  Garbage collection  ----------  */

static om_root_provider root_provider;
static st_oop          *mark_stack;
static uint32_t         mark_top;

void
OM_set_root_provider(om_root_provider provider)
{
    root_provider = provider;
}

/*
 *  Count one reference to an object, and queue it for scanning the first
 *  time it is reached.  Counts saturate, matching the ceiling the rest of
 *  the object memory observes.
 */
static void
mark_visit(st_oop p)
{
    unsigned    count;

    if (!OM_is_object(p))
        return;
    count = OM_count_bits(p);
    if (count < ST_HUGE_SIZE - 1)
        OM_set_count_bits(p, count + 1);
    if (count == 0 && mark_top < ST_OT_ENTRIES)
        mark_stack[mark_top++] = p;
}

uint32_t    st_om_collections;
uint32_t    st_om_reclaimed;

uint32_t
OM_collect(void)
{
    uint32_t    entry;
    uint32_t    reclaimed = 0;

    ++st_om_collections;

    mark_stack = (st_oop *) malloc(ST_OT_ENTRIES * sizeof *mark_stack);
    if (!mark_stack)
        return 0;
    mark_top = 0;

    /*  Every count starts at zero and is rebuilt by the walk.  */
    for (entry = 2; entry < st_om_ot_limit; entry += 2) {
        if (!OM_free_bit((st_oop) entry))
            OM_set_count_bits((st_oop) entry, 0);
    }

    /*
     *  The guaranteed pointers reach almost everything: nil, true, false,
     *  the classes the interpreter names directly, and the SystemDictionary,
     *  which holds every global.  The scheduler association reaches every
     *  process and therefore every suspended context.
     */
    for (entry = 2; entry <= ST_SELECTOR_CANNOT_INTERPRET; entry += 2)
        mark_visit((st_oop) entry);
    if (root_provider)
        root_provider(mark_visit);

    while (mark_top > 0) {
        st_oop      p = mark_stack[--mark_top];
        uint32_t    n;
        uint32_t    i;

        mark_visit(OM_class_of_object(p));

        /*
         *  A CompiledMethod is the one object whose pointer bit lies.  It is
         *  flagged non-pointer because its body is bytecodes, but its first
         *  words are the header and the literal frame, and those literals
         *  are object pointers: selectors, Associations for globals, and
         *  constants.  Walking only pointer-flagged objects would leave
         *  every literal reachable solely from a method unmarked, and the
         *  sweep would free them out from under the running image.
         *
         *  The literal count sits in bits 9..14 of the header, which is a
         *  SmallInteger, so on the raw pointer that is (h >> 1) & 63.
         */
        if (OM_fetch_class(p) == ST_CLASS_COMPILED_METHOD) {
            uint32_t    literals = (OM_fetch_pointer(0, p) >> 1) & 63;

            for (i = 0; i <= literals; ++i)
                mark_visit(OM_fetch_pointer(i, p));
            continue;
        }
        if (!OM_pointer_bit(p))
            continue;
        n = OM_fetch_word_length(p);
        for (i = 0; i < n; ++i)
            mark_visit(OM_fetch_pointer(i, p));
    }

    /*
     *  Anything still at zero was unreachable.  Free it directly rather than
     *  through OM_deallocate: the counts are already correct, so releasing
     *  its fields again would corrupt them.
     */
    for (entry = 2; entry < st_om_ot_limit; entry += 2) {
        st_oop  p = (st_oop) entry;

        if (OM_free_bit(p) || OM_count_bits(p) != 0)
            continue;
        free_chunk_add(OM_segment_bits(p), OM_location(p), OM_size_bits(p));
        OM_set_free_bit(p, 1);
        OM_set_location(p, ot_free_head);
        ot_free_head = p;
        ++reclaimed;
    }
    free(mark_stack);
    mark_stack = NULL;
    st_om_reclaimed += reclaimed;
    if (getenv("ST_GC_LOG"))
        fprintf(stderr, "  gc #%u reclaimed %u; %u words %u entries free\n",
                st_om_collections, reclaimed, OM_core_left(), OM_oops_left());
    return reclaimed;
}

/*  ----------  Allocation  ----------  */

static st_oop
ot_alloc(void)
{
    st_oop  p = ot_free_head;

    if (p == ST_OOP_INVALID)
        return ST_OOP_INVALID;
    ot_free_head = OM_location(p);
    OM_set_free_bit(p, 0);
    return p;
}

/*
 *  Find a chunk of at least `words`, splitting a larger one when the
 *  remainder is big enough to stand on its own.  Returns 0 if no segment can
 *  satisfy the request.
 */
static int
chunk_alloc(uint32_t words, unsigned *out_segment, uint16_t *out_location)
{
    unsigned    segment;

    if (words < 2)
        words = 2;
    for (segment = 0; segment < ST_HEAP_SEGMENTS; ++segment) {
        unsigned    order[ST_BIG_SIZE + 1];
        unsigned    n = 0;
        unsigned    list;
        unsigned    k;

        /*
         *  Search order: the exact size, then each larger exact size, then
         *  the big list.  The big list must come last but must be reachable
         *  -- most free space lives there, so a small request that never
         *  looked at it would fail with megabytes available.
         */
        if (words < ST_BIG_SIZE) {
            for (list = (unsigned) words; list < ST_BIG_SIZE; ++list)
                order[n++] = list;
        }
        order[n++] = 0;

        for (k = 0; k < n; ++k) {
            uint16_t    prev;
            uint16_t    loc;

            list = order[k];
            prev = FREE_END;
            loc  = free_lists[segment][list];
            while (loc != FREE_END) {
                uint32_t    size = FREE_SIZE(segment, loc);

                /*
                 *  Take an exact fit, or one large enough that the remainder
                 *  can still carry its own size and link.  A chunk that
                 *  would leave exactly one word over is passed by: absorbing
                 *  that word would lose it on every cycle, and stranding it
                 *  would do the same.
                 */
                if (size == words || size >= words + 2) {
                    uint16_t    next = FREE_NEXT(segment, loc);
                    uint32_t    rest = size - words;

                    if (prev == FREE_END)
                        free_lists[segment][list] = next;
                    else
                        FREE_NEXT(segment, prev) = next;
                    if (rest >= 2)
                        free_chunk_add(segment, (uint16_t) (loc + words), rest);
                    *out_segment  = segment;
                    *out_location = loc;
                    return 1;
                }
                prev = loc;
                loc  = FREE_NEXT(segment, loc);
            }
        }
    }
    return 0;
}

static st_oop
instantiate(st_oop class_pointer, uint32_t data_words, unsigned pointer_bit,
            unsigned odd_bit, st_oop fill)
{
    uint32_t    total = data_words + ST_HEADER_SIZE;
    unsigned    segment;
    uint16_t    location;
    st_oop      p;
    uint32_t    i;

    if (total > 65535)
        return ST_OOP_INVALID;
    if (!chunk_alloc(total, &segment, &location)) {
        /*  Out of heap: collect once and try again before giving up.  */
        if (OM_collect() == 0 || !chunk_alloc(total, &segment, &location))
            return ST_OOP_INVALID;
    }
    p = ot_alloc();
    if (p == ST_OOP_INVALID) {
        free_chunk_add(segment, location, total);
        if (OM_collect() == 0)
            return ST_OOP_INVALID;
        if (!chunk_alloc(total, &segment, &location))
            return ST_OOP_INVALID;
        p = ot_alloc();
        if (p == ST_OOP_INVALID) {
            free_chunk_add(segment, location, total);
            return ST_OOP_INVALID;
        }
    }
    OM_set_segment_bits(p, segment);
    OM_set_location(p, location);
    OM_set_count_bits(p, 0);
    OM_set_pointer_bit(p, pointer_bit);
    OM_set_odd_bit(p, odd_bit);
    OM_set_size_bits(p, (uint16_t) total);
    OM_set_class_of_object(p, class_pointer);
    OM_increase_ref(class_pointer);
    for (i = 0; i < data_words; ++i) {
        OM_store_word(i, p, fill);
        if (pointer_bit)
            OM_increase_ref(fill);      /*  nil is an object like any other  */
    }
    return p;
}

st_oop
OM_instantiate_pointers(st_oop class_pointer, uint32_t size)
{
    return instantiate(class_pointer, size, 1, 0, ST_NIL);
}

st_oop
OM_instantiate_words(st_oop class_pointer, uint32_t size)
{
    return instantiate(class_pointer, size, 0, 0, 0);
}

st_oop
OM_instantiate_bytes(st_oop class_pointer, uint32_t size)
{
    uint32_t    words = (size + 1) / 2;

    return instantiate(class_pointer, words, 0, size & 1, 0);
}

st_oop
OM_next_instance_after(st_oop after, st_oop class_oop)
{
    /*
     *  Object pointers are word offsets into the object table, two words to
     *  an entry, so the step is two and the first candidate is two past the
     *  one given.
     */
    st_oop  p = (after == ST_OOP_INVALID) ? 2 : after + 2;

    for (; p < st_om_ot_limit; p += 2) {
        if (!OM_is_object(p))
            continue;
        if (OM_fetch_class(p) == class_oop)
            return p;
    }
    return ST_OOP_INVALID;
}

/*
 *  Whether a two-way become: may proceed.  See the same function in om_mt.c
 *  for the argument; this is the same rule stated for this memory.
 *
 *  The guaranteed pointers of Chapter 27 -- nil, true, false, the fixed
 *  classes and the fixed selectors, OOPs 2 through 56 -- name themselves for
 *  the whole image's life.  One of them may exchange bodies with an object
 *  of its OWN CLASS, which is what SystemDictionary>>grow does every time a
 *  global is added, and with nothing else: `nil become: Object new' leaves
 *  an image in which nil is somebody else's body.
 */
int
OM_can_swap_identities(st_oop a, st_oop b)
{
    if (!OM_is_object(a) || !OM_is_object(b))
        return 0;
    if (a == b)
        return 1;
    if ((a <= ST_SELECTOR_CANNOT_INTERPRET || b <= ST_SELECTOR_CANNOT_INTERPRET)
     && OM_fetch_class(a) != OM_fetch_class(b))
        return 0;
    return 1;
}

static void
swap_identities_unguarded(st_oop a, st_oop b)
{
    uint16_t    a0;
    uint16_t    a1;
    unsigned    ca;
    unsigned    cb;

    a0 = st_om_ot[a];
    a1 = st_om_ot[a + 1];
    st_om_ot[a]     = st_om_ot[b];
    st_om_ot[a + 1] = st_om_ot[b + 1];
    st_om_ot[b]     = a0;
    st_om_ot[b + 1] = a1;

    /*  Counts belong to the identity, not the body, so put them back.  */
    ca = OM_count_bits(b);
    cb = OM_count_bits(a);
    OM_set_count_bits(a, ca);
    OM_set_count_bits(b, cb);
}

/*
 *  The bootstrap's own door -- see the note beside the same function in
 *  om_mt.c.  Giving ST_SMALLTALK its Dictionary means exchanging a
 *  guaranteed pointer, which is legitimate exactly once and never again.
 */
void
OM_swap_identities_at_boot(st_oop a, st_oop b)
{
    if (!OM_is_object(a) || !OM_is_object(b) || a == b)
        return;
    swap_identities_unguarded(a, b);
}

int
OM_swap_identities(st_oop a, st_oop b)
{
    if (!OM_can_swap_identities(a, b))
        return 0;
    if (a == b)
        return 1;
    swap_identities_unguarded(a, b);
    return 1;
}

void
OM_set_root_forwarder(om_root_forwarder forwarder, om_root_pin_fn pinned)
{
    (void) forwarder;
    (void) pinned;
}

/*
 *  Nothing to guard: this memory runs one thread and holds no raw pointer
 *  into an object's body across a send.
 */
void
OM_set_swap_guard(om_root_pin_fn pinned)
{
    (void) pinned;
}

st_oop
OM_instantiate_ephemeron(st_oop class_pointer, uint32_t size)
{
    return OM_instantiate_pointers(class_pointer, size);
}

int
OM_can_forward_identity(st_oop from, st_oop to)
{
    (void) from;
    (void) to;
    return 0;
}

int
OM_forward_identity(st_oop from, st_oop to)
{
    (void) from;
    (void) to;
    return 0;
}

void
OM_deallocate(st_oop p)
{
    unsigned    segment;
    uint16_t    location;
    uint32_t    total;

    if (!OM_is_object(p))
        return;
    /*
     *  Release what this object referred to before releasing the object, so
     *  a field pointing back at it sees a count that is still positive.
     */
    if (OM_pointer_bit(p)) {
        uint32_t    n = OM_fetch_word_length(p);
        uint32_t    i;

        for (i = 0; i < n; ++i)
            OM_decrease_ref(OM_fetch_pointer(i, p));
    }
    OM_decrease_ref(OM_class_of_object(p));

    segment  = OM_segment_bits(p);
    location = OM_location(p);
    total    = OM_size_bits(p);

    OM_set_free_bit(p, 1);
    OM_set_count_bits(p, 0);
    OM_set_location(p, ot_free_head);
    ot_free_head = p;
    free_chunk_add(segment, location, total);
}

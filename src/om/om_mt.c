/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The 64-bit object memory.  See om_mt.h for the design and the reasoning
 *  behind keeping an object table.
 */

#include "om_mt.h"
#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

st_atomic_ptr  *st_om_table;
uint32_t         st_om_table_size;
st_atomic_uint   st_om_table_limit;

uint32_t     st_om_collections;
uint32_t     st_om_reclaimed;
uint32_t     st_om_weak_cleared;

static uint32_t     free_head;          /*  index chain through class_oop  */

/*
 *  Guards the table's own bookkeeping -- the free chain, the used limit, and
 *  growth.  It is deliberately narrow: it is never held while an object's
 *  fields are released, because that release recurses back into
 *  OM_deallocate and would deadlock on a non-recursive lock.
 */
static st_mutex     table_lock;
static int          table_lock_ready;
static uint32_t     live_objects;
static uint64_t     live_bytes;
static st_atomic_uint   next_hash;

#define TABLE_INITIAL   4096
#define FREE_END        0

/*  ----------  Lifecycle  ----------  */

int
OM_init(void)
{
    OM_shutdown();
    /*
     *  calloc of this size is a lazy mapping on every platform we target:
     *  the pages arrive as they are first written, so the cost here is
     *  address space rather than memory.
     */
    st_om_table = (st_atomic_ptr *) calloc(ST_OM_MAX_OBJECTS,
                                          sizeof *st_om_table);
    if (!st_om_table)
        return -1;
    st_om_table_size  = ST_OM_MAX_OBJECTS;
    /*
     *  Index 0 is never handed out: object pointer 0 means "invalid", and
     *  index 1 is nil, whose pointer is 2.
     */
    if (!table_lock_ready) {
        if (ST_mutex_init(&table_lock) != 0)
            return -1;
        table_lock_ready = 1;
    }
    ST_store_seq(&st_om_table_limit, 1);
    free_head         = FREE_END;
    live_objects      = 0;
    live_bytes        = 0;
    ST_store_seq(&next_hash, 1);
    return 0;
}

void
OM_shutdown(void)
{
    uint32_t    i;

    if (st_om_table) {
        uint32_t    limit = (uint32_t) ST_load_relaxed(&st_om_table_limit);

        for (i = 0; i < limit; ++i)
            free(OM_table_get(i));
        free(st_om_table);
    }
    st_om_table       = NULL;
    st_om_table_size  = 0;
    ST_store_seq(&st_om_table_limit, 0);
    free_head         = FREE_END;
    live_objects      = 0;
    live_bytes        = 0;
}

int
OM_is_object(st_oop p)
{
    uint32_t    index;

    if (p == ST_OOP_INVALID || (p & 1))
        return 0;
    index = (uint32_t) (p >> 1);
    /*
     *  Acquire, to pair with the release in table_alloc: seeing an index
     *  below the limit must also mean seeing the header stored there.
     */
    if (index == 0 || index >= (uint32_t) ST_load_acquire(&st_om_table_limit))
        return 0;
    {
        om_header  *head = OM_table_get(index);

        if (!head)
            return 0;
        return (head->flags & ST_FMT_FREE) == 0;
    }
}

/*  ----------  Allocation  ----------  */

static uint32_t
table_alloc_locked(void)
{
    uint32_t    index;

    if (free_head != FREE_END) {
        index = free_head;
        /*  The free chain threads through the unused class field.  */
        free_head = (uint32_t) OM_table_get(index)->class_oop;
        free(OM_table_get(index));
        OM_table_set(index, NULL);
        return index;
    }
    /*
     *  A fresh index.  The limit is NOT published here: the caller stores
     *  the header first and then releases it, so no reader can see an index
     *  in range before the entry behind it exists.
     */
    {
        uint32_t    next = (uint32_t) ST_load_relaxed(&st_om_table_limit);

        if (next >= st_om_table_size)
            return 0;
        return next;
    }
}

static st_oop
instantiate(st_oop class_pointer, uint32_t size, uint32_t format,
            size_t bytes)
{
    uint32_t    index;
    om_header  *head;

    head = (om_header *) calloc(1, sizeof *head + bytes);
    if (!head) {
        /*
         *  Collect and TRY AGAIN, whatever the collection answered.
         *
         *  OM_collect answers how many objects it freed, and treating zero
         *  as "cannot allocate" is wrong the moment there is more than one
         *  worker: two threads both find themselves short, the first
         *  collects and frees plenty, the second then collects and frees
         *  NOTHING because the first already did -- and gives up, with the
         *  memory it needed sitting there free.  What the collection
         *  freed is not the question; whether the retry succeeds is.
         */
        (void) OM_collect();
        head = (om_header *) calloc(1, sizeof *head + bytes);
        if (!head)
            return ST_OOP_INVALID;
    }
    head->class_oop = class_pointer;
    head->size      = size;
    head->flags     = format;
    ST_store_relaxed(&head->refcount, 0);
    head->hash      = (uint32_t) ST_fetch_add_relaxed(&next_hash, 1);

    /*
     *  Claim an index, store the entry, and only then publish the limit.
     *  A reader that sees the new limit is guaranteed by the release to see
     *  the header too.
     */
    ST_mutex_lock(&table_lock);
    index = table_alloc_locked();
    if (index == 0) {
        /*
         *  The table is full, which is a reason to collect and was not
         *  treated as one.
         *
         *  There are two ways to run out here and only one of them was
         *  handled: a failed calloc collects and retries, a few lines above,
         *  but a full object table simply gave up.  So a long run died with
         *  every one of the four million table entries in use and three
         *  hundred million words of heap still free, having collected once
         *  in a hundred and seventy-four million bytecodes.  The desktop
         *  loop allocates a context per iteration and asks for nothing else,
         *  so it is exactly the shape of program that exhausts the table
         *  first.
         *
         *  The lock is dropped before collecting.  A collection parks every
         *  other worker at a safepoint and then walks the table; holding the
         *  table lock across that deadlocks against any worker that reaches
         *  its safepoint by way of an allocation.
         */
        ST_mutex_unlock(&table_lock);
        /*
         *  Again: collect, then retry regardless of what it freed.  This
         *  is the one that was actually failing -- "out of memory
         *  activating a method: 1914321 words and 4177478 object table
         *  entries free", once in a dozen runs of the scaling benchmark
         *  with thirty-one workers, which is exactly the shape of two
         *  collections racing.  One worker's entire share of the work
         *  vanished and the answer came out short.
         */
        (void) OM_collect();
        ST_mutex_lock(&table_lock);
        index = table_alloc_locked();
        if (index == 0) {
            ST_mutex_unlock(&table_lock);
            free(head);
            return ST_OOP_INVALID;
        }
    }
    OM_table_set(index, head);
    if (index >= (uint32_t) ST_load_relaxed(&st_om_table_limit))
        ST_store_release(&st_om_table_limit, index + 1);
    live_bytes += bytes;
    ++live_objects;
    ST_mutex_unlock(&table_lock);

    OM_increase_ref(class_pointer);
    return (st_oop) index << 1;
}

st_oop
OM_instantiate_pointers(st_oop class_pointer, uint32_t size)
{
    st_oop      p = instantiate(class_pointer, size, ST_FMT_POINTERS,
                                (size_t) size * sizeof(st_oop));
    uint32_t    i;

    if (p == ST_OOP_INVALID)
        return p;
    /*  calloc gave zeroes; Smalltalk wants nil, which is a different value. */
    for (i = 0; i < size; ++i) {
        ST_oop_store(&((st_oop *) OM_body(p))[i], ST_NIL);
        OM_increase_ref(ST_NIL);
    }
    return p;
}

/*
 *  A weak object: the indexed fields past `fixed` are not traced.
 *
 *  `fixed` is remembered in the header's spare bits rather than looked up
 *  from the class at collection time, because the collector runs at a
 *  safepoint over the raw table and asking a class for its shape there
 *  would mean following pointers it is in the middle of recounting.
 */
st_oop
OM_instantiate_weak(st_oop class_pointer, uint32_t size, uint32_t fixed)
{
    st_oop      p = OM_instantiate_pointers(class_pointer, size);
    om_header  *head;

    if (p == ST_OOP_INVALID)
        return p;
    head = OM_head(p);
    head->flags |= ST_FMT_WEAK;
    /*  Up to 63 named fields ahead of the weak part, which is ample.  */
    head->flags |= (fixed & 0x3F) << 16;
    return p;
}

/*  How many fields at the front of a weak object are still strong.  */
static uint32_t
weak_fixed_fields(const om_header *head)
{
    return (head->flags >> 16) & 0x3F;
}

st_oop
OM_instantiate_words(st_oop class_pointer, uint32_t size)
{
    return instantiate(class_pointer, size, ST_FMT_WORDS,
                       (size_t) size * sizeof(uint16_t));
}

st_oop
OM_instantiate_bytes(st_oop class_pointer, uint32_t size)
{
    return instantiate(class_pointer, size, ST_FMT_BYTES, size);
}

void
OM_deallocate(st_oop p)
{
    om_header  *head;
    uint32_t    index;

    if (!OM_is_object(p))
        return;
    index = (uint32_t) (p >> 1);
    head  = OM_table_get(index);

    if (head->flags & ST_FMT_POINTERS) {
        uint32_t    i;

        for (i = 0; i < head->size; ++i)
            OM_decrease_ref(ST_oop_load(&((st_oop *) (head + 1))[i]));
    }
    OM_decrease_ref(head->class_oop);

    /*
     *  Fields are released above without the lock, because that recurses.
     *  Only the bookkeeping below is guarded.
     */
    ST_mutex_lock(&table_lock);
    live_bytes -= (head->flags & ST_FMT_POINTERS)
                    ? (uint64_t) head->size * sizeof(st_oop)
                    : ((head->flags & ST_FMT_WORDS)
                        ? (uint64_t) head->size * sizeof(uint16_t)
                        : head->size);
    --live_objects;

    /*
     *  Keep a minimal header so the free chain has somewhere to live and so
     *  a stale pointer sees the free flag rather than released memory.
     */
    head = (om_header *) realloc(head, sizeof *head);
    if (!head) {
        /*  realloc shrinking should not fail; if it does, keep the old one. */
        head = OM_table_get(index);
    }
    head->flags     = ST_FMT_FREE;
    head->size      = 0;
    ST_store_relaxed(&head->refcount, 0);
    head->class_oop = free_head;
    OM_table_set(index, head);
    free_head = index;
    ST_mutex_unlock(&table_lock);
}


st_oop
OM_next_instance_after(st_oop after, st_oop class_oop)
{
    uint32_t    index = (after == ST_OOP_INVALID) ? 1
                        : (uint32_t) (after >> 1) + 1;
    uint32_t    limit = (uint32_t) ST_load_acquire(&st_om_table_limit);

    for (; index < limit; ++index) {
        om_header  *head = OM_table_get(index);
        st_oop      p;

        if (!head || (head->flags & ST_FMT_FREE) != 0)
            continue;
        p = (st_oop) index << 1;
        if (OM_fetch_class(p) == class_oop)
            return p;
    }
    return ST_OOP_INVALID;
}

void
OM_swap_identities(st_oop a, st_oop b)
{
    uint32_t    ia;
    uint32_t    ib;
    om_header  *t;

    if (!OM_is_object(a) || !OM_is_object(b))
        return;
    ia = (uint32_t) (a >> 1);
    ib = (uint32_t) (b >> 1);
    /*
     *  Two-way become: the bodies stay put and only the table entries move,
     *  so no reference anywhere in the heap has to be found or rewritten.
     *  Counts belong to the identity rather than the body, so they are put
     *  back after the swap.
     */
    {
        om_header  *ha = OM_table_get(ia);
        om_header  *hb = OM_table_get(ib);
        unsigned    ca = (unsigned) ST_load_relaxed(&ha->refcount);
        unsigned    cb = (unsigned) ST_load_relaxed(&hb->refcount);

        t = ha;
        OM_table_set(ia, hb);
        OM_table_set(ib, t);
        ST_store_relaxed(&hb->refcount, ca);
        ST_store_relaxed(&ha->refcount, cb);
    }
}

/*  ----------  Reference counting  ----------  */

void
OM_increase_ref_object(st_oop p)
{
    if (!OM_is_object(p))
        return;
    /*
     *  Relaxed: two threads counting the same object must not lose an
     *  update, but nothing else is ordered by this.
     */
    ST_fetch_add_relaxed(&OM_head(p)->refcount, 1);
}

void
OM_decrease_ref_object(st_oop p)
{
    om_header  *head;
    unsigned    before;

    if (!OM_is_object(p))
        return;
    head   = OM_head(p);
    /*
     *  Acquire-release, because the thread that drops the last reference is
     *  about to read and free the object, and must see every write another
     *  thread made before releasing its own reference.
     */
    before = (unsigned) ST_fetch_sub_acq_rel(&head->refcount, 1);
    if (before == 0) {
        /*  Already zero; put it back rather than wrap to four billion.  */
        ST_fetch_add_relaxed(&head->refcount, 1);
        return;
    }
    if (before != 1)
        return;

    /*
     *  The count reached zero.  Freeing the body here is safe only when this
     *  thread is the only one that could be looking at it.
     *
     *  With a worker pool running it is not.  Another thread can hold the
     *  same object pointer and be about to dereference it -- it has not
     *  stored a reference, so no count protects it, and a thread that
     *  reclaimed the body would pull it out from under the reader.  This is
     *  the hazard that makes reference counting insufficient for a shared
     *  mutable heap however carefully the counts are maintained.
     *
     *  So with threads running, reclamation is deferred: the object is
     *  simply unreachable, and the next collection frees it at a safepoint,
     *  where by construction nobody is reading.  Reference counting becomes
     *  a hint about when collecting is worthwhile rather than the mechanism
     *  that reclaims.
     */
    if (WORKER_count() == 0)
        OM_deallocate(p);
}

void
OM_store_pointer(uint32_t field, st_oop p, st_oop value)
{
    st_oop  old = OM_fetch_pointer(field, p);

    OM_increase_ref(value);
    ST_oop_store(&((st_oop *) OM_body(p))[field], value);
    OM_decrease_ref(old);
}

int
OM_compare_and_swap_pointer(uint32_t field, st_oop p, st_oop expected,
                            st_oop value)
{
    _Atomic st_oop *slot = (_Atomic st_oop *) &((st_oop *) OM_body(p))[field];
    st_oop          seen = expected;

    /*
     *  The new value is counted BEFORE the attempt and released again if
     *  the attempt fails.  The other order -- swap, then count -- leaves a
     *  window in which the slot refers to an object whose count does not
     *  know about it, and a collection landing there frees a live object.
     *  Counting first can only ever be conservative, which is the side to
     *  be wrong on.
     */
    OM_increase_ref(value);
    if (!ST_cas_strong(slot, &seen, value)) {
        OM_decrease_ref(value);
        return 0;
    }
    OM_decrease_ref(expected);
    return 1;
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
    uint32_t    index = (uint32_t) (p >> 1);

    for (++index; index < (uint32_t) ST_load_acquire(&st_om_table_limit); ++index) {
        if (OM_table_get(index)
         && (OM_table_get(index)->flags & ST_FMT_FREE) == 0)
            return (st_oop) index << 1;
    }
    return ST_OOP_INVALID;
}

uint32_t
OM_core_left(void)
{
    /*  Bounded only by the host allocator; report what is in use instead.  */
    return (uint32_t) (live_bytes / 2);
}

uint32_t
OM_oops_left(void)
{
    uint32_t    n = 0;
    uint32_t    index = free_head;

    while (index != FREE_END) {
        ++n;
        index = (uint32_t) OM_table_get(index)->class_oop;
    }
    return n + (st_om_table_size
                - (uint32_t) ST_load_relaxed(&st_om_table_limit));
}

/*  ----------  Garbage collection  ----------  */

static om_root_provider root_provider;
static st_oop          *mark_stack;
static uint32_t         mark_top;
static uint32_t         mark_capacity;

void
OM_set_root_provider(om_root_provider provider)
{
    root_provider = provider;
}

static void
mark_visit(st_oop p)
{
    om_header  *head;

    if (!OM_is_object(p))
        return;
    head = OM_head(p);
    if (ST_fetch_add_relaxed(&head->refcount, 1) == 0
     && mark_top < mark_capacity)
        mark_stack[mark_top++] = p;
}

static uint32_t
collect_at_safepoint(void *unused)
{
    uint32_t    index;
    uint32_t    reclaimed = 0;

    (void) unused;
    ++st_om_collections;
    mark_capacity = (uint32_t) ST_load_relaxed(&st_om_table_limit) + 1;
    mark_stack = (st_oop *) malloc((size_t) mark_capacity * sizeof *mark_stack);
    if (!mark_stack)
        return 0;
    mark_top = 0;

    for (index = 1; index < (uint32_t) ST_load_relaxed(&st_om_table_limit); ++index) {
        if (OM_table_get(index))
            ST_store_relaxed(&OM_table_get(index)->refcount, 0);
    }

    for (index = 2; index <= ST_SELECTOR_CANNOT_INTERPRET; index += 2)
        mark_visit((st_oop) index);
    if (root_provider)
        root_provider(mark_visit);

    while (mark_top > 0) {
        st_oop      p = mark_stack[--mark_top];
        om_header  *head = OM_head(p);
        uint32_t    i;

        mark_visit(head->class_oop);

        /*
         *  A CompiledMethod is flagged non-pointer because its body is
         *  bytecodes, but its leading words are the header and literal
         *  frame, and those are object pointers.  Missing them frees every
         *  selector and global binding reachable only from a method.
         */
        if (head->class_oop == ST_CLASS_COMPILED_METHOD) {
            /*
             *  A method is a byte object, so its size counts bytes while the
             *  header and literals are read as pointers.  The bound has to
             *  be in pointer-sized slots or the walk runs eight times too
             *  far off the end.
             */
            uint32_t    slots = head->size / (uint32_t) sizeof(st_oop);
            uint32_t    literals;

            if (slots == 0)
                continue;
            literals = (uint32_t) ((OM_fetch_pointer(0, p) >> 1) & 63);
            for (i = 0; i <= literals && i < slots; ++i)
                mark_visit(OM_fetch_pointer(i, p));
            continue;
        }
        if (!(head->flags & ST_FMT_POINTERS))
            continue;
        if (head->flags & ST_FMT_WEAK) {
            /*
             *  A weak object's indexed fields are deliberately not visited.
             *  That is the whole mechanism: what only a weak reference
             *  points at ends the walk with a count of zero and is
             *  collected, and the reference is nilled below.
             */
            uint32_t    fixed = weak_fixed_fields(head);

            for (i = 0; i < fixed && i < head->size; ++i)
                mark_visit(ST_oop_load(&((st_oop *) (head + 1))[i]));
            continue;
        }
        for (i = 0; i < head->size; ++i)
            mark_visit(ST_oop_load(&((st_oop *) (head + 1))[i]));
    }

    /*
     *  Nil the weak references to things that did not survive.
     *
     *  Between the walk and the sweep, which is the only moment both facts
     *  are available: every count is exact, and nothing has been freed yet,
     *  so a dead target can still be recognised by its zero count rather
     *  than by reading memory that has been handed back.
     */
    for (index = 1; index < (uint32_t) ST_load_relaxed(&st_om_table_limit);
         ++index) {
        om_header  *head = OM_table_get(index);
        st_oop     *slots;
        uint32_t    fixed;
        uint32_t    i;

        if (!head || (head->flags & ST_FMT_FREE)
         || !(head->flags & ST_FMT_WEAK))
            continue;
        if (ST_load_relaxed(&head->refcount) == 0)
            continue;               /*  the weak object is itself dying  */
        slots = (st_oop *) (head + 1);
        fixed = weak_fixed_fields(head);
        for (i = fixed; i < head->size; ++i) {
            st_oop      target = ST_oop_load(&slots[i]);
            om_header  *th;

            if (!OM_is_object(target))
                continue;
            th = OM_head(target);
            if (!th || (th->flags & ST_FMT_FREE)
             || ST_load_relaxed(&th->refcount) != 0)
                continue;
            ST_oop_store(&slots[i], ST_NIL);
            OM_increase_ref(ST_NIL);
            ++st_om_weak_cleared;
        }
    }

    for (index = 1; index < (uint32_t) ST_load_relaxed(&st_om_table_limit); ++index) {
        st_oop  p = (st_oop) index << 1;

        if (!OM_table_get(index))
            continue;
        if (OM_table_get(index)->flags & ST_FMT_FREE)
            continue;
        if (ST_load_relaxed(&OM_table_get(index)->refcount) != 0)
            continue;
        /*
         *  Unreachable.  Release it directly rather than through
         *  OM_deallocate: the counts are already exact, so decrementing its
         *  fields again would corrupt them.
         */
        {
            om_header  *head = OM_table_get(index);

            live_bytes -= (head->flags & ST_FMT_POINTERS)
                            ? (uint64_t) head->size * sizeof(st_oop)
                            : ((head->flags & ST_FMT_WORDS)
                                ? (uint64_t) head->size * sizeof(uint16_t)
                                : head->size);
            --live_objects;
            head = (om_header *) realloc(head, sizeof *head);
            if (!head)
                head = OM_table_get(index);
            head->flags     = ST_FMT_FREE;
            head->size      = 0;
            ST_store_relaxed(&head->refcount, 0);
            head->class_oop = free_head;
            OM_table_set(index, head);
            free_head = index;
            ++reclaimed;
        }
        (void) p;
    }
    free(mark_stack);
    mark_stack = NULL;
    st_om_reclaimed += reclaimed;
    if (getenv("ST_GC_LOG"))
        fprintf(stderr, "  gc #%u reclaimed %u; %u live objects\n",
                st_om_collections, reclaimed, live_objects);
    return reclaimed;
}

/*
 *  A collection rewrites every reference count and frees object bodies, so
 *  no mutator may be running.  Parking them all is what the safepoint
 *  protocol is for.
 */
uint32_t
OM_collect(void)
{
    return WORKER_at_safepoint(collect_at_safepoint, NULL);
}

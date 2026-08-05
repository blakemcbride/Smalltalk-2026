/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The 64-bit object memory.  See om_mt.h for the design and the reasoning
 *  behind keeping an object table.
 */

#include "om_mt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

om_header  **st_om_table;
uint32_t     st_om_table_size;
uint32_t     st_om_table_limit;

uint32_t     st_om_collections;
uint32_t     st_om_reclaimed;

static uint32_t     free_head;          /*  index chain through class_oop  */
static uint32_t     live_objects;
static uint64_t     live_bytes;
static uint32_t     next_hash = 1;

#define TABLE_INITIAL   4096
#define FREE_END        0

/*  ----------  Lifecycle  ----------  */

int
OM_init(void)
{
    OM_shutdown();
    st_om_table = (om_header **) calloc(TABLE_INITIAL, sizeof *st_om_table);
    if (!st_om_table)
        return -1;
    st_om_table_size  = TABLE_INITIAL;
    /*
     *  Index 0 is never handed out: object pointer 0 means "invalid", and
     *  index 1 is nil, whose pointer is 2.
     */
    st_om_table_limit = 1;
    free_head         = FREE_END;
    live_objects      = 0;
    live_bytes        = 0;
    next_hash         = 1;
    return 0;
}

void
OM_shutdown(void)
{
    uint32_t    i;

    if (st_om_table) {
        for (i = 0; i < st_om_table_limit; ++i)
            free(st_om_table[i]);
        free(st_om_table);
    }
    st_om_table       = NULL;
    st_om_table_size  = 0;
    st_om_table_limit = 0;
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
    if (index == 0 || index >= st_om_table_limit)
        return 0;
    if (!st_om_table[index])
        return 0;
    return (st_om_table[index]->flags & ST_FMT_FREE) == 0;
}

/*  ----------  Allocation  ----------  */

static int
grow_table(void)
{
    uint32_t        want = st_om_table_size * 2;
    om_header     **grown;

    grown = (om_header **) realloc(st_om_table, (size_t) want * sizeof *grown);
    if (!grown)
        return 0;
    memset(grown + st_om_table_size, 0,
           (size_t) (want - st_om_table_size) * sizeof *grown);
    st_om_table      = grown;
    st_om_table_size = want;
    return 1;
}

static uint32_t
table_alloc(void)
{
    uint32_t    index;

    if (free_head != FREE_END) {
        index = free_head;
        /*  The free chain threads through the unused class field.  */
        free_head = (uint32_t) st_om_table[index]->class_oop;
        free(st_om_table[index]);
        st_om_table[index] = NULL;
        return index;
    }
    if (st_om_table_limit >= st_om_table_size && !grow_table())
        return 0;
    return st_om_table_limit++;
}

static st_oop
instantiate(st_oop class_pointer, uint32_t size, uint32_t format,
            size_t bytes)
{
    uint32_t    index;
    om_header  *head;

    head = (om_header *) calloc(1, sizeof *head + bytes);
    if (!head) {
        if (OM_collect() == 0)
            return ST_OOP_INVALID;
        head = (om_header *) calloc(1, sizeof *head + bytes);
        if (!head)
            return ST_OOP_INVALID;
    }
    index = table_alloc();
    if (index == 0) {
        free(head);
        return ST_OOP_INVALID;
    }
    head->class_oop = class_pointer;
    head->size      = size;
    head->flags     = format;
    head->refcount  = 0;
    head->hash      = next_hash++;
    st_om_table[index] = head;
    ++live_objects;
    live_bytes += bytes;

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
        ((st_oop *) OM_body(p))[i] = ST_NIL;
        OM_increase_ref(ST_NIL);
    }
    return p;
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
    head  = st_om_table[index];

    if (head->flags & ST_FMT_POINTERS) {
        uint32_t    i;

        for (i = 0; i < head->size; ++i)
            OM_decrease_ref(((st_oop *) (head + 1))[i]);
    }
    OM_decrease_ref(head->class_oop);

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
        head = st_om_table[index];
    }
    head->flags     = ST_FMT_FREE;
    head->size      = 0;
    head->refcount  = 0;
    head->class_oop = free_head;
    st_om_table[index] = head;
    free_head = index;
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
        uint32_t    ca = st_om_table[ia]->refcount;
        uint32_t    cb = st_om_table[ib]->refcount;

        t = st_om_table[ia];
        st_om_table[ia] = st_om_table[ib];
        st_om_table[ib] = t;
        st_om_table[ia]->refcount = ca;
        st_om_table[ib]->refcount = cb;
    }
}

/*  ----------  Reference counting  ----------  */

void
OM_increase_ref(st_oop p)
{
    if (!OM_is_object(p))
        return;
    ++OM_head(p)->refcount;
}

void
OM_decrease_ref(st_oop p)
{
    om_header  *head;

    if (!OM_is_object(p))
        return;
    head = OM_head(p);
    if (head->refcount == 0)
        return;
    if (--head->refcount == 0)
        OM_deallocate(p);
}

void
OM_store_pointer(uint32_t field, st_oop p, st_oop value)
{
    st_oop  old = OM_fetch_pointer(field, p);

    OM_increase_ref(value);
    ((st_oop *) OM_body(p))[field] = value;
    OM_decrease_ref(old);
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

    for (++index; index < st_om_table_limit; ++index) {
        if (st_om_table[index]
         && (st_om_table[index]->flags & ST_FMT_FREE) == 0)
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
        index = (uint32_t) st_om_table[index]->class_oop;
    }
    return n + (st_om_table_size - st_om_table_limit);
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
    ++head->refcount;
    if (head->refcount == 1 && mark_top < mark_capacity)
        mark_stack[mark_top++] = p;
}

uint32_t
OM_collect(void)
{
    uint32_t    index;
    uint32_t    reclaimed = 0;

    ++st_om_collections;
    mark_capacity = st_om_table_limit + 1;
    mark_stack = (st_oop *) malloc((size_t) mark_capacity * sizeof *mark_stack);
    if (!mark_stack)
        return 0;
    mark_top = 0;

    for (index = 1; index < st_om_table_limit; ++index) {
        if (st_om_table[index])
            st_om_table[index]->refcount = 0;
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
            uint32_t    literals = (uint32_t)
                            ((OM_fetch_pointer(0, p) >> 1) & 63);

            for (i = 0; i <= literals && i < head->size; ++i)
                mark_visit(OM_fetch_pointer(i, p));
            continue;
        }
        if (!(head->flags & ST_FMT_POINTERS))
            continue;
        for (i = 0; i < head->size; ++i)
            mark_visit(((st_oop *) (head + 1))[i]);
    }

    for (index = 1; index < st_om_table_limit; ++index) {
        st_oop  p = (st_oop) index << 1;

        if (!st_om_table[index])
            continue;
        if (st_om_table[index]->flags & ST_FMT_FREE)
            continue;
        if (st_om_table[index]->refcount != 0)
            continue;
        /*
         *  Unreachable.  Release it directly rather than through
         *  OM_deallocate: the counts are already exact, so decrementing its
         *  fields again would corrupt them.
         */
        {
            om_header  *head = st_om_table[index];

            live_bytes -= (head->flags & ST_FMT_POINTERS)
                            ? (uint64_t) head->size * sizeof(st_oop)
                            : ((head->flags & ST_FMT_WORDS)
                                ? (uint64_t) head->size * sizeof(uint16_t)
                                : head->size);
            --live_objects;
            head = (om_header *) realloc(head, sizeof *head);
            if (!head)
                head = st_om_table[index];
            head->flags     = ST_FMT_FREE;
            head->size      = 0;
            head->refcount  = 0;
            head->class_oop = free_head;
            st_om_table[index] = head;
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

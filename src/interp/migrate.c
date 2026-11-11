/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Reshaping a class without losing another worker's writes.
 *
 *  WHAT WENT WRONG.  ClassDescription>>updateInstancesFrom: is 1983's, and
 *  1983 had one thread.  For every instance it makes an empty object of the
 *  new shape, copies the named fields across according to a permutation
 *  map, copies the indexed fields, and then sends become: -- four steps,
 *  written as four steps because nothing could come between them.
 *
 *  Here eight workers run at once, and something can.  A write to a field
 *  that lands AFTER this has read that field and BEFORE the become: goes
 *  into the body that the become: is about to leave behind: every reference
 *  in the image names the identity, the identity now names the new body,
 *  and the value written is in the old one.  The write is gone.  Nothing
 *  raises, nothing is logged, and the object looks perfectly ordinary
 *  afterwards -- it just holds a value one step stale.  A thousand
 *  instances kept to an invariant by one worker while another reshaped the
 *  class thirty times came back with 22 of them broken and no errors
 *  (Bugs4 REFLECTION-3).
 *
 *  The window cannot be closed in Smalltalk.  There is no lock a mutator
 *  takes: the mutator is `b := b + 1' in some method nobody wrote for this,
 *  and asking every store in the image to synchronise with a reshape that
 *  happens twice a year is the wrong trade by several orders of magnitude.
 *  So the exclusion goes where the collector's already is -- a safepoint,
 *  with every worker parked -- and the four steps happen inside one.
 *
 *  WHAT IS AND IS NOT DONE HERE.  Allocation is NOT: the new bodies are
 *  made in Smalltalk, before the safepoint, and handed in.  Allocating
 *  inside a safepoint can want a collection, a collection wants a
 *  safepoint, and WORKER_at_safepoint does not nest -- it would be a
 *  deadlock, not a slow path.  What IS done here is the copying and the
 *  identity swap, which is exactly the part that has to be indivisible.
 *
 *  All of them together rather than one instance per safepoint, which would
 *  also be correct: parking every worker a thousand times to copy two
 *  fields each time costs more than the whole migration, and doing them
 *  together additionally means no worker can ever observe a population
 *  half in one shape and half in the other.
 *
 *  THE SWAP IS OM_swap_identities_at_boot, not OM_swap_identities, and the
 *  difference is the safepoint: the public one opens its own, which is the
 *  nesting above.  What the public one does inside that safepoint is ask
 *  OM_can_swap_identities and then swap, and that is what this does -- the
 *  question asked HERE, inside the safepoint, for the reason om_mt.c gives
 *  at swap_identities_locked: whether an object is pinned is a question
 *  about what some worker is executing right now, and the answer is only
 *  stable once they have all stopped.  Every pair is asked before any pair
 *  moves, the way the bulk one-way become already does, because forwarding
 *  three of five and then refusing leaves an image in a state nobody asked
 *  for and nobody can undo.
 */

#include "migrate.h"

#include "om.h"
#include "interp.h"
#include "prim.h"
#include "worker.h"

#include <stdlib.h>

typedef struct {
    st_oop      olds;           /*  Array of the instances as they are     */
    st_oop      news;           /*  Array of empty instances, new shape    */
    st_oop      map;            /*  new field i <- old field (map at: i)   */
    uint32_t    old_named;      /*  how many named fields the old shape had */
}   migrate_args;

/*
 *  Whether these two may be exchanged, asked of every pair before any pair
 *  is touched.  The three Arrays are refused explicitly: they are what this
 *  is reading, and an image that reshaped Array itself would otherwise
 *  swap the body out from under the loop.
 */
static int
pair_is_movable(const migrate_args *a, st_oop old, st_oop fresh)
{
    if (!OM_is_object(old) || !OM_is_object(fresh))
        return 0;
    if (old == fresh)
        return 0;
    if (OM_is_int(old) || OM_is_int(fresh))
        return 0;
    if (old == a->olds || old == a->news || old == a->map)
        return 0;
    if (fresh == a->olds || fresh == a->news || fresh == a->map)
        return 0;
    /*
     *  A pointer object and a byte object have nothing to copy between
     *  them.  updateInstancesFrom: makes the new one with the new class's
     *  own shape, so this differs only if the reshape changed the storage
     *  kind, which the caller should have refused.
     */
    if (OM_pointer_bit(old) != OM_pointer_bit(fresh))
        return 0;
    /*
     *  Nor a Symbol on either side (Bugs5 INTERP-7), for primitive 72's
     *  reason (Bugs3 B21): migrating #zork to a String left the symbol
     *  table handing out a String for `zork'.  Reshaping Symbol itself is
     *  thereby refused too, which is a class nothing should reshape.
     */
    if (ST_is_symbol(old) || ST_is_symbol(fresh))
        return 0;
    return OM_can_swap_identities(old, fresh);
}

/*  Copy one instance across, permuting the named fields.  */
static void
copy_across(const migrate_args *a, st_oop old, st_oop fresh)
{
    uint32_t    named = OM_fetch_word_length(a->map);
    uint32_t    i;

    if (!OM_pointer_bit(old)) {
        /*
         *  In bytes, counted as bytes.  A word object's length is in
         *  words, so asking OM_fetch_byte_length of one copied half of it
         *  (Bugs5 OM-4); a 16-bit word is two bytes whichever side holds
         *  them, and the body is copied byte for byte.
         */
        uint32_t    have = OM_fetch_byte_length(old)
                         * (OM_words_bit(old) ? 2u : 1u);
        uint32_t    room = OM_fetch_byte_length(fresh)
                         * (OM_words_bit(fresh) ? 2u : 1u);
        uint32_t    n = have < room ? have : room;

        for (i = 0; i < n; ++i)
            OM_store_byte(i, fresh, OM_fetch_byte(i, old));
        return;
    }

    {
        uint32_t    have = OM_fetch_word_length(old);
        uint32_t    room = OM_fetch_word_length(fresh);
        uint32_t    indexed;

        /*
         *  The named part, by the map: `map at: i' is where field i of the
         *  new shape comes from in the old one, or 0 for an instance
         *  variable this class did not have before -- which stays nil,
         *  which is what basicNew already made it.
         */
        for (i = 0; i < named && i < room; ++i) {
            st_oop      where = OM_fetch_pointer(i, a->map);
            uint32_t    from;

            if (!OM_is_int(where))
                continue;
            if (OM_int_value(where) <= 0)
                continue;
            from = (uint32_t) OM_int_value(where);
            if (from > a->old_named || from > have)
                continue;
            OM_store_pointer(i, fresh, OM_fetch_pointer(from - 1, old));
        }

        /*
         *  The indexed part, one for one.  It sits after the named fields
         *  in both, and the two shapes name a different number of them, so
         *  the offsets differ even though the elements do not.
         */
        indexed = have > a->old_named ? have - a->old_named : 0;
        if (room <= named)
            indexed = 0;
        else if (indexed > room - named)
            indexed = room - named;
        for (i = 0; i < indexed; ++i)
            OM_store_pointer(named + i, fresh,
                             OM_fetch_pointer(a->old_named + i, old));
    }
}

/*
 *  ----------  The activations a reshape leaves behind  ----------
 *
 *  Bugs07 KERNB-4.  Everything above moves the INSTANCES, and every method
 *  of the new class is compiled for the new shape.  What it does not move
 *  is an activation of an OLD method that is running at that moment --
 *  on another worker, parked in a wait, or below in some chain -- whose
 *  receiver is one of the instances.  Its bytecodes index instance
 *  variables by the old offsets, and from the moment the safepoint lets
 *  go they index the migrated body: a variable added in front of `d'
 *  made the running `d := d + 1' write into what was now `c', twenty-eight
 *  million times, silently; a variable removed made `d' read past the new
 *  body.
 *
 *  Carrying such an activation on against the right field would mean
 *  rewriting its method's bytecodes and its instruction pointer, or
 *  carrying an offset map per activation, and a field that was removed
 *  has no right field at all; neither is worth its weight for an event
 *  that happens twice a year.  Refusing the reshape while one is live
 *  would make reloading a class wait on every request that happens to be
 *  in it, and the 1983 fallback below the primitive would do the damage
 *  anyway.  So the activation is told:
 *  each context whose receiver is a migrated instance is marked, inside
 *  this safepoint, with how many leading fields kept their offsets
 *  (OM_narrow_kept_fields), and the interpreter lets the method go on
 *  naming those and abandons it with a CorruptMethod at the first that
 *  moved (receiver_limit_for in interp.c).  A method inherited from an
 *  unchanged superclass names only fields below the mark and never
 *  notices; a reshape that only appends fields marks nothing.
 *
 *  Found by walking the heap, which is what allInstances does as well and
 *  what only a safepoint makes exact: no context can be created or
 *  return while it runs.  The migrated identities are sorted once so the
 *  walk asks each context one binary search.
 */

#if defined(ST_OM_MT)
/*  How many leading named fields are where they were.  */
static uint32_t
fields_kept(const migrate_args *a)
{
    uint32_t    named = OM_fetch_word_length(a->map);
    uint32_t    k;

    for (k = 0; k < named && k < a->old_named; ++k) {
        st_oop  where = OM_fetch_pointer(k, a->map);

        if (!OM_is_int(where) || OM_int_value(where) != (st_int) k + 1)
            break;
    }
    return k;
}

static int
compare_oops(const void *x, const void *y)
{
    st_oop  a = *(const st_oop *) x;
    st_oop  b = *(const st_oop *) y;

    return a < b ? -1 : (a > b ? 1 : 0);
}

static int
is_migrated(const st_oop *sorted, const migrate_args *a, uint32_t n,
            st_oop r)
{
    uint32_t    i;

    if (sorted)
        return bsearch(&r, sorted, n, sizeof *sorted, compare_oops) != NULL;
    /*  No memory for the index: the slow way, which is still correct.  */
    for (i = 0; i < n; ++i)
        if (OM_fetch_pointer(i, a->olds) == r)
            return 1;
    return 0;
}

static void
mark_reshaped_activations(const migrate_args *a, uint32_t n)
{
    uint32_t    kept = fields_kept(a);
    st_oop     *sorted;
    st_oop      p;
    uint32_t    i;

    if (kept >= a->old_named || n == 0)
        return;                 /*  nothing an old method names has moved  */
    sorted = (st_oop *) malloc(n * sizeof *sorted);
    if (sorted) {
        for (i = 0; i < n; ++i)
            sorted[i] = OM_fetch_pointer(i, a->olds);
        qsort(sorted, n, sizeof *sorted, compare_oops);
    }
    /*
     *  MethodContexts only.  A closure's activation is one, with its
     *  receiver in it; a Blue Book BlockContext takes its receiver from
     *  its home, which is one, and the interpreter reads the mark from
     *  the home.
     */
    for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
        if (OM_fetch_class(p) != ST_CLASS_METHOD_CONTEXT
         || !OM_pointer_bit(p)
         || OM_fetch_word_length(p) <= ST_CTX_RECEIVER)
            continue;
        if (is_migrated(sorted, a, n, OM_fetch_pointer(ST_CTX_RECEIVER, p)))
            OM_narrow_kept_fields(p, kept);
    }
    free(sorted);
}

/*
 *  Bugs07 X-18.  The marking alone, for 1983's fallback loop.
 *
 *  When this primitive refuses -- an instance that cannot change identity
 *  -- ClassDescription>>updateInstancesFrom: does the work the 1983 way, a
 *  become: at a time, and the activations still running on those
 *  instances were never told: they kept only the receiver bound become:
 *  recomputes, so an old method went on indexing the new body by its old
 *  offsets, inside it and never past it, but at the wrong field.  So the
 *  fallback asks for this afterwards, with nil for the new instances: the
 *  old ones have been given their new bodies by then, and the walk marks
 *  every context whose receiver is one of them, inside a safepoint as the
 *  primitive's own marking is.  What remains is the window while the loop
 *  runs, between one instance's become: and this, which only the
 *  primitive's single safepoint closes.
 */
static uint32_t
mark_locked(void *user)
{
    migrate_args   *a = (migrate_args *) user;

    mark_reshaped_activations(a, OM_fetch_word_length(a->olds));
    return 1;
}
#endif

static uint32_t
migrate_locked(void *user)
{
    migrate_args   *a = (migrate_args *) user;
    uint32_t        n = OM_fetch_word_length(a->olds);
    uint32_t        i;

    for (i = 0; i < n; ++i) {
        if (!pair_is_movable(a, OM_fetch_pointer(i, a->olds),
                                OM_fetch_pointer(i, a->news)))
            return 0;
    }
    for (i = 0; i < n; ++i) {
        st_oop  old = OM_fetch_pointer(i, a->olds);
        st_oop  fresh = OM_fetch_pointer(i, a->news);

        copy_across(a, old, fresh);
        OM_swap_identities_at_boot(old, fresh);
    }
#if defined(ST_OM_MT)
    mark_reshaped_activations(a, n);
#endif
    return 1;
}

/*
 *  234: <old instances> <new instances> <permutation> <old named count>
 *
 *  With nil for the new instances it moves nothing and only marks the
 *  activations running on the old ones, which 1983's fallback loop has
 *  already migrated (mark_locked; the 64-bit memory only -- the Blue Book
 *  one has no mark to set and fails).
 *
 *  Answers the receiver.  Fails, having changed nothing, if the arguments
 *  are not three Arrays of equal shape and a non-negative SmallInteger, or
 *  if any one of the instances cannot change identity -- a pinned object,
 *  one whose body a worker's C frame holds a raw pointer into.  The
 *  Smalltalk fallback is 1983's own loop, which is what this image did
 *  before and is still right when there is nothing else running.
 */
int
ST_prim_migrate_instances(void)
{
    migrate_args    args;
    st_oop          named = ST_stack_value(0);

    args.olds = ST_stack_value(3);
    args.news = ST_stack_value(2);
    args.map  = ST_stack_value(1);

    if (!OM_is_int(named) || OM_int_value(named) < 0)
        return 0;
    args.old_named = (uint32_t) OM_int_value(named);

    if (!OM_is_object(args.olds) || !OM_pointer_bit(args.olds))
        return 0;
#if defined(ST_OM_MT)
    /*  nil for the new instances: mark the activations only (X-18).  */
    if (args.news == ST_NIL) {
        if (!OM_is_object(args.map) || !OM_pointer_bit(args.map))
            return 0;
        if (WORKER_count() > 0) {
            if (!WORKER_at_safepoint(mark_locked, &args))
                return 0;
        }  else  {
            (void) mark_locked(&args);
        }
        ST_pop_n(4);
        ST_refresh_receiver_limit();
        return 1;
    }
#endif
    if (!OM_is_object(args.news) || !OM_pointer_bit(args.news))
        return 0;
    if (!OM_is_object(args.map) || !OM_pointer_bit(args.map))
        return 0;
    if (OM_fetch_word_length(args.olds) != OM_fetch_word_length(args.news))
        return 0;

    if (WORKER_count() > 0) {
        if (!WORKER_at_safepoint(migrate_locked, &args))
            return 0;
    }  else if (!migrate_locked(&args))  {
        return 0;
    }

    ST_pop_n(4);                /*  the arguments; the receiver answers  */
    /*  The method that asked may itself be running on a migrated
     *  instance -- a class reshaped by one of its own methods.  */
    ST_refresh_receiver_limit();
    return 1;
}

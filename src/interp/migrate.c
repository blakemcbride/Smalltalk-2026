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
#include "prim.h"
#include "worker.h"

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
    return OM_can_swap_identities(old, fresh);
}

/*  Copy one instance across, permuting the named fields.  */
static void
copy_across(const migrate_args *a, st_oop old, st_oop fresh)
{
    uint32_t    named = OM_fetch_word_length(a->map);
    uint32_t    i;

    if (!OM_pointer_bit(old)) {
        uint32_t    have = OM_fetch_byte_length(old);
        uint32_t    room = OM_fetch_byte_length(fresh);
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
    return 1;
}

/*
 *  234: <old instances> <new instances> <permutation> <old named count>
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
    return 1;
}

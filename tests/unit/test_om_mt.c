/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Phase 4 gate: the 64-bit object memory.
 *
 *  There is no 1983 image to check this one against -- the Xerox snapshot is
 *  a 16-bit format and belongs to the other build.  So the tests here build
 *  an object graph by hand and exercise the properties the threaded system
 *  will depend on: exact SmallInteger tagging at 63 bits, the three object
 *  formats, reference counting, cycle collection, atomic identity exchange,
 *  and a snapshot round trip.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "census.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  A minimal world.  The guaranteed object pointers are fixed values, so
 *  they have to be created in order: index 1 is nil at pointer 2, index 2 is
 *  false at 4, and so on up to the last pointer the interpreter names.
 */
static void
build_fixed_objects(void)
{
    st_oop      p;
    int         i;

    /*
     *  Everything up to ST_SELECTOR_CANNOT_INTERPRET must exist so that the
     *  collector's root walk and the interpreter's constants both land on
     *  real objects.  Their classes are filled in afterwards.
     */
    for (i = 1; i <= ST_SELECTOR_CANNOT_INTERPRET / 2; ++i) {
        p = OM_instantiate_pointers(ST_NIL, 0);
        CHECK_EQ_INT(p, (st_oop) i * 2);
        /*
         *  Pin them.  The guaranteed pointers are permanent by definition --
         *  the collector already treats them as roots -- so reference
         *  counting has to agree, or the first time a class loses its last
         *  instance the class itself is reclaimed and its index is handed
         *  out again.  A real image pins them the same way.
         */
        OM_increase_ref(p);
    }
}

static void
test_small_integers(void)
{
    CHECK(OM_is_int(OM_int_oop(0)));
    CHECK(!OM_is_int(2));

    CHECK_EQ_INT(OM_int_value(OM_int_oop(0)), 0);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(-1)), -1);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(1)), 1);
    CHECK_EQ_INT(OM_int_oop(-1), UINT64_MAX);

    /*  Where the 16-bit memory stopped at 16383, this reaches 2^62 - 1.  */
    CHECK(OM_int_fits(ST_INT_MAX));
    CHECK(!OM_int_fits(ST_INT_MAX + 1));
    CHECK(OM_int_fits(ST_INT_MIN));
    CHECK_EQ_INT(OM_int_value(OM_int_oop(ST_INT_MAX)), ST_INT_MAX);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(ST_INT_MIN)), ST_INT_MIN);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(1000000000)), 1000000000);

    CHECK_EQ_INT(OM_fetch_class(OM_int_oop(42)), ST_CLASS_SMALL_INTEGER);
}

static void
test_formats(void)
{
    st_oop      pointers;
    st_oop      words;
    st_oop      bytes;
    uint32_t    i;

    pointers = OM_instantiate_pointers(ST_CLASS_ARRAY, 5);
    CHECK(OM_is_object(pointers));
    CHECK_EQ_INT(OM_fetch_word_length(pointers), 5);
    CHECK_EQ_INT(OM_pointer_bit(pointers), 1);
    for (i = 0; i < 5; ++i)
        CHECK_EQ_INT(OM_fetch_pointer(i, pointers), ST_NIL);

    words = OM_instantiate_words(ST_CLASS_DISPLAY_BITMAP, 8);
    CHECK(OM_is_object(words));
    CHECK_EQ_INT(OM_pointer_bit(words), 0);
    for (i = 0; i < 8; ++i)
        OM_store_word(i, words, (uint16_t) (i * 1111));
    for (i = 0; i < 8; ++i)
        CHECK_EQ_INT(OM_fetch_word(i, words), (uint16_t) (i * 1111));

    bytes = OM_instantiate_bytes(ST_CLASS_STRING, 5);
    CHECK(OM_is_object(bytes));
    CHECK_EQ_INT(OM_fetch_byte_length(bytes), 5);
    for (i = 0; i < 5; ++i)
        OM_store_byte(i, bytes, (uint8_t) "hello"[i]);
    {
        char    text[32];

        OM_string_of(bytes, text, sizeof text);
        CHECK_EQ_STR(text, "hello");
    }

    /*  Distinct objects must not share a body.  */
    CHECK(OM_body(pointers) != OM_body(words));
    CHECK(OM_body(words) != OM_body(bytes));

    OM_deallocate(pointers);
    OM_deallocate(words);
    OM_deallocate(bytes);
}

static void
test_reference_counting(void)
{
    st_oop  holder = OM_instantiate_pointers(ST_CLASS_ARRAY, 2);
    st_oop  held   = OM_instantiate_bytes(ST_CLASS_STRING, 4);

    OM_increase_ref(holder);
    CHECK_EQ_INT(OM_count_bits(held), 0);
    OM_store_pointer(0, holder, held);
    CHECK_EQ_INT(OM_count_bits(held), 1);
    OM_store_pointer(1, holder, held);
    CHECK_EQ_INT(OM_count_bits(held), 2);

    OM_store_pointer(0, holder, ST_NIL);
    CHECK_EQ_INT(OM_count_bits(held), 1);
    CHECK(OM_is_object(held));

    /*  Losing the last reference reclaims it.  */
    OM_store_pointer(1, holder, ST_NIL);
    CHECK(!OM_is_object(held));

    OM_decrease_ref(holder);
    CHECK(!OM_is_object(holder));
}

/*
 *  The case reference counting cannot handle, and the reason the Blue Book
 *  specifies a marking collector as well: two objects that refer only to
 *  each other.  This is exactly the shape that retains contexts -- a block
 *  points at its caller, and the caller's stack holds the block.
 */
static st_oop   root_object;

static void
provide_root(om_visit_fn visit)
{
    visit(root_object);
}

static void
test_collects_cycles(void)
{
    st_oop      a;
    st_oop      b;
    uint32_t    reclaimed;

    root_object = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_increase_ref(root_object);
    OM_set_root_provider(provide_root);

    a = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    b = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_store_pointer(0, a, b);
    OM_store_pointer(0, b, a);
    CHECK_EQ_INT(OM_count_bits(a), 1);
    CHECK_EQ_INT(OM_count_bits(b), 1);

    /*  Neither count can ever fall to zero, yet neither is reachable.  */
    reclaimed = OM_collect();
    printf("  collection reclaimed %u objects\n", reclaimed);
    CHECK(!OM_is_object(a));
    CHECK(!OM_is_object(b));
    CHECK(OM_is_object(root_object));

    /*  A cycle that IS reachable must survive.  */
    a = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    b = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_store_pointer(0, a, b);
    OM_store_pointer(0, b, a);
    OM_store_pointer(0, root_object, a);
    OM_collect();
    CHECK(OM_is_object(a));
    CHECK(OM_is_object(b));

    OM_set_root_provider(NULL);
}

/*
 *  Ephemerons.
 *
 *  The rule is one sentence and every part of it has to be tested: an
 *  ephemeron's fields -- including its key, which is field 0 -- are strong
 *  exactly when the key is reachable some other way.  So there are three
 *  cases, and the third is the one that says why the collector loops.
 */
static void
test_ephemerons(void)
{
    st_oop      eph;
    st_oop      key;
    st_oop      value;
    st_oop      second;
    st_oop      second_key;

    root_object = OM_instantiate_pointers(ST_CLASS_ARRAY, 3);
    OM_increase_ref(root_object);
    OM_set_root_provider(provide_root);

    /*
     *  One: the key is held only by the ephemeron.  Nothing survives -- not
     *  the key, and not the value that only the ephemeron names.  This is
     *  the whole difference from an ordinary object, which would keep both.
     */
    eph   = OM_instantiate_ephemeron(ST_CLASS_ARRAY, 2);
    key   = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    value = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_store_pointer(0, eph, key);
    OM_store_pointer(1, eph, value);
    OM_store_pointer(0, root_object, eph);
    OM_collect();
    CHECK(OM_is_object(eph));           /*  the ephemeron itself is rooted  */
    CHECK(!OM_is_object(key));
    CHECK(!OM_is_object(value));
    /*  And the dead key was nilled rather than left dangling.  */
    CHECK_EQ_INT((int) (OM_fetch_pointer(0, eph) == ST_NIL), 1);

    /*
     *  Two: the key is held elsewhere.  Now the whole ephemeron is strong,
     *  and the value it names survives with it.
     */
    eph   = OM_instantiate_ephemeron(ST_CLASS_ARRAY, 2);
    key   = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    value = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_store_pointer(0, eph, key);
    OM_store_pointer(1, eph, value);
    OM_store_pointer(0, root_object, eph);
    OM_store_pointer(1, root_object, key);
    OM_collect();
    CHECK(OM_is_object(key));
    CHECK(OM_is_object(value));
    CHECK_EQ_INT((int) (OM_fetch_pointer(1, eph) == value), 1);

    /*
     *  Three: a chain.  The second ephemeron's key is reachable ONLY through
     *  the first ephemeron's value, so a single pass would decide the second
     *  one was dead -- the first has not been walked yet when the second is
     *  looked at.  Both must survive, and only a walk that runs again after
     *  each ephemeron is opened can say so.  This is the case that makes the
     *  loop in ephemerons_reached load bearing rather than tidy.
     */
    OM_store_pointer(0, root_object, ST_NIL);
    OM_store_pointer(1, root_object, ST_NIL);
    OM_store_pointer(2, root_object, ST_NIL);
    OM_collect();

    second_key = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    second     = OM_instantiate_ephemeron(ST_CLASS_ARRAY, 2);
    value      = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_store_pointer(0, second, second_key);
    OM_store_pointer(1, second, value);

    key = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    eph = OM_instantiate_ephemeron(ST_CLASS_ARRAY, 2);
    OM_store_pointer(0, eph, key);
    OM_store_pointer(1, eph, second_key);   /*  reaches the other's key  */

    OM_store_pointer(0, root_object, eph);
    OM_store_pointer(1, root_object, key);      /*  the first key is held  */
    OM_store_pointer(2, root_object, second);   /*  the second is rooted   */
    OM_collect();
    CHECK(OM_is_object(second_key));
    CHECK(OM_is_object(value));
    CHECK_EQ_INT((int) (OM_fetch_pointer(0, second) == second_key), 1);

    OM_store_pointer(0, root_object, ST_NIL);
    OM_store_pointer(1, root_object, ST_NIL);
    OM_store_pointer(2, root_object, ST_NIL);
    OM_set_root_provider(NULL);
}

/*
 *  become: swaps two identities. With an object table nothing in the heap
 *  moves and no reference is rewritten, which is what makes it a candidate
 *  for a single atomic operation once threads arrive.
 */
static void
test_become(void)
{
    st_oop  holder = OM_instantiate_pointers(ST_CLASS_ARRAY, 2);
    st_oop  a      = OM_instantiate_bytes(ST_CLASS_STRING, 3);
    st_oop  b      = OM_instantiate_bytes(ST_CLASS_STRING, 3);
    char    text[16];

    OM_increase_ref(holder);
    memcpy(OM_body(a), "aaa", 3);
    memcpy(OM_body(b), "bbb", 3);
    OM_store_pointer(0, holder, a);
    OM_store_pointer(1, holder, b);

    OM_swap_identities(a, b);

    /*  The holder's fields are untouched, but they now name the other body. */
    CHECK_EQ_INT(OM_fetch_pointer(0, holder), a);
    OM_string_of(OM_fetch_pointer(0, holder), text, sizeof text);
    CHECK_EQ_STR(text, "bbb");
    OM_string_of(OM_fetch_pointer(1, holder), text, sizeof text);
    CHECK_EQ_STR(text, "aaa");

    /*  Counts belong to the identity, so they must not have moved.  */
    CHECK_EQ_INT(OM_count_bits(a), 1);
    CHECK_EQ_INT(OM_count_bits(b), 1);
}

static void
test_image_round_trip(void)
{
    const char *path = "build/test-round-trip.image";
    st_oop      array;
    st_oop      text;
    char        err[256];
    char        buf[32];

    array = OM_instantiate_pointers(ST_CLASS_ARRAY, 3);
    text  = OM_instantiate_bytes(ST_CLASS_STRING, 5);
    memcpy(OM_body(text), "world", 5);
    OM_store_pointer(0, array, text);
    OM_store_pointer(1, array, OM_int_oop(123456789));
    OM_store_pointer(2, array, ST_TRUE);

    /*  Reachable from a root, or the collector on load would discard it.  */
    root_object = array;
    OM_set_root_provider(provide_root);

    CHECK_EQ_INT(OM_image_save(path, err, sizeof err), 0);
    if (err[0])
        printf("  save said: %s\n", err);

    CHECK_EQ_INT(OM_image_load(path, err, sizeof err), 0);
    if (err[0])
        printf("  load said: %s\n", err);

    /*  Object pointers are table indices, so they survive verbatim.  */
    CHECK(OM_is_object(array));
    CHECK_EQ_INT(OM_fetch_pointer(1, array), OM_int_oop(123456789));
    CHECK_EQ_INT(OM_fetch_pointer(2, array), ST_TRUE);
    OM_string_of(OM_fetch_pointer(0, array), buf, sizeof buf);
    CHECK_EQ_STR(buf, "world");

    OM_set_root_provider(NULL);
    remove(path);
}

/*
 *  The VM's own connections to the image, across a save and a reload.
 *
 *  Two of them are held in C rather than in any instance variable -- the
 *  semaphore primitive 93 installed for input and the Form primitive 102
 *  made the display -- so a format that stored only objects dropped both,
 *  and a reloaded image came back up with nothing to signal when a key or a
 *  mouse button arrived.  The events were queued and went nowhere.
 */
static void
test_vm_state_round_trip(void)
{
    const char *path = "build/test-vm-state.image";
    st_oop      semaphore;
    st_oop      form;
    char        err[256];

    semaphore = OM_instantiate_pointers(ST_CLASS_ARRAY, 3);
    form      = OM_instantiate_pointers(ST_CLASS_ARRAY, 4);
    st_om_vm_state[ST_VM_INPUT_SEMAPHORE] = semaphore;
    st_om_vm_state[ST_VM_DISPLAY]         = form;

    /*  Held, so the collector on load keeps what the slots point at.  */
    root_object = semaphore;
    OM_set_root_provider(provide_root);
    OM_increase_ref(form);

    CHECK_EQ_INT(OM_image_save(path, err, sizeof err), 0);

    /*  Cleared, so the values below can only have come from the file.  */
    st_om_vm_state[ST_VM_INPUT_SEMAPHORE] = ST_NIL;
    st_om_vm_state[ST_VM_DISPLAY]         = ST_NIL;

    CHECK_EQ_INT(OM_image_load(path, err, sizeof err), 0);
    if (err[0])
        printf("  load said: %s\n", err);

    CHECK_EQ_INT(st_om_vm_state[ST_VM_INPUT_SEMAPHORE], semaphore);
    CHECK_EQ_INT(st_om_vm_state[ST_VM_DISPLAY], form);

    st_om_vm_state[ST_VM_INPUT_SEMAPHORE] = ST_NIL;
    st_om_vm_state[ST_VM_DISPLAY]         = ST_NIL;
    OM_set_root_provider(NULL);
    remove(path);
}

/*
 *  Running out of object table entries is a reason to collect.
 *
 *  There are two ways to run out and only one of them used to be handled: a
 *  failed calloc collected and retried, a full object table gave up.  So a
 *  long run died with every table entry in use and hundreds of millions of
 *  words of heap still free -- the desktop loop allocates a context per
 *  iteration and asks for nothing else, which is exactly the shape of
 *  program that exhausts the table first and the heap never.
 *
 *  The 16-bit memory, written from Chapter 30, had always handled both.
 *
 *  This allocates past the table's size without keeping any of it, so every
 *  entry is garbage by the time the table fills.  A collection has to happen
 *  and allocation has to carry on.
 */
static void
test_full_table_collects(void)
{
    uint32_t    before = st_om_collections;
    uint32_t    i;
    uint32_t    failures = 0;
    uint32_t    total = ST_OM_MAX_OBJECTS + (ST_OM_MAX_OBJECTS / 4);

    for (i = 0; i < total; ++i) {
        st_oop  p = OM_instantiate_pointers(ST_CLASS_ARRAY, 2);

        if (!OM_is_object(p)) {
            ++failures;
            break;
        }
    }
    CHECK_EQ_INT(failures, 0);
    /*  And it got there by collecting, not by the table being large.  */
    CHECK(st_om_collections > before);
}

int
main(void)
{
    int status;

    ST_TEST_BEGIN("64-bit object memory");

    test_small_integers();

    CHECK_EQ_INT(OM_init(), 0);
    build_fixed_objects();
    test_formats();
    test_reference_counting();
    test_collects_cycles();
    test_ephemerons();
    test_become();
    test_image_round_trip();
    test_vm_state_round_trip();
    test_full_table_collects();

    printf("  table holds %u entries, %u free\n", st_om_table_limit,
           OM_oops_left());
    status = ST_TEST_END();
    OM_shutdown();
    return status;
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: this suite validates the 64-bit object memory\n");
    return 0;
}

#endif

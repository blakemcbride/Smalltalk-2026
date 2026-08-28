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

#ifndef _WIN32
/*  For the file-size limit that stands in for a full disk, below.  */
#include <signal.h>
#include <sys/resource.h>
#endif

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

    /*
     *  Reachable from a root, so the collector keeps what the slots point
     *  at.  The form used to be "held" with OM_increase_ref alone, and that
     *  holds nothing across a save: the writer collects first, and the
     *  collector rebuilds every count from the roots, so the form was
     *  swept and written as a hole -- and the slot, restored verbatim,
     *  named a free entry.  The old loader let that through and this test
     *  passed on it; the loader that checks every stored pointer (Bugs3
     *  B58) refuses it, which is the right answer for a file whose display
     *  is a hole.  In the VM the slots are roots themselves; here the
     *  semaphore is the root and carries the form.
     */
    root_object = semaphore;
    OM_set_root_provider(provide_root);
    OM_store_pointer(0, semaphore, form);

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

/*
 *  Bugs3 B8: dropping a long chain of objects must not recurse.
 *
 *  Releasing an object released its fields, and a field that reached zero
 *  was released from inside that release -- one C frame pair per link, so
 *  a linked list of 400,000 Arrays dropped in a method took every
 *  no-worker mode down with SIGSEGV on the C stack.  The marker was
 *  already iterative, so the chain survived a collection and died on
 *  being let go.  Three million links here, ten times what killed it,
 *  built with the head held from a root so that a collection during the
 *  build keeps it, then dropped in one store.
 */
static void
test_long_chain_frees_iteratively(void)
{
    const uint32_t  links = 3000000u;
    st_oop          holder;
    st_oop          head;
    st_oop          tail;
    uint32_t        i;
    uint32_t        failures = 0;

    holder = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_increase_ref(holder);
    root_object = holder;
    OM_set_root_provider(provide_root);

    tail = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    OM_store_pointer(0, holder, tail);
    head = tail;
    for (i = 1; i < links; ++i) {
        st_oop  link = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);

        if (!OM_is_object(link)) {
            ++failures;
            break;
        }
        OM_store_pointer(0, link, head);
        OM_store_pointer(0, holder, link);
        head = link;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK(OM_is_object(head));
    CHECK(OM_is_object(tail));
    CHECK_EQ_INT(OM_count_bits(head), 1);

    /*  The drop.  Recursion would go three million frames deep here.  */
    OM_store_pointer(0, holder, ST_NIL);
    CHECK(!OM_is_object(head));
    CHECK(!OM_is_object(tail));

    OM_decrease_ref(holder);
    root_object = ST_NIL;
    OM_set_root_provider(NULL);
}

/*
 *  Read a whole file into memory, or answer NULL.
 */
static unsigned char *
slurp(const char *path, size_t *len)
{
    FILE           *f = fopen(path, "rb");
    unsigned char  *data;
    long            size;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = (unsigned char *) malloc((size_t) size + 1);
    if (!data || fread(data, 1, (size_t) size, f) != (size_t) size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t) size;
    return data;
}

static int
spit(const char *path, const unsigned char *data, size_t len)
{
    FILE   *f = fopen(path, "wb");
    int     ok;

    if (!f)
        return -1;
    ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int
file_exists(const char *path)
{
    FILE   *f = fopen(path, "rb");

    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/*
 *  Bugs3 B10: a snapshot that fails leaves the previous image untouched.
 *
 *  The writer opened the target with "wb", which truncates it before the
 *  first byte of the new image is written, so a write stopped part way --
 *  here by a file-size limit, in the audit by `ulimit -f 1000', in life by
 *  a full disk or a SIGINT -- left the only copy cut off and refused at
 *  the next start.  Now it writes `<path>.tmp' and renames over the target
 *  once the whole file is on disk.  So: save a good image, make the next
 *  save fail, and the file must be byte for byte what it was, with no
 *  temporary left beside it, and must still load.
 *
 *  The image is padded with a large String so that it is comfortably
 *  bigger than the limit, and the limit is comfortably bigger than the
 *  header, so the failure is a short write in the middle rather than a
 *  refused open.  RLIMIT_FSIZE is POSIX; on Windows only the unwritable
 *  directory half of the test runs.
 */
static void
test_snapshot_never_destroys_the_old_image(void)
{
    const char     *path = "build/test-atomic-save.image";
    const char     *tmp  = "build/test-atomic-save.image.tmp";
    st_oop          array;
    st_oop          text;
    unsigned char  *good;
    size_t          good_len = 0;
    char            err[256];
    uint32_t        i;

    array = OM_instantiate_pointers(ST_CLASS_ARRAY, 2);
    text  = OM_instantiate_bytes(ST_CLASS_STRING, 300000);
    for (i = 0; i < 300000; ++i)
        OM_store_byte(i, text, (uint8_t) ('a' + i % 26));
    OM_store_pointer(0, array, text);
    OM_store_pointer(1, array, OM_int_oop(4242));
    root_object = array;
    OM_set_root_provider(provide_root);

    CHECK_EQ_INT(OM_image_save(path, err, sizeof err), 0);
    good = slurp(path, &good_len);
    CHECK(good != NULL);
    CHECK(good_len > 300000);
    CHECK(!file_exists(tmp));

    /*  A directory that is not there: refused, and nothing appears.  */
    CHECK_EQ_INT(OM_image_save("build/no-such-dir/x.image", err, sizeof err),
                 -1);
    CHECK(err[0] != '\0');
    printf("  save into a missing directory said: %s\n", err);
    CHECK(!file_exists("build/no-such-dir/x.image"));
    CHECK(!file_exists("build/no-such-dir/x.image.tmp"));

#ifndef _WIN32
    {
        struct rlimit   was;
        struct rlimit   small;
        int             saved;
        unsigned char  *after;
        size_t          after_len = 0;

        /*
         *  Past the limit a write fails with EFBIG -- but only if SIGXFSZ
         *  is ignored, since its default action is to kill the process,
         *  which is exactly the interruption the audit stood the limit in
         *  for and not what a test wants.
         */
        CHECK_EQ_INT(getrlimit(RLIMIT_FSIZE, &was), 0);
        signal(SIGXFSZ, SIG_IGN);
        small = was;
        small.rlim_cur = 65536;
        CHECK_EQ_INT(setrlimit(RLIMIT_FSIZE, &small), 0);
        saved = OM_image_save(path, err, sizeof err);
        CHECK_EQ_INT(setrlimit(RLIMIT_FSIZE, &was), 0);
        signal(SIGXFSZ, SIG_DFL);

        CHECK_EQ_INT(saved, -1);
        CHECK(err[0] != '\0');
        printf("  save under a 64 KB file-size limit said: %s\n", err);
        CHECK(!file_exists(tmp));
        after = slurp(path, &after_len);
        CHECK(after != NULL);
        CHECK_EQ_INT(after_len, good_len);
        CHECK(after && good && after_len == good_len
              && memcmp(after, good, good_len) == 0);
        free(after);
    }
#endif

    /*  And the untouched image still loads, with the String intact.  */
    CHECK_EQ_INT(OM_image_load(path, err, sizeof err), 0);
    if (err[0])
        printf("  load said: %s\n", err);
    CHECK(OM_is_object(array));
    CHECK_EQ_INT(OM_fetch_pointer(1, array), OM_int_oop(4242));
    text = OM_fetch_pointer(0, array);
    CHECK(OM_is_object(text));
    CHECK_EQ_INT(OM_fetch_byte_length(text), 300000);
    CHECK_EQ_INT(OM_fetch_byte(299999, text), (uint8_t) ('a' + 299999 % 26));

    free(good);
    OM_set_root_provider(NULL);
    remove(path);
}

/*
 *  Where object `want' begins in an image file, walking the format the
 *  writer uses: the header, then per index a present byte and, when set,
 *  class (8), size (4), flags (4), hash (4) and the body.  Answers the
 *  offset of the class word, and puts the offset of the body in *body.
 *  -1 if the index is not present.
 */
static long
find_object(const unsigned char *img, size_t len, uint32_t want, long *body)
{
    size_t      off = 8 + 4;
    uint32_t    limit;
    uint32_t    index;

    if (len < 16)
        return -1;
    limit = (uint32_t) img[off] | ((uint32_t) img[off + 1] << 8)
          | ((uint32_t) img[off + 2] << 16) | ((uint32_t) img[off + 3] << 24);
    off += 4 + ST_VM_STATE_SLOTS * 8;
    for (index = 1; index < limit && off < len; ++index) {
        unsigned char   present = img[off++];
        uint32_t        size;
        uint32_t        flags;
        size_t          bytes;

        if (!present)
            continue;
        if (off + 20 > len)
            return -1;
        size  = (uint32_t) img[off + 8] | ((uint32_t) img[off + 9] << 8)
              | ((uint32_t) img[off + 10] << 16)
              | ((uint32_t) img[off + 11] << 24);
        flags = (uint32_t) img[off + 12] | ((uint32_t) img[off + 13] << 8)
              | ((uint32_t) img[off + 14] << 16)
              | ((uint32_t) img[off + 15] << 24);
        bytes = (flags & ST_FMT_POINTERS) ? (size_t) size * 8
              : (flags & ST_FMT_WORDS) ? (size_t) size * 2 : size;
        if (index == want) {
            *body = (long) (off + 20);
            return (long) off;
        }
        off += 20 + bytes;
    }
    return -1;
}

static void
put_u64(unsigned char *at, uint64_t v)
{
    int     i;

    for (i = 0; i < 8; ++i)
        at[i] = (unsigned char) (v >> (8 * i));
}

/*
 *  Load a corrupted copy of a good image and say what happened.  Refused
 *  is right; loading is allowed when the damage lands somewhere harmless;
 *  a signal is the failure, and it fails the whole run rather than a
 *  check, which is the point.
 */
static int
load_copy(const char *path, const unsigned char *img, size_t len, char *err,
          size_t errlen)
{
    if (spit(path, img, len) != 0)
        return -2;
    return OM_image_load(path, err, errlen);
}

/*
 *  Bugs3 B58: a corrupt image is refused, never followed.
 *
 *  The loader took the file's word for every object pointer, and the first
 *  thing to look at one was the collector's mark: a byte flipped in the
 *  high half of a pointer field passed OM_is_object -- which narrowed the
 *  oop to 32 bits before comparing it with the table limit while OM_head
 *  indexed with all 64 -- and segfaulted in mark_visit.  A field or a class
 *  aimed at a hole in the table, or past its end, loaded and ran.
 *
 *  So, on a saved image with a known hole in it: the guard at full width;
 *  every shape of bad pointer the audit found, each refused with a message
 *  that names the object; and a few hundred random single-byte flips,
 *  which must each be refused or run and never signal.
 */
static void
test_corrupt_image_is_refused(void)
{
    const char     *path = "build/test-corrupt-source.image";
    const char     *bad  = "build/test-corrupt-copy.image";
    st_oop          junk;
    st_oop          array;
    st_oop          text;
    uint32_t        hole;
    uint32_t        hash_before;
    unsigned char  *good;
    unsigned char  *copy;
    size_t          len = 0;
    long            at;
    long            body = 0;
    char            err[256];
    unsigned        refused = 0;
    unsigned        loaded  = 0;
    unsigned        k;
    uint32_t        seed = 20260827u;

    /*
     *  An entry that will be a hole: allocated before the array, so its
     *  index is below the array's and survives the sweep's trimming of the
     *  table's top, and released before the save so the file records it as
     *  absent.
     */
    junk  = OM_instantiate_pointers(ST_CLASS_ARRAY, 1);
    array = OM_instantiate_pointers(ST_CLASS_ARRAY, 3);
    text  = OM_instantiate_bytes(ST_CLASS_STRING, 5);
    memcpy(OM_body(text), "hello", 5);
    OM_store_pointer(0, array, text);
    OM_store_pointer(1, array, OM_int_oop(77));
    OM_store_pointer(2, array, ST_TRUE);
    hole = (uint32_t) (junk >> 1);
    OM_deallocate(junk);
    CHECK(!OM_is_object(junk));
    root_object = array;
    OM_set_root_provider(provide_root);
    hash_before = OM_identity_hash(array);

    /*  The guard, at full width: the audit's exact oop.  */
    CHECK(OM_is_object(array));
    CHECK(!OM_is_object(array | ((st_oop) 1 << 40)));
    CHECK(!OM_is_object(array | ((st_oop) 1 << 33)));
    CHECK(!OM_is_object((st_oop) 1 << 63));

    CHECK_EQ_INT(OM_image_save(path, err, sizeof err), 0);
    good = slurp(path, &len);
    CHECK(good != NULL);
    if (!good) {
        OM_set_root_provider(NULL);
        return;
    }
    copy = (unsigned char *) malloc(len);
    CHECK(copy != NULL);
    if (!copy) {
        free(good);
        OM_set_root_provider(NULL);
        return;
    }
    at = find_object(good, len, (uint32_t) (array >> 1), &body);
    CHECK(at >= 0);
    CHECK_EQ_INT(find_object(good, len, hole, &body), -1);
    body = 0;
    at = find_object(good, len, (uint32_t) (array >> 1), &body);

    /*  A field with its upper bits set: the segfault in mark_visit.  */
    memcpy(copy, good, len);
    put_u64(copy + body, text | ((st_oop) 1 << 40));
    CHECK_EQ_INT(load_copy(bad, copy, len, err, sizeof err), -1);
    CHECK(strstr(err, "object") != NULL);
    printf("  upper bits set: %s\n", err);

    /*  A field aimed at the hole.  */
    memcpy(copy, good, len);
    put_u64(copy + body, (st_oop) hole << 1);
    CHECK_EQ_INT(load_copy(bad, copy, len, err, sizeof err), -1);
    CHECK(strstr(err, "object") != NULL);
    printf("  field at a hole: %s\n", err);

    /*  A field aimed past the table.  */
    memcpy(copy, good, len);
    put_u64(copy + body, (st_oop) (st_om_table_limit + 5) << 1);
    CHECK_EQ_INT(load_copy(bad, copy, len, err, sizeof err), -1);
    CHECK(strstr(err, "object") != NULL);

    /*  A field of zero, which is no oop at all.  */
    memcpy(copy, good, len);
    put_u64(copy + body, 0);
    CHECK_EQ_INT(load_copy(bad, copy, len, err, sizeof err), -1);
    CHECK(strstr(err, "object") != NULL);

    /*  A class pointer aimed at the hole, and one with its upper bits set. */
    memcpy(copy, good, len);
    put_u64(copy + at, (st_oop) hole << 1);
    CHECK_EQ_INT(load_copy(bad, copy, len, err, sizeof err), -1);
    CHECK(strstr(err, "class") != NULL);
    printf("  class at a hole: %s\n", err);
    memcpy(copy, good, len);
    put_u64(copy + at, ST_CLASS_ARRAY | ((st_oop) 1 << 45));
    CHECK_EQ_INT(load_copy(bad, copy, len, err, sizeof err), -1);
    CHECK(strstr(err, "class") != NULL);

    /*  A VM-state slot with its upper bits set.  */
    memcpy(copy, good, len);
    put_u64(copy + 16 + 8 * ST_VM_DISPLAY, array | ((st_oop) 1 << 40));
    CHECK_EQ_INT(load_copy(bad, copy, len, err, sizeof err), -1);
    CHECK(strstr(err, "slot") != NULL);

    /*
     *  nil made absent: the first record is index 1, whose present byte is
     *  right after the header, and a nil has no fields to cut.
     */
    memcpy(copy, good, len);
    {
        size_t  first = 16 + 8 * ST_VM_STATE_SLOTS;

        copy[first] = 0;
        memmove(copy + first + 1, copy + first + 21, len - first - 21);
        CHECK_EQ_INT(load_copy(bad, copy, len - 20, err, sizeof err), -1);
        CHECK(strstr(err, "guaranteed") != NULL);
        printf("  nil absent: %s\n", err);
    }

    /*  And the random flips: every outcome but a signal is allowed.  */
    for (k = 0; k < 300; ++k) {
        size_t  where;
        int     r;

        seed = seed * 1103515245u + 12345u;
        where = (size_t) (seed >> 8) % len;
        seed = seed * 1103515245u + 12345u;
        memcpy(copy, good, len);
        copy[where] ^= (unsigned char) (1u << ((seed >> 8) % 8));
        r = load_copy(bad, copy, len, err, sizeof err);
        if (r == 0)
            ++loaded;
        else
            ++refused;
    }
    printf("  300 single-byte flips: %u refused, %u loaded, no signal\n",
           refused, loaded);
    CHECK_EQ_INT(refused + loaded, 300);

    /*  The good image again, so what follows runs on a sane memory.  */
    CHECK_EQ_INT(OM_image_load(path, err, sizeof err), 0);
    if (err[0])
        printf("  load said: %s\n", err);
    CHECK(OM_is_object(array));
    CHECK_EQ_INT(OM_fetch_pointer(1, array), OM_int_oop(77));
    /*
     *  Bugs3 B33: the identity hash is thirty bits wide and survives the
     *  round trip -- the audit checked that a save keeps it, and widening
     *  it must not have changed that.
     */
    CHECK_EQ_INT(OM_identity_hash(array), hash_before);
    CHECK(OM_identity_hash(array) < (1u << 30));

    free(copy);
    free(good);
    OM_set_root_provider(NULL);
    remove(path);
    remove(bad);
}

/*
 *  Bugs3 B33: the identity hash belongs to the identity, so a two-way
 *  become: -- which exchanges the bodies and leaves every reference where
 *  it was -- must leave each oop's hash where it was.  Fourteen bits hid
 *  this behind ordinary collisions; at thirty a Set that grew would have
 *  changed its own hash.
 */
static void
test_become_keeps_identity_hashes(void)
{
    st_oop      a = OM_instantiate_bytes(ST_CLASS_STRING, 1);
    st_oop      b = OM_instantiate_bytes(ST_CLASS_STRING, 1);
    uint32_t    ha = OM_identity_hash(a);
    uint32_t    hb = OM_identity_hash(b);

    CHECK(ha != hb);
    CHECK_EQ_INT(OM_swap_identities(a, b), 1);
    CHECK_EQ_INT(OM_identity_hash(a), ha);
    CHECK_EQ_INT(OM_identity_hash(b), hb);
    OM_deallocate(a);
    OM_deallocate(b);
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
    test_become_keeps_identity_hashes();
    test_image_round_trip();
    test_vm_state_round_trip();
    test_long_chain_frees_iteratively();
    test_snapshot_never_destroys_the_old_image();
    test_corrupt_image_is_refused();
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

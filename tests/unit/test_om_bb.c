/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Phase 1 gate.
 *
 *  Loads the 1983 Xerox virtual image and checks the object memory against
 *  the reference dumps Xerox shipped on the same tape:
 *
 *      ref-count-distribution   a histogram of every object's reference count
 *      class.oops               every class and metaclass, with its pointer
 *
 *  Matching these exactly is a far sharper test than any hand-written one:
 *  a single bit-numbering slip, a byte-order mistake, or an off-by-one in
 *  the object-table addressing shows up immediately as thousands of
 *  mismatches.
 *
 *  The oracle files carry no license grant and are gitignored; when they are
 *  absent these tests skip rather than fail, so the suite still runs on a
 *  clean checkout.
 */

#include "st_test.h"

/*
 *  These check the Blue Book object memory against the 1983 image, so they
 *  only apply to that build.  Under OM=mt they compile to a stub.
 */
#ifdef ST_OM_BB

#include "om.h"
#include "census.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORACLE_DIR      "oracle"
#define IMAGE_PATH      ORACLE_DIR "/VirtualImage"
#define REFCOUNT_PATH   ORACLE_DIR "/ref-count-distribution"
#define CLASSOOPS_PATH  ORACLE_DIR "/class.oops"
#define METHODOOPS_PATH ORACLE_DIR "/method.oops"

static int  oracle_present;

static int
file_exists(const char *path)
{
    FILE   *f = fopen(path, "rb");

    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/*  ----------  SmallInteger encoding  ----------  */

/*
 *  These need no image, and they are where an unsigned shift would quietly
 *  turn -1 into 32767.
 */
static void
test_small_integers(void)
{
    CHECK(OM_is_int(OM_int_oop(0)));
    CHECK(!OM_is_int(2));

    CHECK_EQ_INT(OM_int_value(OM_int_oop(0)), 0);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(1)), 1);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(-1)), -1);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(16383)), 16383);
    CHECK_EQ_INT(OM_int_value(OM_int_oop(-16384)), -16384);

    /*  -1 must encode as all ones, per the Blue Book's 15-bit two's
     *  complement in the high bits.  */
    CHECK_EQ_INT(OM_int_oop(-1), 0xFFFF);
    CHECK_EQ_INT(OM_int_oop(0), 1);
    CHECK_EQ_INT(OM_int_oop(1), 3);

    CHECK(OM_int_fits(16383));
    CHECK(!OM_int_fits(16384));
    CHECK(OM_int_fits(-16384));
    CHECK(!OM_int_fits(-16385));

    CHECK_EQ_INT(OM_fetch_class(OM_int_oop(42)), ST_CLASS_SMALL_INTEGER);
}

/*  ----------  Image loading  ----------  */

static void
test_image_loads(void)
{
    char    err[256];

    CHECK_EQ_INT(OM_init(), 0);
    CHECK_EQ_INT(OM_image_load(IMAGE_PATH, err, sizeof err), 0);
    if (err[0])
        printf("  loader said: %s\n", err);

    /*  The header values we decoded by hand from the file.  */
    CHECK_EQ_INT(st_om_image_object_words, 258880);
    CHECK_EQ_INT(st_om_image_ot_words, 38736);

    /*  Guaranteed pointers must be live and must be what they claim.  */
    CHECK(OM_is_object(ST_NIL));
    CHECK(OM_is_object(ST_TRUE));
    CHECK(OM_is_object(ST_FALSE));
    CHECK(OM_is_object(ST_SMALLTALK));
    CHECK(OM_fetch_class(ST_TRUE) != OM_fetch_class(ST_FALSE));
}

/*
 *  The guaranteed class pointers must name the classes the Blue Book says
 *  they do.  This is an independent check on the instance-variable layout
 *  and on Symbol decoding.
 */
static void
test_guaranteed_class_names(void)
{
    static const struct {
        st_oop      oop;
        const char *name;
    } expect[] = {
        { ST_CLASS_SMALL_INTEGER,          "SmallInteger"          },
        { ST_CLASS_STRING,                 "String"                },
        { ST_CLASS_ARRAY,                  "Array"                 },
        { ST_CLASS_FLOAT,                  "Float"                 },
        { ST_CLASS_METHOD_CONTEXT,         "MethodContext"         },
        { ST_CLASS_BLOCK_CONTEXT,          "BlockContext"          },
        { ST_CLASS_POINT,                  "Point"                 },
        { ST_CLASS_LARGE_POSITIVE_INTEGER, "LargePositiveInteger"  },
        { ST_CLASS_DISPLAY_BITMAP,         "DisplayBitmap"         },
        { ST_CLASS_MESSAGE,                "Message"               },
        { ST_CLASS_COMPILED_METHOD,        "CompiledMethod"        },
        { ST_CLASS_SEMAPHORE,              "Semaphore"             },
        { ST_CLASS_CHARACTER,              "Character"             },
    };
    size_t  i;
    char    buf[128];

    for (i = 0; i < sizeof expect / sizeof expect[0]; ++i) {
        CHECK(OM_class_name_of(expect[i].oop, buf, sizeof buf));
        CHECK_EQ_STR(buf, expect[i].name);
    }

    /*  Metaclass is discovered, not hardcoded; confirm the discovery.  */
    CHECK(OM_class_name_of(OM_metaclass(), buf, sizeof buf));
    CHECK_EQ_STR(buf, "Metaclass");
}

/*  ----------  Gate 1: the reference-count histogram  ----------  */

static void
test_refcount_histogram(void)
{
    om_census   c;
    FILE       *f;
    char        line[256];
    uint32_t    expected_objects = 0;
    uint64_t    expected_total   = 0;
    int         mismatches       = 0;
    int         lines            = 0;

    OM_census(&c);

    f = fopen(REFCOUNT_PATH, "r");
    if (!f) {
        printf("  SKIP: %s missing\n", REFCOUNT_PATH);
        return;
    }
    /*
     *  Each line is "<how many objects> <reference count>".  Compare every
     *  bucket rather than just the totals, so a shifted count field cannot
     *  cancel itself out.
     */
    while (fgets(line, sizeof line, f)) {
        unsigned long   objects;
        unsigned long   refcount;

        if (sscanf(line, "%lu %lu", &objects, &refcount) != 2)
            continue;
        ++lines;
        expected_objects += (uint32_t) objects;
        expected_total   += (uint64_t) objects * refcount;
        if (refcount >= ST_HUGE_SIZE)
            continue;
        if (c.refcount_histogram[refcount] != objects) {
            if (mismatches < 10)
                printf("  refcount %lu: got %u objects, Xerox says %lu\n",
                       refcount, c.refcount_histogram[refcount], objects);
            ++mismatches;
        }
    }
    fclose(f);

    printf("  %u live objects, %u free table entries, %llu words\n",
           c.objects, c.free_entries, (unsigned long long) c.total_words);
    printf("  %u pointer objects, %u non-pointer, %u odd-length\n",
           c.pointer_objects, c.nonpointer_objects, c.odd_objects);
    printf("  histogram: %d buckets in the Xerox dump\n", lines);

    CHECK_EQ_INT(mismatches, 0);
    CHECK_EQ_INT(c.objects, expected_objects);
    CHECK_EQ_INT(c.total_refcounts, expected_total);
}

/*
 *  ----------  A documented difference between the dumps and the image  -----
 *
 *  class.oops and method.oops were produced by an ImageStatistics package
 *  that was filed into a working image and is not present in the shipped
 *  VirtualImage.  The evidence is entirely self-consistent: the dumps carry
 *  two extra classes (ImageStatistics and its metaclass, at the two highest
 *  object pointers in the file), the four methods that make up the tool, the
 *  UndefinedObject>>DoIt that ran it, and a recompiled
 *  Integer>>printStringRadix: at a fresh pointer -- exactly what filing in a
 *  package that formats numbers in hex would produce.
 *
 *  ref-count-distribution, by contrast, matches our image object for object,
 *  so it was dumped from this image.
 *
 *  Everything else must match exactly.  These lists are deliberately
 *  specific: a wildcard would let a real regression hide behind them.
 */

static const char *const known_absent_classes[] = {
    "ImageStatistics",
    "ImageStatistics class",
};

static const char *const known_absent_methods[] = {
    "<ImageStatistics>printMethodsWithHex:",
    "<ImageStatistics>printClassesWithHex:",
    "<Object>implementorOop",
    "<Integer>implementorOopAsObject",
    "<UndefinedObject>DoIt",
    "<Integer>printStringRadix:",        /*  recompiled; ours sits elsewhere  */
};

static int
in_list(const char *s, const char *const *list, size_t n)
{
    size_t  i;

    for (i = 0; i < n; ++i) {
        if (strcmp(s, list[i]) == 0)
            return 1;
    }
    return 0;
}

#define COUNT_OF(a)     (sizeof (a) / sizeof (a)[0])

/*  ----------  Gate 2: every class, by pointer and by name  ----------  */

static void
test_class_oops(void)
{
    FILE       *f;
    char        line[512];
    int         mismatches = 0;
    int         checked    = 0;

    f = fopen(CLASSOOPS_PATH, "r");
    if (!f) {
        printf("  SKIP: %s missing\n", CLASSOOPS_PATH);
        return;
    }
    /*
     *  Lines look like:   8r14<tab>16rC<tab>SmallInteger
     *  Octal, hex, then the printable class name.  We parse the hex form.
     */
    while (fgets(line, sizeof line, f)) {
        unsigned    oop;
        char        name[256];
        char       *tab;
        char       *hex;
        char       *nl;

        hex = strchr(line, '\t');
        if (!hex)
            continue;                       /*  the copyright banner  */
        ++hex;
        tab = strchr(hex, '\t');
        if (!tab)
            continue;
        *tab = '\0';
        if (sscanf(hex, "16r%x", &oop) != 1)
            continue;
        strncpy(name, tab + 1, sizeof name - 1);
        name[sizeof name - 1] = '\0';
        nl = strpbrk(name, "\r\n");
        if (nl)
            *nl = '\0';

        {
            char    got[256];

            if (in_list(name, known_absent_classes,
                        COUNT_OF(known_absent_classes))) {
                /*  Must be genuinely absent, not merely different.  */
                CHECK(!OM_is_object((st_oop) oop));
                continue;
            }
            ++checked;
            if (!OM_class_name_of((st_oop) oop, got, sizeof got)
             || strcmp(got, name) != 0) {
                if (mismatches < 10)
                    printf("  oop 16r%X: got \"%s\", Xerox says \"%s\"\n",
                           oop, got, name);
                ++mismatches;
            }
        }
    }
    fclose(f);

    printf("  checked %d classes from the Xerox dump\n", checked);
    CHECK_EQ_INT(checked, 446);
    CHECK_EQ_INT(mismatches, 0);
}

/*  ----------  Gate 3: every method, by pointer, class and selector  --------
 *
 *  This walks method dictionaries, which is the same structure the
 *  interpreter's method lookup will traverse in Phase 2.  Validating it
 *  against Xerox's own dump now means lookup is built on a layout that has
 *  already been proven against 4,494 real methods.
 */

static char   **method_index;       /*  oop -> "<Class>selector"  */

static void
index_method(st_oop cls, st_oop selector, st_oop method, void *user)
{
    char    text[600];
    char    cname[256];
    char    sel[256];

    (void) user;
    OM_class_name_of(cls, cname, sizeof cname);
    OM_string_of(selector, sel, sizeof sel);
    snprintf(text, sizeof text, "<%s>%s", cname, sel);
    /*  st_oop is 16 bits, so every value indexes the table.  */
    if (!method_index[method])
        method_index[method] = strdup(text);
}

static void
test_method_oops(void)
{
    FILE       *f;
    char        line[512];
    uint32_t    walked;
    int         mismatches = 0;
    int         checked    = 0;
    int         absent     = 0;

    method_index = (char **) calloc(ST_OT_WORDS, sizeof *method_index);
    CHECK(method_index != NULL);
    if (!method_index)
        return;
    walked = OM_walk_methods(index_method, NULL);
    printf("  walked %u methods in the image\n", walked);
    CHECK_EQ_INT(walked, 4494);

    f = fopen(METHODOOPS_PATH, "r");
    if (!f) {
        printf("  SKIP: %s missing\n", METHODOOPS_PATH);
        free(method_index);
        method_index = NULL;
        return;
    }
    while (fgets(line, sizeof line, f)) {
        unsigned    oop;
        char       *hex;
        char       *tab;
        char       *nl;
        const char *got;

        hex = strchr(line, '\t');
        if (!hex)
            continue;                       /*  the copyright banner  */
        ++hex;
        tab = strchr(hex, '\t');
        if (!tab)
            continue;
        *tab = '\0';
        if (sscanf(hex, "16r%x", &oop) != 1)
            continue;
        nl = strpbrk(tab + 1, "\r\n");
        if (nl)
            *nl = '\0';

        if (in_list(tab + 1, known_absent_methods,
                    COUNT_OF(known_absent_methods))) {
            ++absent;
            continue;
        }
        ++checked;
        got = (oop < ST_OT_WORDS) ? method_index[oop] : NULL;
        if (!got || strcmp(got, tab + 1) != 0) {
            if (mismatches < 10)
                printf("  method 16r%X: got \"%s\", Xerox says \"%s\"\n",
                       oop, got ? got : "(none)", tab + 1);
            ++mismatches;
        }
    }
    fclose(f);

    printf("  checked %d methods from the Xerox dump (%d known absent)\n",
           checked, absent);
    CHECK_EQ_INT(checked, 4493);
    CHECK_EQ_INT(absent, (int) COUNT_OF(known_absent_methods));
    CHECK_EQ_INT(mismatches, 0);

    {
        uint32_t    i;

        for (i = 0; i < ST_OT_WORDS; ++i)
            free(method_index[i]);
        free(method_index);
        method_index = NULL;
    }
}

/*
 *  The converse direction: every class we can find by walking the object
 *  table must appear in the Xerox dump.  Without this, a census that found
 *  only half the classes would still pass the test above.
 */
static void
test_no_extra_classes(void)
{
    FILE       *f;
    char        line[512];
    st_oop      p;
    int         found  = 0;
    int         extra  = 0;
    static unsigned char in_dump[ST_OT_WORDS];

    f = fopen(CLASSOOPS_PATH, "r");
    if (!f) {
        printf("  SKIP: %s missing\n", CLASSOOPS_PATH);
        return;
    }
    memset(in_dump, 0, sizeof in_dump);
    while (fgets(line, sizeof line, f)) {
        unsigned    oop;
        char       *hex = strchr(line, '\t');

        if (!hex)
            continue;
        if (sscanf(hex + 1, "16r%x", &oop) == 1 && oop < ST_OT_WORDS)
            in_dump[oop] = 1;
    }
    fclose(f);

    for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
        char    name[256];

        if (!OM_class_name_of(p, name, sizeof name))
            continue;
        ++found;
        if (!in_dump[p]) {
            if (extra < 10)
                printf("  extra class 16r%X \"%s\" not in the Xerox dump\n",
                       (unsigned) p, name);
            ++extra;
        }
    }
    printf("  walked the object table and found %d classes\n", found);
    CHECK_EQ_INT(extra, 0);
}

/*  ----------  Allocation  ----------  */

static void
test_allocation(void)
{
    st_oop      a;
    st_oop      b;
    st_oop      s;
    uint32_t    before = OM_core_left();
    uint32_t    i;
    char        text[64];

    printf("  %u words free after load\n", before);
    CHECK(before > 0);

    /*  A pointer object arrives filled with nil and owning a reference.  */
    a = OM_instantiate_pointers(ST_CLASS_ARRAY, 5);
    CHECK(OM_is_object(a));
    CHECK_EQ_INT(OM_fetch_class(a), ST_CLASS_ARRAY);
    CHECK_EQ_INT(OM_fetch_word_length(a), 5);
    CHECK_EQ_INT(OM_pointer_bit(a), 1);
    CHECK_EQ_INT(OM_count_bits(a), 0);
    for (i = 0; i < 5; ++i)
        CHECK_EQ_INT(OM_fetch_pointer(i, a), ST_NIL);

    /*  Byte objects carry the odd bit when their length is odd.  */
    s = OM_instantiate_bytes(ST_CLASS_STRING, 5);
    CHECK(OM_is_object(s));
    CHECK_EQ_INT(OM_fetch_byte_length(s), 5);
    CHECK_EQ_INT(OM_odd_bit(s), 1);
    CHECK_EQ_INT(OM_pointer_bit(s), 0);
    for (i = 0; i < 5; ++i)
        OM_store_byte(i, s, (uint8_t) ("hello"[i]));
    OM_string_of(s, text, sizeof text);
    CHECK_EQ_STR(text, "hello");

    /*  An even-length byte object must not set the odd bit.  */
    b = OM_instantiate_bytes(ST_CLASS_STRING, 4);
    CHECK_EQ_INT(OM_odd_bit(b), 0);
    CHECK_EQ_INT(OM_fetch_byte_length(b), 4);

    /*  Distinct allocations must not overlap.  */
    CHECK(a != s && s != b && a != b);
    CHECK(OM_chunk_addr(a) != OM_chunk_addr(s));

    /*  Storing through OM_store_pointer maintains the counts.  */
    OM_store_pointer(0, a, s);
    CHECK_EQ_INT(OM_fetch_pointer(0, a), s);
    CHECK_EQ_INT(OM_count_bits(s), 1);
    OM_store_pointer(0, a, ST_NIL);
    /*  s lost its last reference, so it was reclaimed.  */
    CHECK(!OM_is_object(s));

    OM_deallocate(a);
    OM_deallocate(b);
    CHECK(!OM_is_object(a));
    CHECK(!OM_is_object(b));

    /*  All of it came back.  */
    CHECK_EQ_INT(OM_core_left(), before);
}

/*
 *  Allocate and release many objects in a churn, then confirm the heap is
 *  exactly as large as it started.  This is what catches a free-list bug
 *  that leaks a few words per cycle.
 */
static void
test_allocation_churn(void)
{
    uint32_t    before = OM_core_left();
    st_oop      held[64];
    int         round;
    int         i;
    int         failed = 0;

    for (round = 0; round < 100; ++round) {
        for (i = 0; i < 64; ++i) {
            held[i] = OM_instantiate_pointers(ST_CLASS_ARRAY,
                                              (uint32_t) (i % 40) + 1);
            /*  Counted once at the end; 6400 CHECKs would drown the log.  */
            if (!OM_is_object(held[i]))
                ++failed;
        }
        /*  Release in a different order than allocated.  */
        for (i = 63; i >= 0; i -= 2)
            OM_deallocate(held[i]);
        for (i = 0; i < 64; i += 2)
            OM_deallocate(held[i]);
    }
    CHECK_EQ_INT(failed, 0);
    CHECK_EQ_INT(OM_core_left(), before);

    /*  The image itself must be untouched by all that.  */
    {
        om_census   c;

        OM_census(&c);
        CHECK_EQ_INT(c.objects, 18391);
    }
}

int
main(void)
{
    int status;

    ST_TEST_BEGIN("Blue Book object memory");

    test_small_integers();

    oracle_present = file_exists(IMAGE_PATH);
    if (!oracle_present) {
        printf("  SKIP: %s missing -- see doc/LICENSING.md\n", IMAGE_PATH);
        return ST_TEST_END();
    }

    test_image_loads();
    test_guaranteed_class_names();
    test_refcount_histogram();
    test_class_oops();
    test_no_extra_classes();
    test_method_oops();
    test_allocation();
    test_allocation_churn();

    status = ST_TEST_END();
    OM_shutdown();
    return status;
}

#else   /*  not ST_OM_BB  */

int
main(void)
{
    printf("skipped: this suite validates the Blue Book object memory\n");
    return 0;
}

#endif

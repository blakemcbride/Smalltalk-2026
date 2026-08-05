/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Driver.  For now it loads an image and reports on it, which is how the
 *  object memory gets cross-checked against the Xerox reference dumps.  It
 *  grows into the real entry point: worker pool startup and the SDL3
 *  main-thread pump.
 */

#include "st_port.h"
#include "om.h"
#include "census.h"
#include "interp.h"
#include "gfx.h"
#include "st_sched.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_VERSION      "0.1.0-phase1"

#if defined(ST_OM_BB)
#define ST_OM_NAME      "bb (16-bit Blue Book, validation harness)"
#elif defined(ST_OM_MT)
#define ST_OM_NAME      "mt (64-bit threaded)"
#else
#define ST_OM_NAME      "none configured"
#endif

static void
usage(const char *argv0)
{
    printf("usage: %s [options]\n", argv0);
    printf("\n");
    printf("  -version              print version and build configuration\n");
    printf("  -run <image> [n]      run the image, opening a window\n");
    printf("  -census <image>       load an image and summarize it\n");
    printf("  -classes <image>      list every class, in class.oops format\n");
    printf("  -methods <image>      list every method, in method.oops format\n");
    printf("  -trace2 <image> [n]   bytecode trace, Xerox trace2 format\n");
    printf("  -trace3 <image> [n]   send trace, Xerox trace3 format\n");
    printf("  -inspect <image> <oop>  describe one object (oop in hex)\n");
    printf("  -help                 this message\n");
}

static void
print_version(void)
{
    printf("Smalltalk-2026 %s\n", ST_VERSION);
    printf("  object memory : %s\n", ST_OM_NAME);
    printf("  CPUs          : %d\n", ST_cpu_count());
#if defined(ST_WINDOWS)
    printf("  platform      : Windows\n");
#elif defined(__APPLE__)
    printf("  platform      : macOS\n");
#else
    printf("  platform      : POSIX\n");
#endif
}

static int
load(const char *path)
{
    char    err[256];

    if (OM_init() != 0) {
        fprintf(stderr, "st80: cannot allocate object memory\n");
        return -1;
    }
    if (OM_image_load(path, err, sizeof err) != 0) {
        fprintf(stderr, "st80: %s\n", err[0] ? err : "image load failed");
        return -1;
    }
    return 0;
}

static int
do_census(const char *path)
{
    om_census   c;
    unsigned    i;

    if (load(path) != 0)
        return 1;
    OM_census(&c);
    printf("image            : %s\n", path);
    printf("object space     : %u words (%u bytes)\n",
           st_om_image_object_words, st_om_image_object_words * 2);
    printf("object table     : %u words, %u entries\n",
           st_om_image_ot_words, st_om_image_ot_words / 2);
    printf("live objects     : %u\n", c.objects);
    printf("free entries     : %u\n", c.free_entries);
    printf("pointer objects  : %u\n", c.pointer_objects);
    printf("non-pointer      : %u\n", c.nonpointer_objects);
    printf("odd-length       : %u\n", c.odd_objects);
    printf("words in objects : %llu\n", (unsigned long long) c.total_words);
    printf("sum of refcounts : %llu\n", (unsigned long long) c.total_refcounts);
    printf("\nreference count histogram (count: objects)\n");
    for (i = 0; i < OM_HISTOGRAM_BUCKETS; ++i) {
        if (c.refcount_histogram[i])
            printf("%6u %u\n", c.refcount_histogram[i], i);
    }
    OM_shutdown();
    return 0;
}

static int
do_classes(const char *path)
{
    st_oop  p;
    int     n = 0;

    if (load(path) != 0)
        return 1;
    for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
        char    name[256];

        if (!OM_class_name_of(p, name, sizeof name))
            continue;
        printf("8r%o\t16r%X\t%s\n", (unsigned) p, (unsigned) p, name);
        ++n;
    }
    fprintf(stderr, "%d classes\n", n);
    OM_shutdown();
    return 0;
}

static void
emit_method(st_oop cls, st_oop selector, st_oop method, void *user)
{
    char    cname[256];
    char    sel[256];

    (void) user;
    OM_class_name_of(cls, cname, sizeof cname);
    OM_string_of(selector, sel, sizeof sel);
    printf("8r%o\t16r%X\t<%s>%s\n", (unsigned) method, (unsigned) method,
           cname, sel);
}

static int
do_methods(const char *path)
{
    uint32_t    n;

    if (load(path) != 0)
        return 1;
    n = OM_walk_methods(emit_method, NULL);
    fprintf(stderr, "%u methods\n", n);
    OM_shutdown();
    return 0;
}

/*
 *  Run the image with tracing on, so the output can be diffed against the
 *  Xerox reference traces.  `mode` selects trace2 or trace3 shape.
 */
static int
do_trace(const char *path, st_trace_mode mode, uint64_t limit)
{
    char    err[256];

    if (load(path) != 0)
        return 1;
    if (ST_interp_init(err, sizeof err) != 0) {
        fprintf(stderr, "st80: %s\n", err);
        return 1;
    }
    printf("Copyright (c) 1983 Xerox Corp.  All rights reserved.\n\n");
    ST_trace_set(mode, stdout);
    ST_interp_run(limit);
    ST_trace_set(ST_TRACE_OFF, NULL);
    OM_shutdown();
    return 0;
}

/*
 *  Run the image for real.
 *
 *  The window cannot be opened until the image tells us how big its display
 *  is, which it does by sending beDisplay -- primitive 102.  So the
 *  interpreter runs first, in slices, and the window appears the moment the
 *  display Form is known.  Between slices thread 0 pumps SDL: that is the
 *  only place events are read and the only place pixels are presented.
 */
#define SLICE_BYTECODES     20000

static int
do_run(const char *path, uint64_t max_cycles)
{
    char        err[256];
    uint64_t    total = 0;

    if (load(path) != 0)
        return 1;
    SCHED_reset();
    if (ST_interp_init(err, sizeof err) != 0) {
        fprintf(stderr, "st80: %s\n", err);
        return 1;
    }
    while (st_vm.running) {
        total += ST_interp_run(SLICE_BYTECODES);
        if (max_cycles && total >= max_cycles)
            break;

        if (!GFX_is_open() && GFX_display_form() != ST_NIL) {
            gfx_form    form;

            if (GFX_form_from_oop(GFX_display_form(), &form)) {
                if (GFX_open("Smalltalk-2026", form.width, form.height,
                             err, sizeof err) != 0) {
                    fprintf(stderr, "st80: %s\n", err);
                    return 1;
                }
                fprintf(stderr, "st80: display is %dx%d\n",
                        form.width, form.height);
            }
        }
        if (GFX_is_open() && !GFX_pump())
            break;
    }
    fprintf(stderr, "st80: stopped after %llu bytecodes; "
                    "%u collections reclaimed %u objects; "
                    "%u words and %u table entries free\n",
            (unsigned long long) total, st_om_collections, st_om_reclaimed,
            OM_core_left(), OM_oops_left());
    /*
     *  Who refers to the contexts that will not die?  Walk back up the
     *  reference graph from one of them; the chain of referrers names the
     *  structure that is retaining the lot.
     */
    if (getenv("ST_WHO_REFERS")) {
        st_oop      target = ST_OOP_INVALID;
        st_oop      p;
        int         hop;

        for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
            if (OM_fetch_class(p) == ST_CLASS_METHOD_CONTEXT) {
                target = p;
                break;
            }
        }
        for (hop = 0; hop < 12 && target != ST_OOP_INVALID; ++hop) {
            st_oop      referrer = ST_OOP_INVALID;
            uint32_t    field_no = 0;
            char        name[128];

            for (p = OM_first_object(); p != ST_OOP_INVALID;
                 p = OM_next_object(p)) {
                uint32_t    n;
                uint32_t    i;

                if (p == target || !OM_pointer_bit(p))
                    continue;
                n = OM_fetch_word_length(p);
                for (i = 0; i < n; ++i) {
                    if (OM_fetch_pointer(i, p) == target) {
                        referrer = p;
                        field_no = i;
                        break;
                    }
                }
                if (referrer != ST_OOP_INVALID)
                    break;
            }
            OM_class_name_of(OM_fetch_class(target), name, sizeof name);
            fprintf(stderr, "  16r%X (a%s) count=%u", (unsigned) target,
                    name, OM_count_bits(target));
            if (referrer == ST_OOP_INVALID) {
                fprintf(stderr, " <- nothing found\n");
                break;
            }
            OM_class_name_of(OM_fetch_class(referrer), name, sizeof name);
            fprintf(stderr, " <- field %u of 16r%X (a%s)\n", field_no,
                    (unsigned) referrer, name);
            target = referrer;
        }
    }

    /*  What is holding the object table open?  Count live objects by class. */
    if (getenv("ST_CLASS_CENSUS")) {
        st_oop      p;
        st_oop      classes[512];
        uint32_t    counts[512];
        int         used = 0;
        int         i;

        memset(counts, 0, sizeof counts);
        memset(classes, 0, sizeof classes);
        for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
            st_oop  cls = OM_fetch_class(p);

            for (i = 0; i < used; ++i) {
                if (classes[i] == cls)
                    break;
            }
            if (i == used) {
                if (used >= 512)
                    continue;
                classes[used] = cls;
                counts[used]  = 0;
                ++used;
            }
            ++counts[i];
        }
        for (i = 0; i < used; ++i) {
            char    name[128];

            if (counts[i] < 200)
                continue;
            OM_class_name_of(classes[i], name, sizeof name);
            fprintf(stderr, "  %6u instances of %s\n", counts[i],
                    name[0] ? name : "?");
        }
    }
    if (GFX_is_open())
        GFX_close();
    OM_shutdown();
    return 0;
}

static int
do_inspect(const char *path, const char *oop_text)
{
    unsigned    raw;
    st_oop      p;
    char        name[256];

    if (sscanf(oop_text, "%x", &raw) != 1) {
        fprintf(stderr, "st80: '%s' is not a hex object pointer\n", oop_text);
        return 1;
    }
    if (load(path) != 0)
        return 1;
    p = (st_oop) raw;

    printf("oop            : 16r%X (8r%o, %u)\n", raw, raw, raw);
    if (OM_is_int(p)) {
        printf("kind           : SmallInteger %lld\n",
               (long long) OM_int_value(p));
        OM_shutdown();
        return 0;
    }
    if (!OM_is_object(p)) {
        printf("kind           : not a live object\n");
        OM_shutdown();
        return 0;
    }
    printf("reference count: %u\n", OM_count_bits(p));
    printf("pointer bit    : %u\n", OM_pointer_bit(p));
    printf("odd bit        : %u\n", OM_odd_bit(p));
#ifdef ST_OM_BB
    printf("segment        : %u\n", OM_segment_bits(p));
    printf("location       : 16r%X\n", OM_location(p));
#endif
    printf("size (words)   : %u\n", OM_size_bits(p));
    printf("word length    : %u\n", OM_fetch_word_length(p));
    printf("byte length    : %u\n", OM_fetch_byte_length(p));
    printf("class          : 16r%X", (unsigned) OM_fetch_class(p));
    if (OM_class_name_of(OM_fetch_class(p), name, sizeof name))
        printf(" (%s)", name);
    printf("\n");
    if (OM_class_name_of(p, name, sizeof name))
        printf("is the class   : %s\n", name);
    if (!OM_pointer_bit(p)) {
        OM_string_of(p, name, sizeof name);
        printf("as text        : \"%s\"\n", name);
    }  else  {
        uint16_t    n = OM_fetch_word_length(p);
        uint16_t    i;

        if (n > 16)
            n = 16;
        for (i = 0; i < n; ++i) {
            st_oop  field = OM_fetch_pointer(i, p);

            printf("  [%2u]         : 16r%X", i, (unsigned) field);
            if (OM_is_int(field))
                printf(" = %lld", (long long) OM_int_value(field));
            else if (OM_class_name_of(field, name, sizeof name))
                printf(" = %s", name);
            else if (OM_is_object(field) && !OM_pointer_bit(field)) {
                OM_string_of(field, name, sizeof name);
                printf(" = \"%s\"", name);
            }
            printf("\n");
        }
    }
    OM_shutdown();
    return 0;
}

int
main(int argc, char **argv)
{
    int     i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "-version")) {
            print_version();
            return 0;
        }
        if (!strcmp(argv[i], "-census") && i + 1 < argc)
            return do_census(argv[i + 1]);
        if (!strcmp(argv[i], "-classes") && i + 1 < argc)
            return do_classes(argv[i + 1]);
        if (!strcmp(argv[i], "-methods") && i + 1 < argc)
            return do_methods(argv[i + 1]);
        if (!strcmp(argv[i], "-run") && i + 1 < argc)
            return do_run(argv[i + 1],
                          (i + 2 < argc) ? strtoull(argv[i + 2], NULL, 0) : 0);
        if (!strcmp(argv[i], "-trace2") && i + 1 < argc)
            return do_trace(argv[i + 1], ST_TRACE_BYTECODES,
                            (i + 2 < argc) ? strtoull(argv[i + 2], NULL, 0) : 0);
        if (!strcmp(argv[i], "-trace3") && i + 1 < argc)
            return do_trace(argv[i + 1], ST_TRACE_SENDS,
                            (i + 2 < argc) ? strtoull(argv[i + 2], NULL, 0) : 0);
        if (!strcmp(argv[i], "-inspect") && i + 2 < argc)
            return do_inspect(argv[i + 1], argv[i + 2]);
    }
    print_version();
    return 0;
}

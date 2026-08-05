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
#include "bootstrap.h"
#include "compiler.h"
#include "chunk.h"
#include "survey.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  SDL3 dropped the static SDLmain library: the translation unit holding
 *  main() includes this header instead.  It matters most on macOS, where SDL
 *  has to be the one that stands up the Cocoa run loop on the thread that
 *  entered main() -- that thread and no other can own the window.
 */
#ifdef ST_HAVE_SDL3
#include <SDL3/SDL_main.h>
#endif

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
    printf("  -bootstrap <a.st...> [-manifest f] [-o image] [-eval expr]\n");
    printf("                        [-startup expr]  what a saved image resumes\n");
    printf("                        build an image from source\n");
    printf("  -run <image> [n]      run the image, opening a window\n");
    printf("  -screenshot <f.pbm>   with -run or -bootstrap, write the display\n");
    printf("  -census <image>       load an image and summarize it\n");
    printf("  -classes <image>      list every class, in class.oops format\n");
    printf("  -methods <image>      list every method, in method.oops format\n");
    printf("  -trace2 <image> [n]   bytecode trace, Xerox trace2 format\n");
    printf("  -trace3 <image> [n]   send trace, Xerox trace3 format\n");
    printf("  -inspect <image> <oop>  describe one object (oop in hex)\n");
    printf("  -syntax <f.st...>     compile every method and report failures\n");
    printf("  -help                 this message\n");
    printf("\n");
    printf("  ST_EVAL_TRACE=1       trace the bytecodes an -eval runs\n");
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

static const char *shot_path;

static void write_screenshot(void);

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
    /*
     *  Write the display out as a portable bitmap, so what the image drew
     *  can be looked at without a window -- and so a headless run can prove
     *  it drew anything at all.
     */
    write_screenshot();
    fprintf(stderr, "st80: stopped after %llu bytecodes; "
                    "%u collections reclaimed %u objects; "
                    "%u words and %u table entries free\n",
            (unsigned long long) total, st_om_collections, st_om_reclaimed,
            OM_core_left(), OM_oops_left());
    /*
     *  How deep is the sender chain?  A chain thousands of frames long means
     *  the image is descending without returning; a short one means the
     *  contexts are retained by something else.
     */
    if (getenv("ST_CHAIN")) {
        st_oop      ctx = st_vm.active_context;
        unsigned    depth = 0;
        char        name[128];

        while (OM_is_present(ctx) && depth < 100000) {
            ++depth;
            ctx = OM_fetch_pointer(0, ctx);     /*  sender or caller  */
        }
        fprintf(stderr, "  active sender chain is %u deep\n", depth);

        /*  And how many contexts are reachable from the scheduler?  */
        {
            st_oop      scheduler = OM_fetch_pointer(ST_ASSOCIATION_VALUE,
                                        ST_SCHEDULER_ASSOCIATION);
            st_oop      active;

            if (OM_is_present(scheduler)) {
                active = OM_fetch_pointer(1, scheduler);
                OM_class_name_of(OM_fetch_class(active), name, sizeof name);
                fprintf(stderr, "  active process is a%s\n", name);
                ctx = OM_fetch_pointer(1, active);
                depth = 0;
                while (OM_is_present(ctx) && depth < 100000) {
                    ++depth;
                    ctx = OM_fetch_pointer(0, ctx);
                }
                fprintf(stderr, "  its suspended chain is %u deep\n", depth);
            }
        }
    }

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

/*
 *  Build an image from source and, optionally, evaluate an expression in it.
 *
 *  Evaluating means compiling the expression as the body of a method,
 *  standing up a context whose sender is nil, and running until it returns.
 *  A return with no sender has nowhere to push its answer, so the
 *  interpreter keeps it and stops -- see st_vm.return_value.
 */
static st_oop
evaluate(const char *expression, char *errbuf, size_t errlen)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                source[4096];
    st_oop              context;
    st_oop              method;

    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_array         = BOOT_make_array;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;

    /*
     *  An expression with a caret in it is already a method body, temporary
     *  declarations and all, so it is used as written.  Anything else is a
     *  single expression whose value is wanted.
     */
    if (strchr(expression, '^'))
        snprintf(source, sizeof source, "doIt %s", expression);
    else
        snprintf(source, sizeof source, "doIt ^%s", expression);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        snprintf(errbuf, errlen, "%s", res.error);
        return ST_OOP_INVALID;
    }
    method = res.method;

    context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 32);
    if (!OM_is_object(context)) {
        snprintf(errbuf, errlen, "out of memory building a context");
        return ST_OOP_INVALID;
    }
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    /*
     *  The instruction pointer is stored one-relative and counts bytes from
     *  the start of the method, so it begins past the header and literals.
     */
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int)
                        (BOOT_method_initial_ip(method) + 1)));
    /*
     *  The stack begins ABOVE the temporaries.  A stack pointer of zero puts
     *  the first push on top of temporary zero, so a method that declares
     *  any variables overwrites them with its own working stack -- which
     *  looks exactly like a compiler bug and is not one.
     */
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, method))));

    memset(&st_vm, 0, sizeof st_vm);
    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    {
        const char *mode = getenv("ST_EVAL_TRACE");

        if (mode)
            ST_trace_set(mode[0] == 's' ? ST_TRACE_SENDS : ST_TRACE_BYTECODES,
                         stderr);
    }
    ST_interp_run(2000000);
    ST_trace_set(ST_TRACE_OFF, NULL);
    if (st_vm.running) {
        snprintf(errbuf, errlen, "expression did not finish in 2M bytecodes");
        return ST_OOP_INVALID;
    }
    return st_vm.return_value;
}

/*
 *  Write the display out as a portable bitmap, so what the image drew can be
 *  looked at without a window -- and so a headless run can prove it drew
 *  anything at all.  Used by -run and by -bootstrap alike: a bootstrapped
 *  image has a Display of its own now, and being able to see what it put
 *  there is the only way to tell drawing from not drawing.
 */
static void
write_screenshot(void)
{
    gfx_form    form;
    FILE       *f;
    int         x;
    int         y;
    long        ink = 0;

    if (!shot_path || GFX_display_form() == ST_NIL)
        return;
    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return;
    f = fopen(shot_path, "wb");
    if (!f) {
        fprintf(stderr, "st80: cannot write %s\n", shot_path);
        return;
    }
    fprintf(f, "P1\n%d %d\n", form.width, form.height);
    for (y = 0; y < form.height; ++y) {
        for (x = 0; x < form.width; ++x) {
            uint16_t    word = form.bits[(size_t) y * form.raster + (x >> 4)];
            int         bit  = (word >> (15 - (x & 15))) & 1;

            ink += bit;
            fputc(bit ? '1' : '0', f);
            fputc(x + 1 == form.width ? '\n' : ' ', f);
        }
    }
    fclose(f);
    fprintf(stderr, "st80: wrote %s, %ld of %d pixels are ink\n",
            shot_path, ink, form.width * form.height);
}

static int
do_bootstrap(const char *const *sources, unsigned count, const char *out_path,
             const char *expression, const char *startup)
{
    st_bootstrap_result result;
    char                err[512];

    if (BOOT_build(sources, count, &result) != 0) {
        fprintf(stderr, "st80: bootstrap failed: %s\n", result.error);
        return 1;
    }
    fprintf(stderr, "st80: %u classes, %u methods, %u symbols\n",
            result.classes_created, result.methods_compiled,
            result.symbols_interned);
    {
        /*
         *  Names nothing defined.  Capitalised ones are ordinary forward
         *  references -- Sensor, Display and Transcript are made when an
         *  image is built, long after the code using them is compiled.  A
         *  lower-case one is almost always a bug in the source, and the 1983
         *  library has some; they are reported rather than hidden.
         */
        const char *names[256];
        unsigned    n = BOOT_undeclared(names, 256);
        unsigned    lower = BOOT_undeclared_lowercase();
        unsigned    i;

        if (n) {
            fprintf(stderr, "st80: %u undeclared global%s", n,
                    n == 1 ? "" : "s");
            if (lower)
                fprintf(stderr, ", %u lower-case (probable source bugs)",
                        lower);
            fprintf(stderr, ":");
            {
                unsigned limit = getenv("ST_BOOT_LOG") ? n : 12;

                for (i = 0; i < n && i < limit; ++i)
                    fprintf(stderr, " %s", names[i]);
                if (n > limit)
                    fprintf(stderr, " ... and %u more", n - limit);
            }
            fprintf(stderr, "\n");
        }
    }

    /*
     *  The screen first: several class initializers ask Display how big it
     *  is, and a nil Display makes them fail for a reason that has nothing
     *  to do with what they are initialising.
     */
    if (!BOOT_install_display(640, 480))
        fprintf(stderr, "st80: no display installed\n");

    {
        st_boot_init_report init;

        BOOT_run_initializers(&init);
        fprintf(stderr, "st80: %u class initializers, %u ran, %u unfinished",
                init.defined, init.ran, init.unfinished);
        if (init.unfinished)
            fprintf(stderr, " (first: %s)", init.first_unfinished);
        fprintf(stderr, "; %u of %u symbols in the library table\n",
                init.symbols_seeded, init.symbols_total);
    }

    /*
     *  What the image does when it wakes up.  A saved image needs a process
     *  to resume, and this is the only place that knows what that should be;
     *  it is a string so that it can be changed without changing C.
     */
    if (!BOOT_install_scheduler(startup ? startup :
                                /*
                                 *  beDisplay first: a reloaded image has a
                                 *  DisplayScreen but the VM has not been told
                                 *  which Form is the screen, and that is what
                                 *  primitive 102 is for.
                                 */
                                "Display beDisplay."
                                " ScheduledControllers restore."
                                " [true] whileTrue:"
                                " [ScheduledControllers"
                                " searchForActiveController]"))
        fprintf(stderr, "st80: no startup process installed\n");

    if (expression) {
        st_oop  value = evaluate(expression, err, sizeof err);
        char    text[256];

        if (value == ST_OOP_INVALID) {
            fprintf(stderr, "st80: %s\n", err);
            return 1;
        }
        ST_print_object(value, text, sizeof text);
        printf("%s\n", text);
    }
    write_screenshot();
    if (out_path) {
#ifdef ST_OM_MT
        if (OM_image_save(out_path, err, sizeof err) != 0) {
            fprintf(stderr, "st80: %s\n", err);
            return 1;
        }
        fprintf(stderr, "st80: wrote %s\n", out_path);
#else
        /*
         *  A bootstrapped image is written in the native format, which
         *  belongs to the 64-bit memory.  The Blue Book build can still
         *  bootstrap and evaluate -- useful for checking the compiler
         *  against an interpreter already proven on the Xerox traces -- but
         *  it has nothing to write the result into.
         */
        fprintf(stderr, "st80: -o needs the 64-bit object memory "
                        "(build with OM=mt)\n");
        return 1;
#endif
    }
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
    /*
     *  A CompiledMethod is worth more than a hex dump.  The plan's compiler
     *  gate is a bytecode-for-bytecode diff against the methods Xerox
     *  compiled in 1983, and this is the side of that diff that cannot be
     *  produced any other way.
     */
    if (OM_fetch_class(p) == ST_CLASS_COMPILED_METHOD) {
        st_oop      header = OM_fetch_pointer(0, p);
        unsigned    literals = ST_header_literal_count(header);
        unsigned    start = (literals + ST_METHOD_LITERAL_START)
                            * (unsigned) sizeof(st_oop);
        unsigned    n = OM_fetch_byte_length(p);
        unsigned    i;

        printf("header         : 16r%X\n", (unsigned) header);
        printf("  flag value   : %u\n", ST_header_flag_value(header));
        printf("  temporaries  : %u\n", ST_header_temporary_count(header));
        printf("  literals     : %u\n", literals);
        printf("  large context: %u\n", ST_header_large_context(header));
        for (i = ST_METHOD_LITERAL_START;
             i < literals + ST_METHOD_LITERAL_START; ++i) {
            st_oop  lit = OM_fetch_pointer(i, p);

            printf("  literal %-2u   : 16r%X", i - ST_METHOD_LITERAL_START,
                   (unsigned) lit);
            if (OM_is_int(lit))
                printf(" = %lld", (long long) OM_int_value(lit));
            else if (OM_is_object(lit) && !OM_pointer_bit(lit)) {
                OM_string_of(lit, name, sizeof name);
                printf(" = \"%s\"", name);
            }
            printf("\n");
        }
        printf("bytecodes      :");
        for (i = start; i < n; ++i)
            printf(" %u", OM_fetch_byte(i, p));
        printf("\n");
        OM_shutdown();
        return 0;
    }
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


static int
do_syntax(int argc, char **argv)
{
    st_survey   survey;
    int         i;

    SURVEY_init(&survey);
    for (i = 0; i < argc; ++i)
        SURVEY_file(&survey, argv[i]);
    SURVEY_report(&survey, stdout);
    return (survey.failed || survey.unreadable) ? 1 : 0;
}


/*  ----------  A growable list of source paths  ----------  */

typedef struct {
    char      **items;
    unsigned    count;
    unsigned    capacity;
} path_list;

static int
path_list_add(path_list *l, const char *path)
{
    if (l->count == l->capacity) {
        unsigned    want = l->capacity ? l->capacity * 2 : 32;
        char      **grown = (char **) realloc(l->items, want * sizeof *grown);

        if (!grown)
            return 0;
        l->items    = grown;
        l->capacity = want;
    }
    l->items[l->count] = strdup(path);
    if (!l->items[l->count])
        return 0;
    ++l->count;
    return 1;
}

static void
path_list_free(path_list *l)
{
    unsigned    i;

    for (i = 0; i < l->count; ++i)
        free(l->items[i]);
    free(l->items);
}

/*
 *  One path per line.  The class library is 226 files whose directories have
 *  spaces in their names, which is more than a command line wants to carry.
 */
static int
read_manifest(const char *path, path_list *l)
{
    FILE   *f = fopen(path, "r");
    char    line[1024];

    if (!f) {
        fprintf(stderr, "st80: cannot open manifest %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        size_t  n = strlen(line);

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n && !path_list_add(l, line)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
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
        if (!strcmp(argv[i], "-syntax") && i + 1 < argc)
            return do_syntax(argc - i - 1, argv + i + 1);
        if (!strcmp(argv[i], "-census") && i + 1 < argc)
            return do_census(argv[i + 1]);
        if (!strcmp(argv[i], "-classes") && i + 1 < argc)
            return do_classes(argv[i + 1]);
        if (!strcmp(argv[i], "-methods") && i + 1 < argc)
            return do_methods(argv[i + 1]);
        if (!strcmp(argv[i], "-bootstrap")) {
            path_list   sources;
            const char *out_path = NULL;
            const char *expression = NULL;
            const char *startup = NULL;
            int         j;
            int         status;

            memset(&sources, 0, sizeof sources);
            for (j = i + 1; j < argc; ++j) {
                if (!strcmp(argv[j], "-o") && j + 1 < argc) {
                    out_path = argv[++j];
                }  else if (!strcmp(argv[j], "-eval") && j + 1 < argc) {
                    expression = argv[++j];
                }  else if (!strcmp(argv[j], "-startup") && j + 1 < argc) {
                    startup = argv[++j];
                }  else if (!strcmp(argv[j], "-screenshot") && j + 1 < argc) {
                    shot_path = argv[++j];
                }  else if (!strcmp(argv[j], "-manifest") && j + 1 < argc) {
                    if (!read_manifest(argv[++j], &sources)) {
                        path_list_free(&sources);
                        return 1;
                    }
                }  else if (!path_list_add(&sources, argv[j])) {
                    fprintf(stderr, "st80: out of memory\n");
                    path_list_free(&sources);
                    return 1;
                }
            }
            if (sources.count == 0) {
                fprintf(stderr, "st80: -bootstrap needs source files\n");
                path_list_free(&sources);
                return 1;
            }
            status = do_bootstrap((const char *const *) sources.items,
                                  sources.count, out_path, expression,
                                  startup);
            path_list_free(&sources);
            return status;
        }
        if (!strcmp(argv[i], "-screenshot") && i + 1 < argc) {
            shot_path = argv[++i];
            continue;
        }
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

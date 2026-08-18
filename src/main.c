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

#include <sys/stat.h>
#include "interp.h"
#include "gfx.h"
#include "font.h"
#include "st_sched.h"
#include "bootstrap.h"
#include "profile.h"

/*
 *  Compile expressions as closures rather than Blue Book blocks.
 *
 *  A developer switch for now.  Which dialect a package is written in
 *  belongs in its profile eventually; until the library is ported there is
 *  nothing to attach it to, and being able to run one expression both ways
 *  is exactly what is wanted while the two are being compared.
 */
static int  use_closures;

#define EVAL_BYTECODE_BUDGET    UINT64_C(200000000)

/*
 *  How long one evaluation may run.
 *
 *  Generous by default, because -eval is given whole programs.  The doctest
 *  runner lowers it a hundredfold: an example written in a method comment
 *  to show what the method does is not a computation, and one that does not
 *  answer inside a couple of million bytecodes has hit something that does
 *  not terminate here.  Left at the default, fifteen hundred of them spend
 *  four seconds each discovering that.
 */
static uint64_t evaluate_budget = EVAL_BYTECODE_BUDGET;
#include "compiler.h"
#include "chunk.h"
#include "survey.h"
#include "doctest.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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

/*
 *  Sanitizer builds say so.
 *
 *  A TSAN binary interprets some fifty times slower than a plain one, and
 *  the difference between "this system is slow" and "this binary is
 *  instrumented" is otherwise invisible: same name, same size to the eye,
 *  same version line.
 */
#if defined(__SANITIZE_THREAD__)
#define ST_SANITIZER    "thread sanitizer -- MUCH slower than a plain build"
#elif defined(__SANITIZE_ADDRESS__)
#define ST_SANITIZER    "address sanitizer -- much slower than a plain build"
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define ST_SANITIZER    "thread sanitizer -- MUCH slower than a plain build"
#elif __has_feature(address_sanitizer)
#define ST_SANITIZER    "address sanitizer -- much slower than a plain build"
#endif
#endif

static int  do_disasm(const char *path, const char *class_name,
                      const char *selector);

static void
usage(const char *argv0)
{
    printf("usage: %s [options]\n", argv0);
    printf("\n");
    printf("  -version              print version and build configuration\n");
    printf("  -bootstrap <a.st...> [-profile p] [-manifest f] [-o image]\n");
    printf("                       [-eval expr] [-startup expr] [-closures]\n");
    printf("                        [-startup expr]  what a saved image resumes\n");
    printf("                        build an image from source\n");
    printf("  -run <image> [n]      run the image, opening a window\n");
    printf("  -screenshot <f.pbm>   with -run or -bootstrap, write the display\n");
    printf("  -inject <script>      post input: m X Y, d CODE, u CODE,\n");
    printf("                        k CODE, w SLICES (wait)\n");
    printf("  -wiggle               move the pointer continuously, no clicks\n");
    printf("  -census <image>       load an image and summarize it\n");
    printf("  -classes <image>      list every class, in class.oops format\n");
    printf("  -methods <image>      list every method, in method.oops format\n");
    printf("  -trace2 <image> [n]   bytecode trace, Xerox trace2 format\n");
    printf("  -trace3 <image> [n]   send trace, Xerox trace3 format\n");
    printf("  -inspect <image> <oop>  describe one object (oop in hex)\n");
    printf("  -disasm <image> <Class> <selector>   dump a method's bytecodes\n");
    printf("  -syntax <f.st...>     compile every method and report failures\n");
    printf("  -primitives <f.st...> every primitive that source asks the VM "
           "for\n");
    printf("        -tests            with -bootstrap: run every SUnit test "
           "and exit non-zero\n"
           "                          if any did not pass\n");
    printf("        -doctests <f.st>  with -bootstrap: run the \"expr >>> "
           "value\" examples in\n"
           "                          that file's method comments\n");
    printf("        both of the above also take -profile <p.profile>\n");
    printf("  -help                 this message\n");
    printf("\n");
    printf("  ST_EVAL_TRACE=1       trace the bytecodes an -eval runs\n");
    printf("  ST_BOTTOM_LOG=1       report a return off the bottom of a stack\n");
    printf("\n");
    printf("  ST_DISPLAY_THEME      paper (default), classic, dark\n");
    printf("  ST_DISPLAY_SCALE      screen pixels per display pixel\n");
    printf("  ST_DISPLAY_WINDOW     WxH: open the window at this size\n");
    printf("  ST_DISPLAY_FIT=off    keep the image's own screen size instead\n"
           "                        of growing it to fill the window\n");
    printf("  ST_DISPLAY_PRESENTATION  integer, letterbox, stretch\n");
    printf("  ST_DISPLAY_TRACE=1    report why a resize was refused\n");
}

static void
print_version(void)
{
    printf("Smalltalk-2026 %s\n", ST_VERSION);
    printf("  object memory : %s\n", ST_OM_NAME);
#ifdef ST_SANITIZER
    printf("  instrumented  : %s\n", ST_SANITIZER);
#endif
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
    SCHED_timer_stop();
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
    SCHED_timer_stop();
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
    SCHED_timer_stop();
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
    SCHED_timer_stop();
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

/*
 *  Input to post as though it came from the window.
 *
 *  "m X Y" moves the pointer, "d CODE" presses a button, "u CODE" releases
 *  it, "k CODE" taps a key: 128 is the blue button, 129 yellow, 130 red.
 *  It exists so the interactive half can be driven without a person at the
 *  mouse -- what it posts goes through the same queue SDL fills, so what it
 *  drives is the real path.
 */
static const char *inject_script;
static int         wiggle;

/*
 *  ----------  Telling the image its screen changed size  ----------
 *
 *  Growing the display Form is not enough, and the way it fails is worth
 *  writing down because nothing about it looks like a display fault.
 *
 *  ControlManager>>searchForActiveController offers control only to a
 *  controller whose view contains the cursor, and the one that answers for
 *  the desktop is screenController, whose view was windowed to
 *  `Display boundingBox' when the snapshot was taken.  Grow the screen and
 *  that rectangle is stale: with the cursor in the new area NO controller
 *  wants control, the search loop spins for ever, and the buttons are never
 *  read.  The desktop looks right and the mouse does nothing at all.
 *
 *  ControlManager>>restore is the message that fixes it, and sending a
 *  message from C between bytecodes cannot be done safely -- the reply would
 *  land on the stack of whatever frame we interrupted, one slot above where
 *  its own bytecodes expect to find things.  So do what `View>>setWindow:'
 *  does instead, which is a state change and not a computation: set the
 *  window, drop the viewport, and unlock the cached transformation and inset
 *  box so the image recomputes them the next time it asks.
 *
 *  Every field is found BY NAME, through the class's own instanceVariables.
 *  Reaching in by index would be a second copy of a layout the image already
 *  publishes, and would rot silently the first time an image differed.  If
 *  any name is missing the answer is 0 and the screen is not resized at all
 *  -- letterboxing a 640x480 desktop is a much smaller disappointment than a
 *  desktop whose mouse does nothing.
 */

/*  Behavior: superclass methodDict format subclasses; then ClassDescription's
 *  own instanceVariables, which is the Array of names we want.  */
#define CLASS_INSTANCE_VARIABLES    4

static st_oop
image_global(const char *name)
{
    uint32_t    n = OM_is_present(ST_SMALLTALK)
                        ? OM_fetch_word_length(ST_SMALLTALK) : 0;
    uint32_t    k;

    for (k = 0; k < n; ++k) {
        st_oop  entry = OM_fetch_pointer(k, ST_SMALLTALK);
        char    text[128];

        if (!OM_is_present(entry) || !OM_pointer_bit(entry)
         || OM_fetch_word_length(entry) < 2)
            continue;
        OM_string_of(OM_fetch_pointer(0, entry), text, sizeof text);
        if (strcmp(text, name) == 0)
            return OM_fetch_pointer(1, entry);
    }
    return ST_NIL;
}

static int
instvar_index(st_oop class_oop, const char *name)
{
    st_oop  chain[64];
    int     depth = 0;
    int     base  = 0;
    int     i;

    while (OM_is_present(class_oop) && depth < 64) {
        chain[depth++] = class_oop;
        class_oop = OM_fetch_pointer(ST_CLASS_SUPERCLASS, class_oop);
    }
    /*  The root's variables come first, so count downwards.  */
    for (i = depth - 1; i >= 0; --i) {
        st_oop      names = OM_fetch_pointer(CLASS_INSTANCE_VARIABLES,
                                             chain[i]);
        uint32_t    n = (OM_is_present(names) && OM_pointer_bit(names))
                            ? OM_fetch_word_length(names) : 0;
        uint32_t    k;

        for (k = 0; k < n; ++k) {
            char    text[64];

            OM_string_of(OM_fetch_pointer(k, names), text, sizeof text);
            if (strcmp(text, name) == 0)
                return base + (int) k;
        }
        base += (int) n;
    }
    return -1;
}

/*  nil is an object; it is not one of these.  */
static int
is_instance(st_oop p)
{
    return p != ST_NIL && OM_is_present(p) && OM_is_object(p);
}

static int
fetch_named(st_oop object, const char *name, st_oop *out)
{
    int i;

    if (!is_instance(object))
        return 0;
    i = instvar_index(OM_fetch_class(object), name);
    if (i < 0)
        return 0;
    *out = OM_fetch_pointer((uint32_t) i, object);
    return 1;
}

static int
store_named(st_oop object, const char *name, st_oop value)
{
    int i;

    if (!is_instance(object))
        return 0;
    i = instvar_index(OM_fetch_class(object), name);
    if (i < 0)
        return 0;
    OM_store_pointer((uint32_t) i, object, value);
    return 1;
}

static st_oop
make_point(int x, int y)
{
    st_oop  p = OM_instantiate_pointers(ST_CLASS_POINT, 2);

    if (!OM_is_present(p))
        return ST_NIL;
    OM_store_pointer(0, p, OM_int_oop((st_int) x));
    OM_store_pointer(1, p, OM_int_oop((st_int) y));
    return p;
}

/*  View>>unlock, which recurses so a composite view recomputes throughout. */
static void
unlock_view(st_oop view, int depth)
{
    st_oop      subs;
    uint32_t    n;
    uint32_t    k;

    if (!is_instance(view) || depth > 32)
        return;
    store_named(view, "displayTransformation", ST_NIL);
    store_named(view, "insetDisplayBox", ST_NIL);
    if (!fetch_named(view, "subViews", &subs) || !is_instance(subs)
     || !OM_pointer_bit(subs))
        return;
    n = OM_fetch_word_length(subs);
    for (k = 0; k < n; ++k)
        unlock_view(OM_fetch_pointer(k, subs), depth + 1);
}

/*
 *  Refusing is a legitimate answer, and a silent one is impossible to act on:
 *  what the user sees is a screen that would not grow, with no way to tell a
 *  deliberate refusal from a fault.  ST_DISPLAY_TRACE=1 names the step.
 */
static int
screen_refused(const char *why)
{
    if (getenv("ST_DISPLAY_TRACE"))
        fprintf(stderr, "st80: the screen was not resized: %s\n", why);
    return 0;
}

/*
 *  ----------  Bringing an old image's font up to date  ----------
 *
 *  The face is the VM's, not the image's.  The 1983 sources are code and
 *  carry no font data, so the bootstrap paints one into a StrikeFont and
 *  leaves it in the image as DefaultFont -- which means an image built last
 *  month still draws with last month's face, and improving the face here
 *  does nothing at all for the image somebody already has.  That is a poor
 *  bargain for a resource the image never owned: it would mean rebuilding
 *  an image to fix its typography, and no snapshot of any work would ever
 *  get the fix.
 *
 *  So refresh it on load.  The glyph strike, the extent, and the metrics all
 *  come from font.h, and the metrics matter most: TextStyle>>
 *  gridForFont:withLead: answers `font height + lead' for the line grid and
 *  `font ascent' for the baseline, so a stale ascent is stale line spacing
 *  everywhere.  The style's own copies are set too, for the paths that read
 *  them rather than recomputing.
 *
 *  Only when the height actually differs, and only through fields found by
 *  name.  An image with no DefaultFont -- the Xerox VirtualImage, which has
 *  its own real fonts and must keep them -- is left alone, which is also
 *  what happens when any name is missing.
 */
#define STRIKE_CODES    128
#define STRIKE_WIDTH    (STRIKE_CODES * ST_FONT_WIDTH)

static void
font_follows_vm(void)
{
    st_oop      style = image_global("DefaultTextStyle");
    st_oop      fonts;
    st_oop      glyphs;
    st_oop      old_bits;
    st_oop      new_bits;
    gfx_form    strike;
    uint32_t    raster = STRIKE_WIDTH / 16;
    uint32_t    count;
    uint32_t    k;
    unsigned    code;
    unsigned    row;

    /*
     *  Reached through the style rather than through DefaultFont.  That is
     *  the global the bootstrap defines, and it is not what an image
     *  carries -- it is gone by the time one is written -- while the style
     *  holds the same four faces and is what every piece of text asks.
     *  Going through the thing that is used rather than the thing that was
     *  defined is why this finds anything at all.
     */
    if (!is_instance(style) || !fetch_named(style, "fontArray", &fonts))
        return;
    if (!is_instance(fonts) || !OM_pointer_bit(fonts))
        return;
    count = OM_fetch_word_length(fonts);
    if (count == 0)
        return;
    if (!fetch_named(OM_fetch_pointer(0, fonts), "glyphs", &glyphs))
        return;
    if (!GFX_form_from_oop(glyphs, &strike))
        return;
    if (strike.height == ST_FONT_HEIGHT && strike.width == STRIKE_WIDTH)
        return;                         /*  already this face  */

    old_bits = OM_fetch_pointer(ST_FORM_BITS, glyphs);
    new_bits = OM_instantiate_words(OM_fetch_class(old_bits),
                                    raster * ST_FONT_HEIGHT);
    if (!OM_is_present(new_bits))
        return;
    {
        uint16_t   *dst = OM_word_base(new_bits);
        size_t      n = (size_t) raster * ST_FONT_HEIGHT;
        size_t      i;

        for (i = 0; i < n; ++i)
            dst[i] = 0;
        for (code = ST_FONT_FIRST; code <= ST_FONT_LAST; ++code) {
            unsigned    x = code * ST_FONT_WIDTH;

            for (row = 0; row < ST_FONT_HEIGHT; ++row) {
                uint8_t     glyph = ST_FONT_GLYPHS[code - ST_FONT_FIRST][row];
                uint32_t    index = row * raster + x / 16;
                unsigned    shift = (x % 16 == 0) ? 8 : 0;

                dst[index] = (uint16_t) (dst[index]
                                         | ((uint16_t) glyph << shift));
            }
        }
    }
    OM_store_pointer(ST_FORM_BITS,   glyphs, new_bits);
    OM_store_pointer(ST_FORM_WIDTH,  glyphs, OM_int_oop(STRIKE_WIDTH));
    OM_store_pointer(ST_FORM_HEIGHT, glyphs, OM_int_oop(ST_FONT_HEIGHT));

    /*
     *  The four faces share this one strike -- the bootstrap makes bold,
     *  italic and bold italic by copying every field -- so the glyphs are
     *  done for all of them.  ascent and descent are SmallIntegers and were
     *  copied, so each face carries its own and each has to be told.
     */
    for (k = 0; k < count; ++k) {
        st_oop  face = OM_fetch_pointer(k, fonts);

        store_named(face, "ascent",  OM_int_oop(ST_FONT_ASCENT));
        store_named(face, "descent", OM_int_oop(ST_FONT_DESCENT));
    }
    store_named(style, "lineGrid", OM_int_oop(ST_FONT_HEIGHT));
    store_named(style, "baseline", OM_int_oop(ST_FONT_ASCENT));

    fprintf(stderr, "st80: the image's font was %dx%d; it is now %dx%d, "
                    "ascent %d descent %d\n",
            ST_FONT_WIDTH, strike.height, ST_FONT_WIDTH, ST_FONT_HEIGHT,
            ST_FONT_ASCENT, ST_FONT_DESCENT);
}

static int
screen_follows_display(int width, int height)
{
    st_oop  manager = image_global("ScheduledControllers");
    st_oop  rect_class = image_global("Rectangle");
    st_oop  screen;
    st_oop  view;
    st_oop  rect;
    st_oop  origin;
    st_oop  corner;

    if (!is_instance(manager))
        return screen_refused("the image has no ScheduledControllers");
    if (!is_instance(rect_class))
        return screen_refused("the image has no Rectangle");
    if (!fetch_named(manager, "screenController", &screen))
        return screen_refused("ControlManager has no screenController field");
    if (!fetch_named(screen, "view", &view) || !is_instance(view))
        return screen_refused("the screen controller has no view");

    origin = make_point(0, 0);
    corner = make_point(width, height);
    rect   = OM_instantiate_pointers(rect_class, 2);
    if (!OM_is_present(origin) || !OM_is_present(corner)
     || !OM_is_present(rect))
        return screen_refused("no room for the new window rectangle");
    if (!store_named(rect, "origin", origin)
     || !store_named(rect, "corner", corner))
        return screen_refused("Rectangle has no origin and corner fields");

    /*  View>>setWindow: -- window, then the two caches it invalidates.  */
    if (!store_named(view, "window", rect))
        return screen_refused("View has no window field");
    store_named(view, "viewport", ST_NIL);
    unlock_view(view, 0);
    return 1;
}

/*
 *  How many slices to wait before reading the next command.
 *
 *  The script used to be posted in one burst, which cannot drive a menu: a
 *  Smalltalk-80 menu tracks the pointer in a loop while the button is held,
 *  so down, move and up arriving in the same instant are seen by that loop
 *  as a button already released.  `w <slices>' gives the image time to run
 *  between events, which is what makes a scripted menu selection -- and so
 *  a scripted browser, and Phase J's thirty minutes of scripted input --
 *  possible at all.
 */
static int inject_wait;

static void
run_inject_script(void)
{
    const char *p = inject_script;

    if (inject_wait > 0) {
        --inject_wait;
        return;
    }
    while (p && *p) {
        char    what = *p++;
        long    a = 0;
        long    b = 0;

        while (*p == ' ')
            ++p;
        a = strtol(p, (char **) &p, 10);
        if (what == 'm') {
            while (*p == ' ')
                ++p;
            b = strtol(p, (char **) &p, 10);
            GFX_inject_mouse((int) a, (int) b);
        }  else if (what == 'd') {
            GFX_inject_button((unsigned) a, 1);
        }  else if (what == 'u') {
            GFX_inject_button((unsigned) a, 0);
        }  else if (what == 'k') {
            GFX_inject_key((unsigned) a, 1);
            GFX_inject_key((unsigned) a, 0);
        }  else if (what == 'w') {
            while (*p == ' ' || *p == ';')
                ++p;
            inject_script = p;
            inject_wait = (int) a;
            return;                 /*  resume here after the wait  */
        }
        while (*p == ' ' || *p == ';')
            ++p;
    }
    inject_script = NULL;
}

static void write_screenshot(void);

static int
do_run(const char *path, uint64_t max_cycles)
{
    char        err[256];
    uint64_t    total = 0;
    /*
     *  Why the run ended.
     *
     *  There are four ways out of the loop below and they used to look
     *  identical from outside -- the same "stopped after N bytecodes" line
     *  whether the window had been closed, the image had nothing left to
     *  run, or a limit had been reached.  A system that stops on its own is
     *  the one case where the reason is the whole of what you want to know.
     */
    const char *why = "the image stopped";


    if (load(path) != 0)
        return 1;
    GFX_set_screen_hook(screen_follows_display);
    font_follows_vm();
    SCHED_reset();
    if (ST_interp_init(err, sizeof err) != 0) {
        fprintf(stderr, "st80: %s\n", err);
        return 1;
    }
    while (st_vm.running) {
        total += ST_interp_run(SLICE_BYTECODES);
        /*
         *  Scripted input goes in after the first slice, by which time the
         *  image has started its input process and can drain it.  Posting it
         *  before that would fill the queue and signal a semaphore nobody is
         *  waiting on yet.
         */
        if (inject_script && total >= SLICE_BYTECODES)
            run_inject_script();
        /*
         *  Move the pointer, and nothing else.
         *
         *  A user resting a hand on the mouse posts a stream of motion and
         *  no buttons at all, which is a load nothing else here produces:
         *  two events and two semaphore signals per move, each waking a
         *  process that outranks the one running.  Reproducing that without
         *  a mouse is the only way to test it.
         */
        if (wiggle) {
            static int  step;
            gfx_form    form;

            if (GFX_form_from_oop(GFX_display_form(), &form)) {
                /*  A circle, so it keeps moving and stays on the screen.  */
                double  a = (double) step * 0.05;
                int     cx = form.width / 2;
                int     cy = form.height / 2;

                GFX_inject_mouse(cx + (int) (cos(a) * (form.width / 3)),
                                 cy + (int) (sin(a) * (form.height / 3)));
                ++step;
            }
        }
        if (max_cycles && total >= max_cycles) {
            why = "the bytecode limit was reached";
            break;
        }

        if (!GFX_is_open() && GFX_display_form() != ST_NIL) {
            gfx_form    form;

            if (GFX_form_from_oop(GFX_display_form(), &form)) {
                if (GFX_open("Smalltalk-2026", form.width, form.height,
                             err, sizeof err) != 0) {
                    fprintf(stderr, "st80: %s\n", err);
                    return 1;
                }
                {
                    int ww = 0, wh = 0;

                    /*  Opening may have resized the screen to the window.  */
                    GFX_form_from_oop(GFX_display_form(), &form);
                    GFX_window_size(&ww, &wh);
                    fprintf(stderr, "st80: display %dx%d at %dx in a %dx%d "
                                    "window, %s\n",
                            form.width, form.height, GFX_scale(), ww, wh,
                            GFX_presentation());
                }
            }
        }
        if (GFX_is_open() && !GFX_pump()) {
            why = "the window was closed";
            break;
        }
    }
    /*
     *  Write the display out as a portable bitmap, so what the image drew
     *  can be looked at without a window -- and so a headless run can prove
     *  it drew anything at all.
     */
    write_screenshot();
    {
        unsigned long   damages = 0, presents = 0;

        GFX_draw_counts(&damages, &presents);
        if (getenv("ST_DISPLAY_TRACE"))
            fprintf(stderr, "st80: %lu draws to the display, %lu presented\n",
                    damages, presents);
    }
    if (GFX_events_dropped())
        fprintf(stderr, "st80: %u input events were dropped -- the image "
                        "rebuilds its button state from this stream, so it "
                        "may have been left wrong\n",
                GFX_events_dropped());
    if (ST_quit_requested)
        why = "the image quit";
    fprintf(stderr, "st80: %s\n", why);
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
    SCHED_timer_stop();
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
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;
    ctx.dialect            = use_closures ? ST_DIALECT_CLOSURES
                                          : ST_DIALECT_BLUE_BOOK;

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

    /*
     *  This thread is about to interpret, so the collector has to be able
     *  to see its stack.  provide_roots also visits the running thread
     *  unconditionally; both are cheap and only one of them can be
     *  forgotten.
     */
    ST_interp_register();
    memset(&st_vm, 0, sizeof st_vm);
    st_vm.active_context = ST_NIL;
    /*
     *  Give the expression a green Process to be, before it runs.
     *
     *  Without one it is a bare context that the scheduler has never heard
     *  of, and that is fine right up until the expression FORKS.  When a
     *  forked process finishes it suspends, the scheduler looks for
     *  something else to run, finds nothing -- the expression that forked
     *  it is not on any ready list because it is not a process at all --
     *  and declares the image deadlocked, stopping the whole evaluation
     *  with whatever return value happened to be lying about.
     *
     *  This is a correctness fix on its own -- an expression that forks
     *  should be something the scheduler can return to -- and it is NOT a
     *  cure for the symptom that prompted it.  With MessageSend>>cull:
     *  added, every AnnouncerTest still passes singly while the expression
     *  that runs them all still answers nil, exactly as before.  So the
     *  driver not being a process was real and was not the cause; see
     *  task #59.
     */
    {
        st_oop  assoc = BOOT_lookup_global("Process", NULL);
        st_oop  cls   = OM_is_object(assoc)
                            ? OM_fetch_pointer(ST_ASSOCIATION_VALUE, assoc)
                            : ST_OOP_INVALID;

        if (OM_is_object(cls)) {
            st_oop  proc = OM_instantiate_pointers(cls, 4);

            if (OM_is_object(proc)) {
                OM_store_pointer(ST_LINK_NEXT, proc, ST_NIL);
                OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, proc, context);
                OM_store_pointer(ST_PROCESS_PRIORITY, proc, OM_int_oop(4));
                OM_store_pointer(ST_PROCESS_MY_LIST, proc, ST_NIL);
                OM_increase_ref(proc);
                st_vm.active_process = proc;
            }
        }
    }
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    {
        const char *mode = getenv("ST_EVAL_TRACE");

        if (mode)
            ST_trace_set(mode[0] == 's' ? ST_TRACE_SENDS : ST_TRACE_BYTECODES,
                         stderr);
    }
    /*
     *  Generous, because the budget is only here to stop a runaway
     *  expression: two million was enough for a probe and not enough for a
     *  recursive block, and an expression that fails for want of budget
     *  looks exactly like one that is wrong.
     */
    ST_interp_run(evaluate_budget);
    ST_trace_set(ST_TRACE_OFF, NULL);
    if (st_vm.running) {
        snprintf(errbuf, errlen, "expression did not finish in %llu bytecodes",
                 (unsigned long long) evaluate_budget);
        return ST_OOP_INVALID;
    }
    return st_vm.return_value;
}

/*
 *  Dump a method: its header, its literal frame, and its bytecodes.
 *
 *  The image can do this itself -- CompiledMethod>>symbolic is in the 1983
 *  sources -- but it needs a working image to do it, which is exactly what
 *  one does not have when a method is misbehaving.  This reads the bytes.
 */
static int
do_disasm(const char *path, const char *class_name, const char *selector)
{
    st_oop      cls;
    st_oop      method;
    st_oop      header;
    uint32_t    literals;
    uint32_t    start;
    uint32_t    n;
    uint32_t    i;
    int         meta = 0;
    char        name[128];

    if (load(path) != 0)
        return 1;

    snprintf(name, sizeof name, "%s", class_name);
    /*  "Foo class" names the metaclass, where class-side methods live.  */
    {
        char   *space = strstr(name, " class");

        if (space) {
            *space = '\0';
            meta = 1;
        }
    }
    /*
     *  Looked up in the image's own Smalltalk, not the builder's table:
     *  this reads an image off disk, and nothing built it in this process.
     */
    cls = ST_NIL;
    {
        uint32_t    n = OM_is_present(ST_SMALLTALK)
                            ? OM_fetch_word_length(ST_SMALLTALK) : 0;
        uint32_t    k;

        for (k = 0; k < n; ++k) {
            st_oop  entry = OM_fetch_pointer(k, ST_SMALLTALK);
            char    text[128];

            if (!OM_is_present(entry) || !OM_pointer_bit(entry)
             || OM_fetch_word_length(entry) < 2)
                continue;
            OM_string_of(OM_fetch_pointer(0, entry), text, sizeof text);
            if (strcmp(text, name) == 0) {
                cls = OM_fetch_pointer(1, entry);
                break;
            }
        }
    }
    if (!OM_is_present(cls)) {
        fprintf(stderr, "st80: no class named %s\n", name);
        return 1;
    }
    if (meta)
        cls = OM_fetch_class(cls);

    /*  Up the chain, as a send would go.  */
    method = ST_NIL;
    {
        st_oop  search = cls;

        while (OM_is_present(search) && !OM_is_present(method)) {
            st_oop      dict = OM_fetch_pointer(1, search);
            uint32_t    capacity = OM_method_dict_capacity(dict);
            uint32_t    slot;

            for (slot = 0; slot < capacity; ++slot) {
                st_oop      key = OM_method_dict_key(dict, slot);
                char        text[128];

                if (!OM_is_present(key))
                    continue;
                OM_string_of(key, text, sizeof text);
                if (strcmp(text, selector) == 0) {
                    method = OM_method_dict_value(dict, slot);
                    break;
                }
            }
            search = OM_fetch_pointer(0, search);    /*  superclass  */
        }
    }
    if (!OM_is_present(method)) {
        fprintf(stderr, "st80: %s does not understand #%s\n",
                class_name, selector);
        return 1;
    }

    header   = OM_fetch_pointer(0, method);
    literals = ST_header_literal_count(header);
    start    = BOOT_method_initial_ip(method);
    n        = OM_fetch_byte_length(method);

    printf("%s>>%s\n", class_name, selector);
    printf("  header      : %lld (flag %u, %u temporaries, %u literals%s)\n",
           (long long) OM_int_value(header),
           ST_header_flag_value(header),
           ST_header_temporary_count(header),
           literals,
           ST_header_large_context(header) ? ", large context" : "");
    for (i = 0; i < literals; ++i) {
        char    text[128];

        ST_print_object(OM_fetch_pointer(1 + i, method), text, sizeof text);
        printf("  literal %-3u : %s\n", i, text);
    }
    /*  The last three bytes are the source pointer, not code.  */
    if (n > start + 3)
        n -= 3;
    printf("  bytecodes   : %u..%u\n", start, n - 1);
    for (i = start; i < n; ++i) {
        char        text[128];
        uint8_t     b = (uint8_t) OM_fetch_byte(i, method);
        unsigned    extra = ST_bytecode_operand_bytes(b);
        unsigned    k;

        ST_bytecode_name(b, text, sizeof text);
        printf("    %4u: %3u", i, b);
        for (k = 1; k <= extra && i + k < n; ++k)
            printf(" %3u", (unsigned) (uint8_t) OM_fetch_byte(i + k, method));
        for (k = extra; k < 2; ++k)
            printf("    ");
        printf("  %s\n", text);
        i += extra;
    }
    return 0;
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
bootstrap_is_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

static int
do_bootstrap(const char *const *sources, const int *dialects, unsigned count,
             const char *out_path, const char *expression, const char *startup,
             int run_tests, const st_names *doctest_paths)
{
    st_bootstrap_result result;
    char                err[512];

    if (BOOT_build_dialects(sources, dialects, count, &result) != 0) {
        fprintf(stderr, "st80: bootstrap failed: %s\n", result.error);
        return 1;
    }
    fprintf(stderr, "st80: %u classes, %u methods, %u symbols\n",
            result.classes_created, result.methods_compiled,
            result.symbols_interned);
    if (result.classes_rejected)
        fprintf(stderr, "st80: %u class definitions this system cannot build "
                        "were skipped (named above)\n",
                result.classes_rejected);
    if (result.traits_rejected)
        fprintf(stderr, "st80: %u classes were built without the traits they "
                        "asked for (named above)\n",
                result.traits_rejected);
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
        fprintf(stderr, "st80: %u class initializers, %u ran, %u skipped,"
                        " %u unfinished",
                init.defined, init.ran, init.skipped, init.unfinished);
        if (init.unfinished)
            fprintf(stderr, " (%s)", init.unfinished_names[0]
                            ? init.unfinished_names : init.first_unfinished);
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

    /*
     *  Run every test in the image and answer through the exit code.
     *
     *  The point of SUnit here is to turn "did the port work" from a
     *  judgement into a number, and that only pays off if a build script
     *  can ask.  It runs before -eval so that a failing suite stops there
     *  rather than going on to evaluate something in a broken image.
     */
    /*
     *  Pharo's own examples, run against this image.
     *
     *  They answer the question the port actually cares about.  "st80
     *  -syntax" says whether Pharo's source parses here; this says whether
     *  it MEANS here what it means there, and it says so in a number that
     *  nobody had to write by hand -- the examples came with the methods.
     *
     *  A doctest is checked by evaluating "(expression) = (expected)",
     *  which is what the notation means.  Three outcomes are told apart,
     *  because they are three different pieces of news: a pass, a WRONG
     *  ANSWER -- the method exists here and does something else -- and a
     *  failure to run at all, which is almost always a class or a selector
     *  this image has not got yet.
     */
    if (doctest_paths->count) {
        st_doctest_list list;
        unsigned        i;
        unsigned        passed = 0;
        unsigned        wrong = 0;
        unsigned        unrunnable = 0;
        unsigned        shown = 0;
        st_oop          raised = BOOT_intern_symbol("stDoctestRaised", NULL);
        uint64_t        saved_budget = evaluate_budget;

        evaluate_budget = UINT64_C(2000000);
        memset(&list, 0, sizeof list);
        for (i = 0; i < doctest_paths->count; ++i) {
            char    scan_error[512] = "";

            if (!DOCTEST_scan(doctest_paths->items[i], &list,
                              scan_error, sizeof scan_error))
                fprintf(stderr, "st80: %s: %s\n",
                        doctest_paths->items[i], scan_error);
        }
        for (i = 0; i < list.count; ++i) {
            char    source[2048];
            st_oop  value;

            /*
             *  Under a handler, so that a selector this image has not got
             *  is told apart from an answer that is merely different.  The
             *  two are different news: one is a method to port, the other
             *  is a method that is here and disagrees -- and only the
             *  second is a bug.  Without the handler every missing
             *  selector counted as a wrong answer and printed a backtrace
             *  on the way past.
             */
            snprintf(source, sizeof source,
                     "^[(%s) = (%s)] on: Error do: [:e | #stDoctestRaised]",
                     list.items[i].expression, list.items[i].expected);
            value = evaluate(source, err, sizeof err);
            if (value == ST_TRUE) {
                ++passed;
                continue;
            }
            if (value == ST_OOP_INVALID || value == raised)
                ++unrunnable;
            else
                ++wrong;
            /*
             *  A few, named, rather than all of them: the point of the
             *  number is to move, and a thousand lines of output is not
             *  something anybody reads twice.
             */
            if (value != ST_OOP_INVALID && value != raised && shown < 12) {
                ++shown;
                fprintf(stderr, "st80:   wrong: %s  (%s:%u, %s)\n",
                        list.items[i].expression, list.items[i].file,
                        list.items[i].line, list.items[i].where);
            }
        }
        fprintf(stderr, "st80: %u doctests in %u methods of %u files: "
                        "%u passed, %u wrong, %u need something not here\n",
                list.count, list.methods, list.files,
                passed, wrong, unrunnable);
        DOCTEST_free(&list);
        evaluate_budget = saved_budget;
        if (wrong)
            return 1;
    }

    if (run_tests) {
        /*
         *  One run, asked two questions.
         *
         *  This used to evaluate `TestCase allTests run' twice -- once to
         *  print the report and again to ask whether it passed -- which ran
         *  every test in the image twice and could answer on a different run
         *  than the one it printed.
         *
         *  One expression rather than a global between two, because each
         *  -eval is compiled on its own against the globals the image was
         *  BUILT with: a name put into Smalltalk at run time is not one the
         *  next expression's compiler can see, so it reads as nil and the
         *  question goes to nobody.
         */
        st_oop  passed = evaluate(
            "| r | r := TestCase allTests run. r report. ^r hasPassed",
            err, sizeof err);

        if (passed == ST_OOP_INVALID) {
            fprintf(stderr, "st80: %s\n", err);
            return 1;
        }
        if (passed != ST_TRUE)
            return 1;
    }

    if (expression) {
        st_oop  value = evaluate(expression, err, sizeof err);
        char    text[256];

        if (value == ST_OOP_INVALID) {
            fprintf(stderr, "st80: %s\n", err);
            return 1;
        }
        /*
         *  A String or Symbol answers its own text.  ST_print_object names
         *  the class, which is right for an object with no obvious reading
         *  and useless for the one kind that has one -- and the one kind
         *  that has one is how the image reports anything at all.
         */
        if (OM_is_object(value)
         && (OM_fetch_class(value) == ST_CLASS_STRING
          || OM_fetch_class(value) == BOOT_global("Symbol"))) {
            uint32_t    n = OM_fetch_byte_length(value);
            uint32_t    k;

            for (k = 0; k < n; ++k)
                putchar((char) OM_fetch_byte(k, value));
            putchar('\n');
        }  else  {
            ST_print_object(value, text, sizeof text);
            printf("%s\n", text);
        }
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
    SCHED_timer_stop();
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
        SCHED_timer_stop();
    OM_shutdown();
        return 0;
    }
    if (!OM_is_object(p)) {
        printf("kind           : not a live object\n");
        SCHED_timer_stop();
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
        SCHED_timer_stop();
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
    SCHED_timer_stop();
    OM_shutdown();
    return 0;
}


/*
 *  Feed a survey from the command line: named files, and "-profile p" for
 *  everything a profile names.
 *
 *  A profile is the unit in which this system says what an image is made
 *  of, so it is the unit both of these reports want to be asked about.
 *  Answers 0 if a profile could not be read.
 */
static int
survey_arguments(st_survey *survey, int argc, char **argv)
{
    int     i;

    for (i = 0; i < argc; ++i) {
        st_names    expanded;
        int        *expanded_dialects = NULL;
        char        err[512];
        unsigned    k;

        if (strcmp(argv[i], "-profile") != 0 || i + 1 >= argc) {
            SURVEY_file(survey, argv[i]);
            continue;
        }
        if (!PROFILE_expand(argv[++i], &expanded, &expanded_dialects,
                            err, sizeof err)) {
            fprintf(stderr, "st80: %s\n", err);
            return 0;
        }
        for (k = 0; k < expanded.count; ++k)
            SURVEY_file(survey, expanded.items[k]);
        SRC_names_free(&expanded);
        free(expanded_dialects);
    }
    return 1;
}

static int
do_syntax(int argc, char **argv)
{
    st_survey   survey;

    SURVEY_init(&survey);
    if (!survey_arguments(&survey, argc, argv))
        return 1;
    SURVEY_report(&survey, stdout);
    return (survey.failed || survey.unreadable) ? 1 : 0;
}

/*
 *  Which primitives a body of source asks the VM for.
 *
 *  The port's finite question.  Source names primitives by number, the
 *  numbers are enumerable, and what is left after the ones this VM answers
 *  is the work -- so this converts "will Pharo's kernel run here" from a
 *  judgement into a list that gets shorter.
 *
 *  It exits 0 whatever it finds: an unimplemented primitive is the report's
 *  subject, not its failure.
 */
static int
do_primitives(int argc, char **argv)
{
    st_survey   survey;

    SURVEY_init(&survey);
    if (!survey_arguments(&survey, argc, argv))
        return 1;
    SURVEY_primitive_report(&survey, stdout);
    return survey.unreadable ? 1 : 0;
}


/*  ----------  A growable list of source paths  ----------  */

typedef struct {
    char      **items;
    unsigned    count;
    unsigned    capacity;
} path_list;

/*  The dialect each of those paths is written in, kept in step with it.  */
typedef struct {
    int        *items;
    unsigned    count;
    unsigned    capacity;
} dialect_list;

static int
dialect_list_add(dialect_list *l, int dialect)
{
    if (l->count == l->capacity) {
        unsigned    want = l->capacity ? l->capacity * 2 : 32;
        int        *grown = (int *) realloc(l->items, want * sizeof *grown);

        if (!grown)
            return 0;
        l->items    = grown;
        l->capacity = want;
    }
    l->items[l->count++] = dialect;
    return 1;
}

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
        if (!strcmp(argv[i], "-primitives") && i + 1 < argc)
            return do_primitives(argc - i - 1, argv + i + 1);
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
            int         run_tests  = 0;
            st_names    doctests;

            memset(&doctests, 0, sizeof doctests);
            const char *startup = NULL;
            int          j;
            int          status;
            dialect_list dialects;

            memset(&sources, 0, sizeof sources);
            memset(&dialects, 0, sizeof dialects);
            for (j = i + 1; j < argc; ++j) {
                if (!strcmp(argv[j], "-o") && j + 1 < argc) {
                    out_path = argv[++j];
                }  else if (!strcmp(argv[j], "-eval") && j + 1 < argc) {
                    expression = argv[++j];
                }  else if (!strcmp(argv[j], "-startup") && j + 1 < argc) {
                    startup = argv[++j];
                }  else if (!strcmp(argv[j], "-closures")) {
                    use_closures = 1;
                }  else if (!strcmp(argv[j], "-tests")) {
                    run_tests = 1;
                }  else if (!strcmp(argv[j], "-doctests") && j + 1 < argc) {
                    SRC_names_add(&doctests, argv[++j]);
                }  else if (!strcmp(argv[j], "-screenshot") && j + 1 < argc) {
                    shot_path = argv[++j];
                }  else if (!strcmp(argv[j], "-manifest") && j + 1 < argc) {
                    unsigned    before = sources.count;

                    if (!read_manifest(argv[++j], &sources)) {
                        path_list_free(&sources);
                        return 1;
                    }
                    while (dialects.count < sources.count) {
                        (void) before;
                        dialect_list_add(&dialects, ST_DIALECT_BLUE_BOOK);
                    }
                }  else if (!strcmp(argv[j], "-profile") && j + 1 < argc) {
                    st_names    expanded;
                    int        *expanded_dialects = NULL;
                    char        err[512];
                    unsigned    k;

                    if (!PROFILE_expand(argv[++j], &expanded,
                                        &expanded_dialects, err, sizeof err)) {
                        fprintf(stderr, "st80: %s\n", err);
                        path_list_free(&sources);
                        return 1;
                    }
                    for (k = 0; k < expanded.count; ++k) {
                        if (!path_list_add(&sources, expanded.items[k])
                         || !dialect_list_add(&dialects,
                                              expanded_dialects
                                                ? expanded_dialects[k]
                                                : ST_DIALECT_BLUE_BOOK)) {
                            fprintf(stderr, "st80: out of memory\n");
                            SRC_names_free(&expanded);
                            free(expanded_dialects);
                            path_list_free(&sources);
                            return 1;
                        }
                    }
                    SRC_names_free(&expanded);
                    free(expanded_dialects);
                }  else if (bootstrap_is_directory(argv[j])) {
                    /*
                     *  A bare directory means every source under it, which
                     *  is what Phase 5's exit criterion asks for:
                     *  `st80 -bootstrap sources/ -o st80.image'.  Before
                     *  this the path was handed to the reader as a file and
                     *  failed with "short read on sources/".
                     */
                    st_names    tree;
                    char        err[512];
                    unsigned    k;

                    memset(&tree, 0, sizeof tree);
                    if (!PROFILE_expand_tree(argv[j], &tree, err, sizeof err)) {
                        fprintf(stderr, "st80: %s\n", err);
                        SRC_names_free(&tree);
                        path_list_free(&sources);
                        return 1;
                    }
                    for (k = 0; k < tree.count; ++k) {
                        if (!path_list_add(&sources, tree.items[k])
                         || !dialect_list_add(&dialects,
                                use_closures ? ST_DIALECT_CLOSURES
                                             : ST_DIALECT_BLUE_BOOK)) {
                            fprintf(stderr, "st80: out of memory\n");
                            SRC_names_free(&tree);
                            path_list_free(&sources);
                            return 1;
                        }
                    }
                    SRC_names_free(&tree);
                }  else if (!path_list_add(&sources, argv[j])
                         || !dialect_list_add(&dialects,
                                use_closures ? ST_DIALECT_CLOSURES
                                             : ST_DIALECT_BLUE_BOOK)) {
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
                                  dialects.items, sources.count, out_path,
                                  expression, startup, run_tests,
                                  &doctests);
            path_list_free(&sources);
            free(dialects.items);
            return status;
        }
        if (!strcmp(argv[i], "-screenshot") && i + 1 < argc) {
            shot_path = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "-wiggle")) {
            wiggle = 1;
            continue;
        }
        if (!strcmp(argv[i], "-inject") && i + 1 < argc) {
            inject_script = argv[++i];
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
        if (!strcmp(argv[i], "-disasm") && i + 3 < argc)
            return do_disasm(argv[i + 1], argv[i + 2], argv[i + 3]);
    }
    print_version();
    return 0;
}

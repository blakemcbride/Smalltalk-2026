/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The 1983 class library, bootstrapped and running.
 *
 *  test_library.c proves the library PARSES.  This proves it runs: 226
 *  classes and 4517 methods are compiled into a live object memory and then
 *  asked to compute, with the answers checked.  Nothing of Xerox's is in the
 *  result -- the sources are the MIT ones in sources/ -- and nothing of ours
 *  is either beyond the VM: 3 factorial is answered by Number>>factorial as
 *  Xerox wrote it, not by anything in kernel/Kernel.st.
 *
 *  This is the prerequisite Phase 8 was waiting on.  MVC is 30-odd classes
 *  sitting on top of Collection, Stream, Rectangle and Form, and none of it
 *  could be attempted while the image held only the 36 kernel classes.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "compiler.h"
#include "bootstrap.h"
#include "gfx.h"
#include "st_sched.h"

#include <stdio.h>
#include <string.h>

#define MANIFEST    "sources/MANIFEST"
#define MAX_SOURCES 512

static char     paths[MAX_SOURCES][256];
static unsigned path_count;
static int      built;

static int
load_manifest(void)
{
    FILE   *f = fopen(MANIFEST, "r");
    char    line[256];

    if (!f)
        return 0;
    while (path_count < MAX_SOURCES && fgets(line, sizeof line, f)) {
        size_t  n = strlen(line);

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n)
            snprintf(paths[path_count++], sizeof paths[0], "%s", line);
    }
    fclose(f);
    return path_count > 0;
}

static int
build_once(void)
{
    const char         *list[MAX_SOURCES];
    st_bootstrap_result res;
    unsigned            i;

    for (i = 0; i < path_count; ++i)
        list[i] = paths[i];
    if (BOOT_build(list, path_count, &res) != 0) {
        printf("  bootstrap failed: %s\n", res.error);
        return 0;
    }
    printf("  %u classes, %u methods, %u symbols\n", res.classes_created,
           res.methods_compiled, res.symbols_interned);

    CHECK_EQ_INT(res.classes_created, 226);
    /*  4517 from the MIT sources, plus the few in kernel/Bootstrap.st.  */
    CHECK_EQ_INT(res.methods_compiled, 4521);
    built = 1;
    return 1;
}

/*
 *  Evaluate as the driver does: compile the expression as a method body,
 *  stand up a context whose sender is nil, and run.
 */
static st_oop
evaluate(const char *expression)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                source[1024];
    st_oop              context;

    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_array         = BOOT_make_array;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;

    /*
     *  Collect first.  Every doIt here is unreachable the moment it finishes,
     *  and an expression that runs away allocating would otherwise exhaust
     *  the table and make every LATER expression fail as "out of memory" --
     *  turning one broken thing into twenty misleading ones.
     */
    OM_collect();

    /*
     *  An expression containing a caret is already a method body, temporary
     *  declarations and all, so it is used as written -- the same rule the
     *  driver uses.  Wrapping it in "doIt ^" instead produces "doIt ^| f |",
     *  which is not a sentence in any dialect.
     */
    if (strchr(expression, '^'))
        snprintf(source, sizeof source, "doIt %s", expression);
    else
        snprintf(source, sizeof source, "doIt ^%s", expression);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        printf("  cannot compile \"%s\": %s\n", expression, res.error);
        return ST_OOP_INVALID;
    }
    context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 64);
    if (!OM_is_present(context))
        return ST_OOP_INVALID;
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, res.method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int)
                        (BOOT_method_initial_ip(res.method) + 1)));
    /*  The stack begins above the temporaries, not at zero.  */
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, res.method))));

    memset(&st_vm, 0, sizeof st_vm);
    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    ST_interp_run(20000000);
    if (st_vm.running) {
        printf("  \"%s\" did not finish\n", expression);
        return ST_OOP_INVALID;
    }
    return st_vm.return_value;
}

static void
check_integer(const char *expression, st_int want)
{
    st_oop  value = evaluate(expression);

    ++st_test_checks;
    if (!OM_is_int(value) || OM_int_value(value) != want) {
        char    text[128];

        ++st_test_failures;
        ST_print_object(value, text, sizeof text);
        printf("  FAIL %s: got %s, want %lld\n", expression, text,
               (long long) want);
    }
}

static void
check_oop(const char *expression, st_oop want, const char *label)
{
    st_oop  value = evaluate(expression);

    ++st_test_checks;
    if (value != want) {
        char    text[128];

        ++st_test_failures;
        ST_print_object(value, text, sizeof text);
        printf("  FAIL %s: got %s, want %s\n", expression, text, label);
    }
}

static void
check_class(const char *expression, const char *class_name)
{
    st_oop  value = evaluate(expression);

    ++st_test_checks;
    if (!OM_is_present(value)
     || OM_fetch_class(value) != BOOT_global(class_name)) {
        char    text[128];

        ++st_test_failures;
        ST_print_object(value, text, sizeof text);
        printf("  FAIL %s: got %s, want a %s\n", expression, text, class_name);
    }
}

/*
 *  The classes the library is expected to bring, which the 36-class kernel
 *  never had.  MVC needs all of these.
 */
static void
test_classes_present(void)
{
    static const char *const expected[] = {
        "OrderedCollection", "Dictionary", "Set", "Bag", "SortedCollection",
        "Interval", "Symbol", "Stream", "ReadStream", "WriteStream",
        "Rectangle", "Form", "BitBlt", "Pen", "Fraction", "Date", "Time",
        "StandardSystemView", "StandardSystemController", "Browser",
        "Paragraph", "TextStyle", "DisplayScreen", "Debugger", "Inspector"
    };
    unsigned    i;

    for (i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
        st_oop  cls = BOOT_global(expected[i]);

        ++st_test_checks;
        if (!OM_is_present(cls)) {
            ++st_test_failures;
            printf("  FAIL missing class %s\n", expected[i]);
        }
    }
}

/*
 *  Arithmetic answered by the 1983 methods rather than by ours.  factorial,
 *  gcd: and max: are Xerox's code in Number and Integer; nothing in this
 *  project implements them.
 */
static void
test_arithmetic(void)
{
    check_integer("3 + 4", 7);
    check_integer("6 * 7", 42);
    check_integer("3 factorial", 6);
    check_integer("6 factorial", 720);
    check_integer("100 gcd: 75", 25);
    check_integer("3 max: 9", 9);
    check_integer("3 min: 9", 3);
    check_integer("-7 abs", 7);
    check_integer("17 \\\\ 5", 2);
    check_integer("2 raisedTo: 10", 1024);
    check_integer("(1 to: 10) inject: 0 into: [:a :b | a + b]", 55);
    /*  Fractions: exact arithmetic through Fraction>>+ and reduction.  */
    check_integer("(3/4) + (1/4)", 1);
    check_integer("((2/3) + (1/6)) numerator", 5);
    check_integer("((2/3) + (1/6)) denominator", 6);
}

/*
 *  Collections, which is where the library really starts.  Every one of
 *  these runs hundreds of bytecodes through Xerox's own code.
 */
static void
test_collections(void)
{
    check_integer("#(1 2 3) size", 3);
    check_integer("'hello' size", 5);
    check_integer("(OrderedCollection new add: 1; add: 2; yourself) size", 2);
    check_integer("(Array with: 1 with: 2 with: 3) size", 3);
    check_integer("((1 to: 5) collect: [:i | i * i]) last", 25);
    check_integer("((1 to: 10) select: [:i | i even]) size", 5);
    check_integer("((1 to: 10) detect: [:i | i > 7]) ", 8);
    check_integer("(Set new add: 3; add: 3; yourself) size", 1);
    check_integer("('hello' indexOf: $l)", 3);
    check_integer("(#(3 1 2) asSortedCollection) first", 1);

    check_class("Dictionary new", "Dictionary");
    /*  printString on the structured collections, which needs Stream,
     *  Symbol and Character all working together.  */
    check_integer("(Set new add: 3; yourself) printString size", 8);
    check_integer("Object new printString size", 9);
    check_integer("(Dictionary new at: 1 put: 2; yourself) printString size",
                  18);
    check_class("(WriteStream on: String new)", "WriteStream");
    check_class("3/4", "Fraction");
}

/*
 *  printString is the deepest thing here: it runs Stream, WriteStream,
 *  String, Character and Symbol together, and it needed the class variables
 *  that BOOT_run_initializers now sets, plus primitives 63 and 64.
 */
static void
test_printing(void)
{
    check_integer("42 printString size", 2);
    check_integer("$A printString size", 2);
    check_integer("'hello' printString size", 7);       /*  with the quotes */
    check_integer("#(1 2 3) printString size", 8);      /*  "(1 2 3 )"      */
    check_integer("3 printString first asciiValue", 51);/*  $3              */
}

/*
 *  Symbols, which are what made this hard: interning consults a table that
 *  Symbol class>>initialize builds by interning, so a new image cannot
 *  bootstrap it from inside.  BOOT_run_initializers seeds it from C and then
 *  places every entry with the image's OWN intern:, so the hash is the
 *  library's and identity survives.
 */
static void
test_symbols(void)
{
    check_integer("#foo size", 3);
    check_integer("'hello' asSymbol size", 5);
    check_oop("#foo = 'foo'", ST_FALSE, "false");   /*  Symbol>>= is identity */
    check_oop("'foo' = #foo", ST_TRUE,  "true");    /*  String>>= is by value */

    /*
     *  Identity is the whole point of a Symbol, and it holds three ways: two
     *  lookups of the same text, a lookup against a symbol the COMPILER made
     *  while building the library, and a lookup against one made after the
     *  table existed.  The third is why BOOT_intern_symbol places into the
     *  table rather than only seeding it once.
     */
    check_oop("'hello' asSymbol == 'hello' asSymbol", ST_TRUE, "true");
    check_oop("#printString == 'printString' asSymbol", ST_TRUE, "true");
    check_oop("#at:put: == 'at:put:' asSymbol", ST_TRUE, "true");
    check_oop("#zzz == 'zzz' asSymbol", ST_TRUE, "true");

    /*
     *  A long selector is still one Symbol.  The intern table compared
     *  against a 64-byte C copy of the text, so anything past 63 characters
     *  never matched itself and every mention made a new Symbol -- the
     *  method installed under one and sent with another, and a dictionary
     *  keyed by identity could not find it.  Fourteen selectors in the
     *  library are that long.
     */
    check_integer("'subclass:instanceVariableNames:classVariableNames:"
                  "poolDictionaries:category:' asSymbol size", 76);
    check_oop("'setDestForm:sourceForm:halftoneForm:combinationRule:"
              "destOrigin:sourceOrigin:extent:clipRect:' asSymbol"
              " == #setDestForm:sourceForm:halftoneForm:combinationRule:"
              "destOrigin:sourceOrigin:extent:clipRect:", ST_TRUE, "true");
}

/*
 *  Floats are IEEE single precision in two words, most significant first --
 *  Chapter 30, and what the interpreter emits for every computed result.
 *  The bootstrap used to store the host's double in native word order, so a
 *  literal and a computed value of the same number were neither the same
 *  shape nor the same bits: 3.5 exponent answered -1060.
 */
static void
test_floats(void)
{
    check_integer("3.5 truncated", 3);
    check_integer("3.5 rounded", 4);
    check_integer("3.5 exponent", 1);
    check_integer("(3.5 + 1.5) truncated", 5);
    check_integer("(3.5 * 2) truncated", 7);
    check_integer("7 asFloat truncated", 7);
    check_oop("3.5 < 4.0", ST_TRUE, "true");
}

/*
 *  BOOT_string_hash duplicates String>>hash in C, to place symbols in the
 *  library's table without interpreting a send per symbol.  A duplicate that
 *  is merely believed is a bug with a long fuse, so it is checked here
 *  against the image's own answer, for strings that reach every branch of
 *  the formula: empty, one character, two, and longer.
 */
static void
test_string_hash_agrees(void)
{
    static const char *const samples[] = {
        "", "a", "z", "ab", "foo", "hello", "printString",
        "at:put:", "instanceVariableNames:", "x"
    };
    unsigned    i;

    for (i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        char    expression[128];
        st_oop  from_image;
        st_oop  interned = BOOT_intern_symbol(samples[i], NULL);
        uint32_t from_c  = BOOT_string_hash(interned);

        snprintf(expression, sizeof expression, "'%s' hash", samples[i]);
        from_image = evaluate(expression);
        /*  Recomputed after evaluate, which collects.  */
        from_c = BOOT_string_hash(interned);

        ++st_test_checks;
        if (!OM_is_int(from_image)
         || (uint32_t) OM_int_value(from_image) != from_c) {
            char    text[128];

            ++st_test_failures;
            ST_print_object(from_image, text, sizeof text);
            printf("  FAIL hash of \"%s\": C says %u, the image says %s\n",
                   samples[i], from_c, text);
        }
    }
}

static void
test_strings(void)
{
    /*  Unary binds tighter than binary, so the parentheses are required.  */
    check_integer("('ab' , 'cd') size", 4);
    check_integer("('hello' copyFrom: 2 to: 4) size", 3);
    check_integer("('hello' occurrencesOf: $l)", 2);
    check_integer("('hello' reverse) first asciiValue", 111);   /*  $o  */
    check_integer("((String new: 2) at: 1 put: $z) asInteger", 122);
}

static void
test_graphics_objects(void)
{
    /*  The MVC substrate: points and rectangles doing real geometry.  */
    check_class("3 @ 4", "Point");
    check_integer("(3 @ 4) x", 3);
    check_integer("((3 @ 4) + (1 @ 1)) x", 4);
    check_class("0 @ 0 corner: 10 @ 10", "Rectangle");
    check_integer("(0 @ 0 corner: 10 @ 20) width", 10);
    check_integer("(0 @ 0 corner: 10 @ 20) height", 20);
    check_integer("(0 @ 0 corner: 10 @ 20) area", 200);
    check_integer("((0 @ 0 corner: 10 @ 10) intersect: (5 @ 5 corner: 20 @ 20))"
                  " area", 25);
}

/*
 *  BitBlt, through the library's own Form and BitBlt classes.
 *
 *  Filling a form is the case that is exactly as wide as its raster, and
 *  Chapter 18's word count is off by one there unless the test is "<=".  With
 *  "<" a 16-pixel blit at x = 0 claims two words of a one-word row: it wrote
 *  a whole extra word per row, masked to all ones, and read past the end of
 *  the bitmap.  It survived the Xerox image because a form there is nearly
 *  always wider than the blit.
 */
static void
test_bitblt(void)
{
    /*  A fresh form is white.  */
    check_integer("(Form extent: 16 @ 16) bits inject: 0 into: [:a :b | a + b]",
                  0);

    /*  Filled black, every one of the sixteen words is all ones.  */
    check_integer("| f | f := Form extent: 16 @ 16."
                  " f fill: f boundingBox rule: 3 mask: Form black."
                  " ^f bits first", 65535);
    check_integer("| f | f := Form extent: 16 @ 16."
                  " f fill: f boundingBox rule: 3 mask: Form black."
                  " ^f bits inject: 0 into: [:a :b | a + b]", 16 * 65535);

    /*  Two words wide, eight rows: the same total by a different shape.  */
    check_integer("| f | f := Form extent: 32 @ 8."
                  " f fill: f boundingBox rule: 3 mask: Form black."
                  " ^f bits inject: 0 into: [:a :b | a + b]", 16 * 65535);

    /*  And the mask the library hands out really is black.  */
    check_integer("Form black bits inject: 0 into: [:a :b | a + b]",
                  16 * 65535);
}

/*  How many pixels of the Display are set.  */
static long
display_ink(void)
{
    gfx_form    form;
    int         x;
    int         y;
    long        ink = 0;

    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return -1;
    for (y = 0; y < form.height; ++y) {
        for (x = 0; x < form.width; ++x) {
            uint16_t    word = form.bits[(size_t) y * form.raster + (x >> 4)];

            ink += (word >> (15 - (x & 15))) & 1;
        }
    }
    return ink;
}

static void
check_ink(const char *expression, long want)
{
    long    got;

    evaluate(expression);
    got = display_ink();
    ++st_test_checks;
    if (got != want) {
        ++st_test_failures;
        printf("  FAIL %s: %ld pixels of ink, want %ld\n", expression, got,
               want);
    }
}

/*
 *  The image drawing on a screen of its own.
 *
 *  A 1983 image inherits its Display from the image it was built from; this
 *  one is given a first by BOOT_install_display, and then draws on it with
 *  Xerox's own Form and BitBlt code through primitive 96.  The counts are
 *  exact because the areas are: a fill covers precisely the rectangle asked
 *  for, and gray covers half of it.
 */
static void
test_display(void)
{
    check_integer("Display width", 640);
    check_integer("Display height", 480);
    check_integer("Display bits size", 40 * 480);

    /*  Nothing drawn yet.  */
    CHECK_EQ_INT(display_ink(), 0);

    /*  160 x 80 = 12800.  */
    check_ink("Display fill: (40@40 corner: 200@120) rule: 3 mask: Form black."
              " ^Display width", 12800);

    /*  A white rectangle knocked out of it: 120 x 40 = 4800 fewer.  */
    check_ink("Display fill: (60@60 corner: 180@100) rule: 3 mask: Form white."
              " ^Display width", 12800 - 4800);

    /*  Gray is a halftone, so it covers half of its 160 x 80.  */
    check_ink("Display fill: (240@40 corner: 400@120) rule: 3 mask: Form gray."
              " ^Display width", 12800 - 4800 + 6400);

    /*  And back to white.  */
    check_ink("Display white. ^Display width", 0);
}

/*
 *  Text, which needed a font: the 1983 sources are code and carry none, so
 *  the image is given one written for this project.  Rendering a line runs
 *  the strike -- one Form holding every glyph, with an xTable saying where
 *  each begins -- through BitBlt, once per character, at whatever x the
 *  previous character left off.  Most of those are not word aligned, which
 *  is the case that needs each bitmap access bounds checked rather than the
 *  whole blit refused: an eight-pixel span starting four bits before a word
 *  boundary needs two destination words and so reads two source words from a
 *  form only one word wide.
 */
static void
test_text(void)
{
    check_integer("DefaultFont ascent", 7);
    check_integer("DefaultFont glyphs width", 128 * 8);
    check_integer("DefaultFont glyphs height", 8);
    check_integer("DefaultFont widthOf: $A", 8);
    check_integer("DefaultFont widthOf: $i", 8);
    check_integer("(DefaultFont characterForm: $A) width", 8);

    /*  The glyph really is the letter A: these are its eight rows.  */
    check_integer("(DefaultFont characterForm: $A) bits"
                  " inject: 0 into: [:a :b | a + b]",
                  0x1800 + 0x3C00 + 0x6600 + 0xC300 + 0xFF00 + 0xC300
                  + 0xC300);

    /*  An 8x8 black square lands whole wherever it is put, aligned or not. */
    check_ink("| f | Display white. f := Form extent: 8@8."
              " f fill: f boundingBox rule: 3 mask: Form black."
              " f displayOn: Display at: 20@10"
              " clippingBox: Display boundingBox rule: 3 mask: Form black."
              " ^1", 64);
    check_ink("| f | Display white. f := Form extent: 8@8."
              " f fill: f boundingBox rule: 3 mask: Form black."
              " f displayOn: Display at: 29@10"
              " clippingBox: Display boundingBox rule: 3 mask: Form black."
              " ^1", 64);

    /*  And a line of text draws every character of it.  */
    check_ink("Display white."
              " DefaultFont characters: (1 to: 12) in: 'Smalltalk-80'"
              " displayAt: 20@20 clippedBy: Display boundingBox rule: 3"
              " mask: Form black. ^1", 291);
    check_ink("Display white. ^1", 0);
}

/*
 *  Composing text: Paragraph, the CompositionScanner and the font together.
 *
 *  This is what a view displays, so it is the last thing under MVC rather
 *  than part of it.  The width of a line is the sum of the widths the font
 *  reports, which for this face is eight a character -- so the numbers here
 *  are the string lengths, and they are exact.
 */
static void
test_paragraph(void)
{
    check_integer("(Paragraph withText: 'hello' asText) numberOfLines", 1);
    check_integer("(Paragraph withText: 'hello' asText) width", 5 * 8);
    check_integer("('hello world' asParagraph) width", 11 * 8);
    /*  Empty text composes to no lines at all, not to one empty one.  */
    check_integer("('' asParagraph) numberOfLines", 0);

    /*  And it draws: 'Hi' is two glyphs of known ink.  */
    check_ink("Display white. 'Hi' asParagraph displayOn: Display at: 20@20."
              " ^1", 50);   /*  the ink of H and i in this face  */
}

/*
 *  A view on the screen.
 *
 *  StandardSystemView is the window everything in MVC lives in: a labelled
 *  tab above a body, drawn by the library's own View code through the
 *  transformation, the border and the text it just learned to compose.  This
 *  is Phase 8's substrate -- what a Browser or an Inspector puts itself in.
 */
static void
test_view(void)
{
    check_integer("| v | v := StandardSystemView new. v label: 'Hello'."
                  " ^v label size", 5);
    check_integer("| v | v := StandardSystemView new."
                  " v window: (60@40 corner: 360@200). ^v window width", 300);

    /*
     *  Displayed, it covers its window: a gray body and a labelled tab.  The
     *  count is stable because the dither is, and it is far more than the
     *  border alone -- a view that failed to fill would be obvious here.
     */
    check_ink("| v | Display white. v := StandardSystemView new."
              " v label: 'Hello World'. v window: (60@40 corner: 360@200)."
              " v display. ^1", 12512);
}

/*
 *  The window scheduler.
 *
 *  ScheduledControllers is a ControlManager, made when an image is built and
 *  carried by every snapshot after; Sensor likewise.  Neither exists in an
 *  image built from sources, and without them a great deal of the interface
 *  asks nil for the active controller or the cursor and stops there.
 *
 *  Object class>>initialize cannot be run to get DependentsFields, because it
 *  asks the user to confirm resetting every dependency in the system and
 *  there is nobody to ask; its two halves are called directly instead.
 *  Without it addDependent: sends at:ifAbsent: to nil and no view can
 *  register interest in a model.
 */
static void
test_scheduler(void)
{
    ++st_test_checks;
    if (!OM_is_present(BOOT_global("Sensor"))) {
        ++st_test_failures;
        printf("  FAIL Sensor was not installed\n");
    }
    ++st_test_checks;
    if (!OM_is_present(BOOT_global("ScheduledControllers"))) {
        ++st_test_failures;
        printf("  FAIL ScheduledControllers was not installed\n");
    }

    /*  It starts holding the screen controller and nothing else.  */
    check_integer("ScheduledControllers scheduledControllers size", 1);

    /*
     *  Two windows scheduled and the screen restored: the manager redraws
     *  its gray background and both views over it.  Far more ink than the
     *  views alone, because the background is most of the screen.
     */
    check_ink("| a b | Display white."
              " a := StandardSystemView new. a label: 'Transcript'."
              " a window: (30@30 corner: 300@180)."
              " b := StandardSystemView new. b label: 'Workspace'."
              " b window: (200@120 corner: 560@340)."
              " ScheduledControllers schedulePassive: a controller."
              " ScheduledControllers schedulePassive: b controller."
              " ScheduledControllers restore. ^1", 124794);
    check_integer("ScheduledControllers scheduledControllers size", 3);
}

/*
 *  The scheduler, and the process a saved image resumes into.
 *
 *  ProcessorScheduler class>>new refuses on purpose -- "the integrity of the
 *  system depends on a unique scheduler" -- because in 1983 the one scheduler
 *  was made when the image was built and carried by every snapshot after.
 *  An image built from sources has to be given one, along with a process to
 *  wake up in, or there is nothing for -run to resume.
 */
static void
test_process_scheduler(void)
{
    CHECK(BOOT_install_scheduler("^Display width"));

    ++st_test_checks;
    if (!OM_is_present(BOOT_global("Processor"))) {
        ++st_test_failures;
        printf("  FAIL Processor was not installed\n");
    }
    check_oop("Processor activeProcess isNil", ST_FALSE, "false");
    check_integer("Processor activePriority", 4);

    /*  Eight priorities, each with a list of its own.  */
    check_integer("(Processor instVarAt: 1) size", 8);

    /*
     *  And the VM has been handed the semaphore to signal when input
     *  arrives.  InputSensor class>>install is what installs it, by way of
     *  primitive 93, and it can only run once there is a Processor with an
     *  active process to take a priority from -- the process it forks asks
     *  for Processor activePriority.  Run any earlier and the method stops
     *  before its last line with no complaint anyone would notice, and the
     *  image is left with no way to be told about a key or a mouse button:
     *  the events queue up and the semaphore they signal is nobody\'s.
     *
     *  So this is asserted here, immediately after the scheduler is built,
     *  because that ordering is the whole of what makes it work.
     */
    ++st_test_checks;
    if (!OM_is_present(SCHED_input_semaphore())) {
        ++st_test_failures;
        printf("  FAIL no input semaphore was installed\n");
    }

    /*
     *  The image is built with its system processes already running.
     *
     *  Two of the class initializers fork one.  Delay class>>initialize
     *  forks the timing process at Processor timingPriority, which is 8, and
     *  InputSensor class>>install forks the process that drains the event
     *  queue at lowIOPriority, which is 6.  Both sit on their run queues
     *  waiting on a semaphore, which is where a 1983 image keeps them.
     *
     *  Neither used to be there.  Both initializers ask Processor for a
     *  priority, and the scheduler was built after the initializers ran, so
     *  each stopped at that line -- Delay leaving an image in which every
     *  wait would have been forever.  Asserting the queues is the only way
     *  to see it, because a process that was never forked is not missing
     *  from anywhere you would think to look.
     *
     *  Asserted BEFORE the yields below, and that ordering is the point: a
     *  yield runs the highest-priority process that is ready, so the first
     *  two yields take these two off their queues and leave them waiting on
     *  their semaphores instead.  Both states are correct; only the first
     *  says anything about whether the processes were forked at all.
     */
    check_oop("^((Processor instVarAt: 1) at: 8) isEmpty", ST_FALSE, "false");
    check_oop("^((Processor instVarAt: 1) at: 6) isEmpty", ST_FALSE, "false");

    /*
     *  Yielding, which is the smallest thing that needs two processes.
     *
     *  ProcessorScheduler>>yield forks a process to signal a semaphore and
     *  waits on it, so it exercises the whole handoff: the forked process is
     *  queued, control transfers to it, it signals, the waiting process is
     *  taken off the semaphore and resumed, and control comes back.  Each of
     *  those steps moves a process from a place that refers to it to one
     *  that does not yet, and every one of them was, at some point, the
     *  moment the process was reclaimed and the system reported that every
     *  process was blocked.
     */
    check_oop("Processor yield. ^true", ST_TRUE, "true");
    check_integer("Processor yield. ^3 + 4", 7);
}

/*
 *  SystemOrganization: the map from class categories to classes.
 *
 *  The Browser opens on it -- BrowserView openOn: SystemOrganization -- so
 *  without it there is nothing to browse.  Every class definition names its
 *  category, so the information has been going past all along; it is
 *  collected and handed to the library's own organizer.
 */
static void
test_system_organization(void)
{
    check_oop("SystemOrganization isNil", ST_FALSE, "false");
    /*  One per source directory.  */
    check_integer("SystemOrganization categories size", 41);
}

/*
 *  Processes, and a controller that wants control.
 *
 *  These are what the interaction loop is made of.  A forked process really
 *  runs -- the array it writes proves the scheduler switched to it and back
 *  -- and a controller under the cursor says so, which is what
 *  searchForActiveController waits for.
 *
 *  What cannot be asserted here is the loop itself, and deliberately: when a
 *  controller does want control, searchForActiveController gives it and the
 *  controller runs ITS loop, which does not return.  That is correct MVC and
 *  it is also the reason a test cannot wait for it.
 */
static void
test_processes(void)
{
    check_oop("Semaphore new isNil", ST_FALSE, "false");
    check_oop("([1] newProcess) isNil", ST_FALSE, "false");
    check_integer("| p | p := [1] newProcess. p priority: 6. ^p priority", 6);
    check_integer("Processor lowIOPriority", 6);

    /*
     *  Forking and yielding is NOT asserted here, though it works: this
     *  harness stands a context up and interprets it directly rather than
     *  going through the scheduler, so a yield switches away from a process
     *  the harness still believes it is running.  What that would test is the
     *  harness.  The behaviour is shown instead by -eval, where the same
     *  expression answers 99, and by the booted image, whose input process is
     *  forked exactly this way.
     */

    /*  The Sensor answers, so the controller layer has something to ask.  */
    check_class("Sensor cursorPoint", "Point");
    check_oop("Sensor anyButtonPressed", ST_FALSE, "false");

    /*  And a controller under the cursor wants control.  */
    check_oop("| v | v := StandardSystemView new."
              " v window: (0@0 corner: 640@480)."
              " ^v controller isControlWanted", ST_TRUE, "true");
    check_oop("| v | v := StandardSystemView new."
              " v window: (500@400 corner: 600@450)."
              " ^v controller isControlWanted", ST_FALSE, "false");
}

/*
 *  A System Browser.
 *
 *  Built the way BrowserView class>>openOn: builds one, but with the window
 *  set rather than swept out: "open" calls "view resize", which in 1983 asks
 *  the user to drag a rectangle, and there is nobody here to drag one.
 *  Everything else is the library's -- the Browser model on
 *  SystemOrganization, the five list views, the code view -- and what it
 *  draws is the categories this image was built from.
 */
static void
test_browser(void)
{
    check_ink("| b top | Display white."
              " b := Browser new on: SystemOrganization."
              " top := BrowserView model: b label: 'System Browser'"
              " minimumSize: 400@250."
              " top addCategoryView: (0@0 extent: 0.25@0.35) on: b"
              " readOnly: false."
              " top addClassView: (0.25@0 extent: 0.25@0.3) on: b"
              " readOnly: false."
              " top addMetaView: (0.25@0.3 extent: 0.25@0.05) on: b"
              " readOnly: false."
              " top addProtocolView: (0.5@0 extent: 0.25@0.35) on: b"
              " readOnly: false."
              " top addSelectorView: (0.75@0 extent: 0.25@0.35) on: b"
              " readOnly: false."
              " top addTextView: (0@0.35 extent: 1.0@0.65) on: b"
              " initialSelection: nil."
              " top window: (20@20 corner: 620@460). top display. ^1",
              /*
               *  It used to be 56618, and that number was the bug.
               *
               *  Displaying the text view reached CharacterBlock class>>
               *  stringIndex:character:boundingRectangle:, which sends a
               *  method the 1983 sources never define -- searching all of
               *  sources/ for BoundingRectangle: finds the send and nothing
               *  else.  The display died there, part drawn, and the count
               *  recorded whatever had been painted before it stopped.
               *
               *  kernel/Bootstrap.st supplies the method, so the browser now
               *  draws to completion: a title tab, five list panes with the
               *  category list filled, and an empty text pane because no
               *  method is selected yet.  Less ink, and all of it wanted.
               */
              15035);
}

/*
 *  Browsing: category, class, protocol, selector, source.
 *
 *  Every pane of the Browser answers, and the last one answers real source
 *  text.  Smalltalk-80 does not keep source in the image -- a CompiledMethod
 *  carries a position into a sources file and the Browser reads the chunk
 *  there -- so the bootstrap writes every method's source into one String
 *  and hands it over as SourceFiles.  Nothing says that stream has to be a
 *  file: RemoteString asks it only to position: and nextChunk.
 */
static void
test_browsing(void)
{
    check_integer("(Browser new on: SystemOrganization) categoryList size", 41);

    /*  Kernel-Objects holds Boolean, False, Object, True, UndefinedObject.  */
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. ^b classList size", 5);

    /*  Boolean's protocols, and the selectors in the first of them.  */
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. b className: #Boolean."
                  " ^b protocolList size", 4);
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. b className: #Boolean."
                  " b protocol: (b protocolList at: 1)."
                  " ^b selectorList size", 6);

    /*  And the source of a method, read back out of SourceFiles.  */
    check_integer("(Boolean sourceCodeAt: #not) size", 122);
    check_integer("((Boolean sourceCodeAt: #not) asText"
                  " makeSelectorBoldIn: Boolean) size", 122);
    check_integer("(SourceFiles at: 1) contents size", 1203447);
}

/*
 *  Editing, compiling, inspecting and debugging -- inside the image.
 *
 *  This is the rest of Phase 8's exit criterion, and none of it is our code:
 *  the Compiler, the Inspector and the Debugger are the library's, running on
 *  an image bootstrapped from source.  A method compiled here is installed in
 *  a real method dictionary and answers when sent.
 */
static void
test_compile_inspect_debug(void)
{
    /*  The image compiles a method into a class, and it runs.  */
    check_integer("Object compile: 'answerFortyTwo ^42' classified: 'testing'"
                  " notifying: nil. ^3 answerFortyTwo", 42);
    check_integer("Object compile: 'twice: n ^n * 2' classified: 'testing'"
                  " notifying: nil. ^(3 twice: 21)", 42);

    /*  It is a real method: the Browser can find it and read it back.  */
    check_integer("Object compile: 'answerFortyTwo ^42' classified: 'testing'"
                  " notifying: nil."
                  " ^(Object sourceCodeAt: #answerFortyTwo) size", 18);

    /*  The Inspector opens on an object and lists its fields.  */
    check_integer("(Inspector new inspect: 3@4) fieldList size", 3);
    check_class("Inspector inspect: 3@4", "Inspector");

    /*  The Debugger opens on a context and lists the stack.  */
    check_integer("(Debugger context: thisContext) contextList size", 1);
    check_class("Debugger context: thisContext", "Debugger");
}

/*
 *  Every class the image defines is reachable by name from Smalltalk.
 *
 *  A binding is made the first time a name is needed, and for the first
 *  seventeen of them that is before the class named Association exists --
 *  Association being the seventeenth.  Those carried a nil class, and the
 *  one thing in the system that asks a binding what class it is happens to
 *  be Dictionary>>add:, which sends #key.  So the system dictionary refused
 *  exactly those seventeen, and "Smalltalk includesKey: #Array" answered
 *  false in a system where Array worked perfectly well -- a compiled method
 *  holds the binding and reads its value, and never asks its class.
 *
 *  The names below are chosen from the refused seventeen and from after
 *  them, because what makes this worth a test is that both must hold.
 */
static void
test_globals_are_reachable_by_name(void)
{
    /*  Refused when their bindings had no class.  */
    check_oop("^Smalltalk includesKey: #Array", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Association", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #OrderedCollection", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Stream", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Interval", ST_TRUE, "true");

    /*  Defined after Association, so never affected.  */
    check_oop("^Smalltalk includesKey: #Point", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Object", ST_TRUE, "true");

    /*  And the lookup answers the class itself, not merely a binding.  */
    check_oop("^(Smalltalk at: #Array) == Array", ST_TRUE, "true");
    check_oop("^(Smalltalk at: #OrderedCollection) == OrderedCollection",
              ST_TRUE, "true");

    /*
     *  Nothing is missing.  Every class the bootstrap built is in the
     *  dictionary; a count that falls short means bindings were refused
     *  again, which is precisely how this went unnoticed before.
     */
    check_oop("^Smalltalk size >= 310", ST_TRUE, "true");
}

/*
 *  Input, through the path a window's events take.
 *
 *  GFX_inject_* does exactly what the SDL handlers do -- move the pointer,
 *  set the button state, queue the event words, signal the input semaphore.
 *  Driving a private queue instead would prove nothing about the one the
 *  image reads; this is the same one.
 *
 *  With it the interactive half can be tested without a person in front of
 *  it: the Sensor answers where the pointer is, and a controller says whether
 *  it wants control -- which is the question searchForActiveController spends
 *  its whole life asking.
 */
static void
test_input(void)
{
    /*  The Sensor reports where the pointer was put.  */
    GFX_inject_mouse(100, 80);
    check_integer("Sensor cursorPoint x", 100);
    check_integer("Sensor cursorPoint y", 80);

    GFX_inject_mouse(300, 240);
    check_integer("Sensor cursorPoint x", 300);
    check_integer("Sensor cursorPoint y", 240);

    /*
     *  Buttons take the longer way round, and it is worth saying which.
     *
     *  The pointer's position is polled straight from the VM by primitive
     *  90, so it answers whatever was last injected.  The button state is
     *  not polled: it is kept by InputState and updated by the input PROCESS
     *  as it drains the event queue, which it does when the semaphore
     *  primitive 93 installed is signalled.  So nothing here is true until a
     *  process other than this one has run, and the yield is what lets it.
     *
     *  Codes 128, 129 and 130 are the blue, yellow and red buttons, and
     *  InputState keeps them as bits 1, 2 and 4 in that order.
     */
    GFX_inject_button(130, 1);
    check_integer("Processor yield. ^Sensor buttons", 4);
    check_oop("^Sensor anyButtonPressed", ST_TRUE, "true");
    check_oop("^Sensor redButtonPressed", ST_TRUE, "true");

    GFX_inject_button(130, 0);
    check_integer("Processor yield. ^Sensor buttons", 0);
    check_oop("^Sensor noButtonPressed", ST_TRUE, "true");

    /*  And the keyboard, which arrives on the same queue.  */
    GFX_inject_key('A', 1);
    GFX_inject_key('A', 0);
    check_oop("Processor yield. ^Sensor keyboardPressed", ST_TRUE, "true");
    check_integer("Processor yield. ^Sensor keyboard asInteger", 'A');

    /*
     *  A controller wants control when the pointer is over its view, and
     *  does not when it is not.  Moving the pointer changes the answer,
     *  which is the whole of how MVC decides who is in charge.
     */
    GFX_inject_mouse(50, 50);
    check_oop("| v | v := StandardSystemView new."
              " v window: (0@0 corner: 200@200)."
              " ^v controller isControlWanted", ST_TRUE, "true");
    GFX_inject_mouse(500, 400);
    check_oop("| v | v := StandardSystemView new."
              " v window: (0@0 corner: 200@200)."
              " ^v controller isControlWanted", ST_FALSE, "false");
}

/*
 *  Printing, which is the deepest path in the library: printOn: runs Stream,
 *  WriteStream, String, Symbol, Character and -- for a Float -- LargeInteger
 *  division, all at once.  Everything that used to be listed here as not
 *  working now is here as an assertion instead.
 */
static void
test_printing_deep(void)
{
    check_integer("3.5 printString size", 3);
    check_integer("2.0 sqrt printString size", 7);
    check_integer("OrderedCollection new printString size", 20);
    check_integer("(1 to: 5) asOrderedCollection printString size", 30);
    check_integer("(OrderedCollection new add: 1; yourself) printString size",
                  22);
    check_integer("OrderedCollection name size", 17);
}

/*
 *  Mixed-mode arithmetic, which the library does by coercing to the higher
 *  generality and retrying.  Both directions matter and only one of them used
 *  to work: a Float receiver with an Integer argument fell back through
 *  "super >= aNumber", and a binary message to super was not compiled as a
 *  super send at all.
 */
static void
test_mixed_arithmetic(void)
{
    check_oop("3.5 >= 0", ST_TRUE, "true");
    check_oop("3.5 <= 4", ST_TRUE, "true");
    check_oop("3 < 3.5",  ST_TRUE, "true");
    check_integer("(3 + 1.5) truncated", 4);
    check_integer("(3.5 + 1) truncated", 4);
    check_integer("3.5 floor", 3);
    check_integer("3.5 ceiling", 4);
}

int
main(void)
{
    ST_TEST_BEGIN("1983 image");

    if (!load_manifest()) {
        printf("skipped: %s not found (run from the top of the tree)\n",
               MANIFEST);
        return ST_TEST_END();
    }
    if (!build_once())
        return ST_TEST_END();

    /*
     *  The screen first: Text class>>initialize asks Display how wide it is
     *  when it works out the default tab stops, so an image with no Display
     *  gets no text constants and then no text style.
     */
    CHECK(BOOT_install_display(640, 480));

    /*
     *  The step a fileIn does not do and an image build does.  Without it the
     *  library's class variables are all nil, and printString, Symbol
     *  interning and Character creation all walk into nil.
     */
    {
        st_boot_init_report init;

        BOOT_run_initializers(&init);
        printf("  %u class initializers, %u ran, %u skipped, %u unfinished",
               init.defined, init.ran, init.skipped, init.unfinished);
        if (init.unfinished)
            printf(" (first: %s)", init.first_unfinished);
        printf("\n");
        CHECK(init.defined >= 45);
        /*
         *  Every one either ran or was deliberately skipped.  Three are:
         *  Object asks the user a question, Symbol builds the table that
         *  interning reads, and FormMenuView reads Xerox files we do not
         *  ship.  never_initialize in the bootstrap says why for each.
         */
        CHECK_EQ_INT(init.ran + init.skipped, init.defined);
        CHECK_EQ_INT(init.skipped, 3);
    }

    test_classes_present();
    test_arithmetic();
    test_collections();
    test_printing();
    test_symbols();
    test_string_hash_agrees();
    test_floats();
    test_strings();
    test_graphics_objects();
    test_bitblt();
    test_display();
    test_text();
    test_paragraph();
    test_view();
    test_scheduler();
    test_process_scheduler();
    test_processes();
    test_system_organization();
    test_browsing();
    test_browser();
    test_compile_inspect_debug();
    test_globals_are_reachable_by_name();
    test_input();
    test_printing_deep();
    test_mixed_arithmetic();

    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: the bootstrap targets the 64-bit object memory\n");
    return 0;
}

#endif

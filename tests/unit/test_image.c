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
    CHECK_EQ_INT(res.methods_compiled, 4517);
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
     *  The step a fileIn does not do and an image build does.  Without it the
     *  library's class variables are all nil, and printString, Symbol
     *  interning and Character creation all walk into nil.
     */
    {
        st_boot_init_report init;

        BOOT_run_initializers(&init);
        printf("  %u class initializers, %u ran, %u unfinished",
               init.defined, init.ran, init.unfinished);
        if (init.unfinished)
            printf(" (first: %s)", init.first_unfinished);
        printf("\n");
        CHECK(init.defined >= 45);
        CHECK_EQ_INT(init.ran, init.defined);   /*  all of them  */
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

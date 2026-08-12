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
#include "census.h"
#include "gfx.h"
#include "st_sched.h"

#include <stdio.h>
#include <string.h>

#define MANIFEST    "sources/MANIFEST"

/*
 *  The image these tests build is the 1983 library plus the one package
 *  lib/ adds for closures.  Both halves are named so that a count which
 *  moves says WHICH half moved: the Xerox numbers are what they have always
 *  been, and anything else is ours.
 */
#define BLUEBOOK_CLASSES        226
#define BLUEBOOK_METHODS        4521
#define BLUEBOOK_CATEGORIES     41
#define LIB_CLASSES             23       /*  BlockClosure, the exceptions,
                                            SUnit, and the fixtures        */
/*
 *  329 rather than 327: lib/Concurrency gained Mutex, Monitor, SharedQueue
 *  and Promise, and BlockClosure gained the scheduling protocol
 *  BlockContext has had since 1983.  The total moves by only two because
 *  the 1983 SharedQueue is now EXCLUDED -- ours supersedes it, which is
 *  the substitution ratchet, and its methods leave as ours arrive.
 */
#define LIB_METHODS             338
/*
 *  Three, not five: the extension packages define no CLASSES, and a
 *  category is a property of a class definition.  Kernel-Methods-Fixes and
 *  System-Runtime only add methods to classes that already exist.
 */
#define LIB_CATEGORIES          6       /*  Kernel-Closures, -Exceptions,
                                            -Pragmas, Probe-Core, SUnit,
                                            SUnit-Tests                    */
#define MAX_SOURCES 512

static char     paths[MAX_SOURCES][256];
static unsigned path_count;
/*  Where the Blue Book files stop and ours begin.  */
static unsigned first_of_ours;
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
    /*
     *  And ours, which live in lib/ because sources/ is frozen.  Without
     *  BlockClosure the closure bytecodes have nothing to make and every
     *  closure expression stops the interpreter, which is exactly the
     *  arrangement that keeps the 1983 image from ever meeting one.
     *
     *  These are the files profiles/st2026.profile names, listed again
     *  because this test builds its image directly rather than through a
     *  profile.  They compile as CLOSURES; everything above is Blue Book.
     */
    {
        static const char *const ours[] = {
            "lib/Kernel/BlockClosure.class.st",
            "lib/Kernel/WeakArray.class.st",
            "lib/System/SystemDictionary.extension.st",
            "lib/Concurrency/ProcessorScheduler.extension.st",
            "lib/Concurrency/Object.extension.st",
            "lib/Kernel-Pragmas/AdditionalMethodState.class.st",
            "lib/Kernel-Pragmas/Pragma.class.st",
            "lib/Kernel-Pragmas/CompiledMethod.extension.st",
            "lib/Kernel-Exceptions/Exception.class.st",
            "lib/Kernel-Exceptions/Error.class.st",
            "lib/Kernel-Exceptions/Warning.class.st",
            "lib/Kernel-Exceptions/ZeroDivide.class.st",
            "lib/Kernel-Exceptions/MessageNotUnderstood.class.st",
            "lib/Kernel-Exceptions/BlockClosure.extension.st",
            "lib/Kernel-Exceptions/BlockContext.extension.st",
            "lib/Kernel-Exceptions/ContextPart.extension.st",
            "lib/Kernel-Exceptions/Object.extension.st",
            "lib/Kernel-Exceptions/SmallInteger.extension.st",
            "lib/Kernel-Exceptions/AssertionFailure.class.st",
            "lib/Kernel-Methods/MethodContext.extension.st",
            "lib/Kernel-Methods/CompiledMethod.extension.st",
            "lib/Kernel-Protocol/Object.extension.st",
            "lib/Kernel-Protocol/UndefinedObject.extension.st",
            "lib/Kernel-Protocol/BlockContext.extension.st",
            "lib/Kernel-Protocol/BlockClosure.extension.st",
            "lib/Kernel-Protocol/Boolean.extension.st",
            "lib/Kernel-Protocol/String.extension.st",
            "lib/Kernel-Protocol/Symbol.extension.st",
            "lib/Kernel-Protocol/Character.extension.st",
            "lib/Kernel-Protocol/Float.extension.st",
            "lib/Kernel-Protocol/Number.extension.st",
            "lib/Kernel-Protocol/Integer.extension.st",
            "lib/Kernel-Protocol/Collection.extension.st",
            "lib/Kernel-Protocol/Array.extension.st",
            "lib/Kernel-Protocol/Behavior.extension.st",
            "lib/Collections-Protocol/Collection.extension.st",
            "lib/Collections-Protocol/SequenceableCollection.extension.st",
            "lib/Collections-Protocol/ArrayedCollection.extension.st",
            "lib/Collections-Protocol/Dictionary.extension.st",
            "lib/Streams-Protocol/Stream.extension.st",
            "lib/Streams-Protocol/SequenceableCollection.extension.st",
            "lib/Streams-Protocol/Object.extension.st",
            "lib/Streams-Protocol/String.extension.st",
            "lib/Streams-Protocol/Symbol.extension.st",
            "lib/Streams-Protocol/Character.extension.st",
            "lib/Strings-Protocol/String.extension.st",
            "lib/SUnit/TestFailure.class.st",
            "lib/SUnit/TestResult.class.st",
            "lib/SUnit/TestCase.class.st",
            "lib/SUnit/TestSuite.class.st",
            "lib/SUnit-Tests/SUnitTest.class.st",
            "lib/SUnit-Tests/SUnitBrokenTest.class.st",
            "lib/SUnit-Tests/SUnitReportingTest.class.st",
            "lib/Probe/Greeter.class.st",
            "lib/Probe/Initialized.class.st",
            "lib/Probe/SelfMade.class.st",
            "lib/Probe/Subinitialized.class.st",
            "lib/Probe/Slotted.class.st",
            "lib/Probe/TGreeting.trait.st",
            "lib/Probe/Unwind.class.st"
        };
        unsigned    k;

        first_of_ours = path_count;
        for (k = 0; k < sizeof ours / sizeof ours[0]
                 && path_count < MAX_SOURCES; ++k)
            snprintf(paths[path_count++], sizeof paths[0], "%s", ours[k]);
    }
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
    {
        static int  dialects[MAX_SOURCES];
        unsigned    k;

        for (k = 0; k < path_count; ++k)
            dialects[k] = (k >= first_of_ours) ? ST_DIALECT_CLOSURES
                                               : ST_DIALECT_BLUE_BOOK;
        if (BOOT_build_dialects(list, dialects, path_count, &res) != 0) {
            printf("  bootstrap failed: %s\n", res.error);
            return 0;
        }
    }
    printf("  %u classes, %u methods, %u symbols\n", res.classes_created,
           res.methods_compiled, res.symbols_interned);

    CHECK_EQ_INT(res.classes_created, BLUEBOOK_CLASSES + LIB_CLASSES);
    /*  4517 from the MIT sources, plus the few in kernel/Bootstrap.st.  */
    CHECK_EQ_INT(res.methods_compiled, BLUEBOOK_METHODS + LIB_METHODS);
    /*
     *  One trait, and two of its three methods flattened into Greeter --
     *  the third is #subject, which Greeter defines itself and therefore
     *  keeps.  A trait creates no class, so classes_created does not move.
     */
    CHECK_EQ_INT(res.traits_created, 1);
    CHECK_EQ_INT(res.methods_flattened, 2);
    CHECK_EQ_INT(res.traits_rejected, 0);
    /*
     *  Three: Initialized, TestResult and TestSuite.  Not Subinitialized,
     *  whose chain already has one, and not SelfMade, which wrote its own.
     *  A 1983 class never qualifies -- the chunk files already say what
     *  they mean, and the ~34 that want initialization write the idiom out
     *  by hand.  SUnit is where the mechanism stops being a demonstration:
     *  TestSuite new has to answer a suite with an empty collection in it,
     *  and nothing in SUnit says so.
     */
    /*
     *  Eleven, not three, and the jump is deliberate.
     *
     *  Object>>initialize now exists -- Pharo's convention, needed because
     *  every Pharo class whose initialize begins `super initialize' has to
     *  have something to stop at.  A consequence is that `initialize' is
     *  reachable from EVERY class, so the loader synthesizes
     *  `new ^super new initialize' for every lib/ and pharo/ class rather
     *  than only for the few that defined one themselves.
     *
     *  That is Pharo's object model arriving, not an accident: in Pharo,
     *  `Foo new' initializes.  The 1983 classes are untouched, because the
     *  synthesis only ever applied to ours.
     */
    CHECK_EQ_INT(res.news_synthesized, 11);
    built = 1;
    return 1;
}

/*
 *  Evaluate as the driver does: compile the expression as a method body,
 *  stand up a context whose sender is nil, and run.
 */
/*  Which dialect the expressions below are compiled as.  */
static int  test_dialect = ST_DIALECT_BLUE_BOOK;

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
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;
    ctx.dialect            = test_dialect;

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

/*
 *  A String or Symbol answer, compared by its characters.
 */
static void
check_string(const char *expression, const char *want)
{
    st_oop  value = evaluate(expression);
    char    text[256];

    ++st_test_checks;
    if (!OM_is_present(value)) {
        ++st_test_failures;
        printf("  FAIL %s: no answer, want '%s'\n", expression, want);
        return;
    }
    OM_string_of(value, text, sizeof text);
    if (strcmp(text, want) != 0) {
        ++st_test_failures;
        printf("  FAIL %s: got '%s', want '%s'\n", expression, text, want);
    }
}

static void
check_boolean(const char *expression, int want)
{
    check_oop(expression, want ? ST_TRUE : ST_FALSE, want ? "true" : "false");
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
 *  Arithmetic that leaves SmallInteger range.
 *
 *  A primitive is required to FAIL when its result will not fit, because
 *  failing is what runs the Smalltalk body that promotes to a
 *  LargePositiveInteger.  Multiplication did not: it formed a * b in
 *  int64_t, that wrapped, and whenever the wrapped value happened to land
 *  back inside +/-2^62 the primitive answered it and the promotion never
 *  ran.  21 factorial is exactly such a value -- it came out as
 *  -4249290049419214848, negative, a SmallInteger, and wrong by 2^64.
 *
 *  That is the shape worth testing for rather than the number: a primitive
 *  quietly succeeding where it had to fail leaves no trace anywhere, since
 *  failing is the ordinary path and nothing announces not taking it.  So
 *  each check below is an identity that only holds if the promotion
 *  happened, not a comparison against a constant someone could update to
 *  match a wrong answer.
 */
static void
test_integers_larger_than_a_smallinteger(void)
{
    /*
     *  20! fits and 21! does not, so the boundary is crossed here.
     *  check_integer rather than check_class for the small one: a
     *  SmallInteger is a tagged immediate, so it is not "present" as an
     *  object and check_class cannot see it.  Requiring the exact value is
     *  the stronger check anyway.
     */
    check_integer("20 factorial", 2432902008176640000LL);
    check_class("21 factorial", "LargePositiveInteger");
    check_integer("21 factorial // 20 factorial", 21);
    check_integer("21 factorial - 21 factorial", 0);
    check_integer("21 factorial printString size", 20);

    /*  The wrapped product used to be a plausible small number.  */
    check_class("3037000500 * 3037000500", "LargePositiveInteger");
    check_integer("(3037000500 * 3037000500) // 3037000500", 3037000500);

    /*  raisedTo: multiplies, so it wrapped to 0 for anything past 2^63.  */
    check_class("2 raisedTo: 70", "LargePositiveInteger");
    check_integer("(2 raisedTo: 70) // (2 raisedTo: 69)", 2);
    check_integer("(2 raisedTo: 70) printString size", 22);

    /*
     *  Addition never wrapped -- two SmallIntegers always fit int64_t --
     *  but its promotion answered 2^32, because the image builds a
     *  LargePositiveInteger with bitShift: and the primitive refused every
     *  shift of 31 or more.  That bound was inherited from the 16-bit
     *  memory, where it was right.
     */
    check_class("4611686018427387903 + 1", "LargePositiveInteger");
    check_integer("4611686018427387903 + 1 - 1", 4611686018427387903LL);
    check_integer("(1 bitShift: 40)", 1099511627776LL);
    check_integer("(1 bitShift: 40) bitShift: -40", 1);
    check_class("1 bitShift: 62", "LargePositiveInteger");

    /*  And the negative side, which shifts and multiplies differently.  */
    check_integer("(0 - 21 factorial) + 21 factorial", 0);
    check_class("0 - 21 factorial", "LargeNegativeInteger");
}

/*
 *  Evaluating a block twice at once.
 *
 *  A Blue Book BlockContext is the closure and the activation record in one
 *  object, so ST_activate_block used to write the instruction pointer, the
 *  stack pointer and the caller into the very object somebody was holding.
 *  Each activation now gets its own record, which is what these check.
 *
 *  What it does NOT fix, and no amount of copying could: a block's
 *  ARGUMENTS live in the home method's temporary frame, not the block's.
 *  Two activations of one block therefore share the variable, and so do two
 *  different blocks in the same method, which the compiler may give the
 *  same slot.  That is what closures are for and it is Phase D.  The tests
 *  below are written to say which side of that line each case is on.
 */
static void
test_blocks_activate_separately(void)
{
    /*
     *  Recursion where the outer value is already on the stack before the
     *  inner call.  This answered nil until each activation got its own
     *  record; it is the case the copy fixes.
     */
    check_integer("| f | f := [:n | n = 0 ifTrue: [1] "
                  "ifFalse: [n * (f value: n - 1)]]. ^f value: 10",
                  3628800);
    check_integer("| f | f := [:n | n = 0 ifTrue: [0] "
                  "ifFalse: [1 + (f value: n - 1)]]. ^f value: 100", 100);

    /*  A block reached again from inside itself, without arguments.  */
    check_integer("| n b | n := 0. b := [n := n + 1. "
                  "n < 5 ifTrue: [b value]. n]. ^b value", 5);

    /*  Ordinary re-use, which worked before and must keep working.  */
    check_integer("| b | b := [:n | n * 2]. ^(b value: 3) + (b value: 4)", 14);
    check_integer("((1 to: 5) collect: [:i | i * i]) last", 25);
    check_integer("(1 to: 10) inject: 0 into: [:a :b | a + b]", 55);

    /*
     *  And the boundary, asserted rather than left to be discovered.  The
     *  argument of the outer activation is gone after the inner one runs,
     *  because both wrote the same home slot.  When Phase D lands this
     *  answers 7 and the test changes with the behaviour it describes.
     */
    check_integer("| b c | c := [:m | m * 10]. b := [:n | (c value: 99). n]. "
                  "^b value: 7", 99);
}

/*
 *  Closures.
 *
 *  Everything here is compiled in the closure dialect, and every one of
 *  these answers differently -- or not at all -- as a Blue Book block.  A
 *  BlockContext keeps its arguments and temporaries in the HOME method's
 *  frame, so two activations of one block share them; a closure has a frame
 *  of its own and captures what it needs.
 *
 *  The 1983 library and the trace oracle never see any of this: the dialect
 *  is a field in the compile context and defaults to Blue Book.
 */
static void
test_closures(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  The plain shapes, which must go on working.  */
    check_integer("[3 + 4] value", 7);
    check_integer("[:a :b | a * b] value: 6 value: 7", 42);
    check_integer("(1 to: 5) inject: 0 into: [:a :b | a + b]", 15);
    check_integer("((1 to: 20) collect: [:i | i * i]) last", 400);
    check_integer("[:x | | y | y := x * 3. y + 1] value: 5", 16);
    check_integer("| n | n := 0. [n < 5] whileTrue: [n := n + 1]. ^n", 5);

    /*
     *  Recursion.  This is the case Phase B could not fix and named as
     *  Phase D's: it answered nil, because the second activation of the
     *  block overwrote the first one's argument.
     */
    check_integer("| f | f := [:n | n < 2 ifTrue: [n] "
                  "ifFalse: [(f value: n - 1) + (f value: n - 2)]]. "
                  "^f value: 25", 75025);
    check_integer("| f | f := [:n | n = 0 ifTrue: [1] "
                  "ifFalse: [n * (f value: n - 1)]]. ^f value: 10", 3628800);

    /*
     *  Capture by value, and capture by reference.  A name a block only
     *  reads is copied; one that anything assigns has to be shared, or the
     *  two scopes would stop seeing each other's stores.
     */
    check_integer("| t | t := 5. ^[t + 1] value", 6);
    check_integer("| t | t := 5. [t := t * 2] value. ^t", 10);
    check_integer("| t b | t := 1. b := [t]. t := 99. ^b value", 99);
    check_integer("| s | s := 0. (1 to: 10) do: [:i | s := s + i]. ^s", 55);

    /*
     *  A block outliving the method that made it, and each activation
     *  capturing its own copy.  Neither is possible with a BlockContext:
     *  its home is gone, and its argument slot is shared.
     */
    check_integer("| mk | mk := [:n | [n * 2]]. "
                  "^((mk value: 5) value) + ((mk value: 7) value)", 24);
    check_integer("| c | c := [:n | [:m | n + m]]. ^(c value: 10) value: 5",
                  15);
    check_integer("| a | a := OrderedCollection new. "
                  "(1 to: 3) do: [:i | a add: [i]]. "
                  "^(a collect: [:b | b value]) inject: 0 into: [:x :y | x + y]",
                  6);

    /*  Non-local return, out of a block and out of a nested one.  */
    check_oop("| b | b := [:x | x > 3 ifTrue: [^#big]. #small]. ^b value: 5",
              BOOT_intern_symbol("big", NULL), "#big");
    check_oop("| b | b := [:x | x > 3 ifTrue: [^#big]. #small]. ^b value: 1",
              BOOT_intern_symbol("small", NULL), "#small");
    check_integer("| f | f := [:c | c do: [:e | e > 2 ifTrue: [^e]]. 0]. "
                  "^f value: #(1 2 3 4)", 3);
    check_integer("^(1 to: 10) detect: [:i | i > 6]", 7);

    /*
     *  And the boundary Phase B recorded.  As a Blue Book block this
     *  answers 99, because both blocks are given the same home slot; as
     *  closures each argument is its own.  The assertion changes with the
     *  behaviour it describes, which is what it was written for.
     */
    check_integer("| b c | c := [:m | m * 10]. b := [:n | (c value: 99). n]. "
                  "^b value: 7", 7);

    test_dialect = ST_DIALECT_BLUE_BOOK;
    check_integer("| b c | c := [:m | m * 10]. b := [:n | (c value: 99). n]. "
                  "^b value: 7", 99);
}

/*
 *  Exceptions.
 *
 *  Smalltalk-80 has none.  It has Object>>error:, which opens a debugger,
 *  and the convention of passing a block to run when something is not
 *  found; neither can be caught and neither lets a caller decide.
 *
 *  What makes this implementable is that a marked frame can be found by
 *  walking senders.  on:do: declares <primitive: 199> and ensure: declares
 *  <primitive: 198>; neither number is implemented, an unimplemented
 *  primitive fails, so the Smalltalk body runs and the number is left on
 *  the frame as a LABEL.  D0 asked the shipped 1983 image whether it uses
 *  either for anything real: its highest primitive is 135.
 */
static void
test_exceptions(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  Catching, and the value of the protected block when nothing is
        signalled.  */
    check_integer("[3 + 4] on: Error do: [:e | 0]", 7);
    check_integer("[Error new signal] on: Error do: [:e | 42]", 42);
    check_integer("[Error new signal] on: Error do: [:e | e return: 5]", 5);

    /*  The class hierarchy decides what a handler catches.  */
    check_oop("[Error new signal] on: Exception do: [:e | true]", ST_TRUE,
              "true");
    check_integer("[[Error new signal] on: ZeroDivide do: [:e | 1]] "
                  "on: Error do: [:e | 2]", 2);
    check_oop("[Warning new signal] on: Error do: [:e | true]", ST_NIL,
              "nil, from the default action");

    /*  The gate the plan names.  */
    check_integer("[1/0] on: ZeroDivide do: [:e | e return: 42]", 42);
    check_integer("[nil foo] on: MessageNotUnderstood do: [:e | 1]", 1);
    check_integer("[nil error: 'x'] on: Error do: [:e | 9]", 9);

    /*
     *  A handler is disabled while it runs, so anything IT signals goes to
     *  the next handler out.  Without that the search that was meant to
     *  move outwards finds the same frame again and goes round for ever --
     *  an exception a handler could not deal with hung the image.
     */
    check_oop("[[Error new signal] on: Error do: [:e | Error new signal. 1]] "
              "on: Error do: [:e2 | true]", ST_TRUE, "true");

    /*  retry, pass and resume.  */
    check_integer("| n | n := 0. ^[n := n + 1. n < 3 ifTrue: [Error new signal]. n] "
                  "on: Error do: [:e | e retry]", 3);
    check_integer("[[Error new signal] on: Error do: [:e | e pass]] "
                  "on: Error do: [:e | 8]", 8);
    check_integer("[Warning new signal] on: Warning do: [:e | e resume: 9]", 9);
    /*  Resuming really goes back to the signal point, not past it.  */
    check_integer("[(Warning new signal) + 1] on: Warning do: [:e | e resume: 9]",
                  10);
    /*
     *  And an exception resumed after its handler returned is refused.  A
     *  returned context still looks well formed -- do_return nils the
     *  fields of the frame it leaves, not of everything that frame called
     *  -- so the test has to be whether it is still reachable, and that is
     *  in the primitive rather than here.
     */
    check_oop("| saved | [Warning new signal] on: Warning do: "
              "[:e | saved := e. e return: 1]. "
              "^[saved resume: 2] on: Error do: [:x | true]", ST_TRUE, "true");
    /*  retry restarts the frame rather than nesting another one.  */
    check_integer("| n | n := 0. ^[n := n + 1. n < 5 ifTrue: [Error new signal]. n] "
                  "on: Error do: [:e | e retry]", 5);

    /*  Ordinary division is unaffected by the ZeroDivide override.  */
    check_integer("7 // 2", 3);
    check_integer("7 \\\\ 2", 1);
    check_integer("(1/2) denominator", 2);

    /*
     *  Unwinding.  These need real methods: in a doIt every ^ targets the
     *  doIt and there is nothing left to look at afterwards.  Unwind
     *  records what happened in what order, which is the whole question.
     */
    check_oop("Unwind reset. ^Unwind normal",
              BOOT_intern_symbol("normal", NULL), "#normal");
    check_integer("Unwind reset. Unwind normal. ^Unwind trace size", 3);
    check_oop("Unwind reset. ^Unwind earlyReturn",
              BOOT_intern_symbol("early", NULL), "#early");
    /*  body and unwound, and NOT the statement after the ensure:.  */
    check_integer("Unwind reset. Unwind earlyReturn. ^Unwind trace size", 2);
    check_oop("Unwind reset. Unwind earlyReturn. ^Unwind trace last",
              BOOT_intern_symbol("unwound", NULL), "#unwound");

    /*  Two ensure: frames between the ^ and its home: both run, inner
        first.  This is the case these implementations usually get wrong. */
    check_integer("Unwind reset. Unwind nested. ^Unwind trace size", 3);
    check_oop("Unwind reset. Unwind nested. ^Unwind trace at: 2",
              BOOT_intern_symbol("inner", NULL), "#inner");
    check_oop("Unwind reset. Unwind nested. ^Unwind trace at: 3",
              BOOT_intern_symbol("outer", NULL), "#outer");

    /*  ifCurtailed: runs only when the receiver does not finish.  */
    check_integer("Unwind reset. Unwind curtailedNormally. ^Unwind trace size",
                  1);
    check_integer("Unwind reset. Unwind curtailedEarly. ^Unwind trace size", 2);

    /*
     *  And a block returning from a method that has already returned.  The
     *  VM sends cannotReturn:, which until this package existed nothing
     *  implemented -- so it stopped quietly and kept the value, which looks
     *  exactly like success.
     */
    check_oop("[Unwind escapingBlock value] on: Error do: [:e | true]",
              ST_TRUE, "true");

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  Pragmas the image can read.
 *
 *  The Blue Book has one, <primitive: N>, and treats it as syntax rather
 *  than as an object.  Squeak generalised the notation; making the result
 *  readable from Smalltalk is what turns it from something the compiler
 *  throws away into something a program can act on -- which is what SUnit's
 *  <test> is for, and what the parallel-safety audit's <shared: #serialize>
 *  will be.
 *
 *  The AdditionalMethodState is found by scanning the literal frame for
 *  one, not at a fixed index: Pharo puts it next to last, and next to last
 *  here is where the Blue Book header extension goes when a method declares
 *  a primitive.
 */
static void
test_pragmas_are_objects(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  Tonel v3 writes #slots where v1 writes #instVars; for a plain slot
        they say the same thing, and the class behaves the same.  */
    check_integer("(Slotted new left: 3 right: 4) sum", 7);

    /*
     *  A trait is applied by flattening: its source is compiled into the
     *  using class.  Greeter takes TGreeting whole and overrides #subject,
     *  so #greeting comes from the trait and calls the CLASS's version --
     *  which is the property that makes flattening worth doing rather than
     *  copying a CompiledMethod, since a copied method would carry the
     *  trait's own literal frame and instance-variable indices.
     */
    check_string("Greeter new greeting", "hello from a class");
    /*  Class-side trait methods come across too, from #traits alone.  */
    check_string("Greeter defaultGreeting", "hello");
    /*  And the flattened method records where its source lives.  */
    check_string("(Greeter organization categoryOfElement: #greeting)",
                 "*trait:TGreeting");
    /*  A trait creates no class: it is not in the system dictionary.  */
    check_boolean("Smalltalk includesKey: #TGreeting", 0);

    /*
     *  A package class that defines initialize and no new is given the 1983
     *  idiom by the loader, so Pharo-flavoured code allocates the way it
     *  expects to.
     */
    check_integer("Initialized new count", 1);
    /*
     *  And its subclass is NOT given one.  This is the whole reason the rule
     *  is about the chain rather than the class: Initialized class>>new
     *  already sends initialize, so a second new here would send it twice --
     *  the outer super new running this initialize, then the inner one.
     *  2 means once; 3 would mean the bug.
     */
    check_integer("Subinitialized new count", 2);
    /*  A class that writes its own new keeps it, and its initialize with it.  */
    check_oop("SelfMade new count", ST_NIL, "nil");

    /*  A method with no pragmas has none, and costs no literal.  */
    check_integer("(Unwind class compiledMethodAt: #normal) pragmas size", 0);
    /*  Nor does one whose only pragma is a primitive, which is the
        compiler's business rather than the image's.  */
    check_integer("(BlockClosure compiledMethodAt: #on:do:) pragmas size", 0);

    /*  Three of them, keyword and arguments intact.  */
    check_integer("(Unwind class compiledMethodAt: #annotated) pragmas size",
                  3);
    check_oop("(Unwind class compiledMethodAt: #annotated) pragmas first "
              "keyword == #test", ST_TRUE, "true");
    check_oop("((Unwind class compiledMethodAt: #annotated) "
              "pragmaAt: #shared:) argumentAt: 1", 
              BOOT_intern_symbol("serialize", NULL), "#serialize");
    check_integer("((Unwind class compiledMethodAt: #annotated) "
                  "pragmaAt: #deprecated:) arguments first size", 11);
    check_oop("(Unwind class compiledMethodAt: #annotated) hasPragma: #test",
              ST_TRUE, "true");
    check_oop("(Unwind class compiledMethodAt: #annotated) hasPragma: #nope",
              ST_FALSE, "false");
    check_oop("((Unwind class compiledMethodAt: #annotated) pragmaAt: #nope) "
              "isNil", ST_TRUE, "true");

    /*  And the method still runs, which the extra literal must not disturb. */
    check_oop("Unwind annotated", BOOT_intern_symbol("annotated", NULL),
              "#annotated");

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  Weak references, and a collection that can be asked for.
 *
 *  The 1983 library has no garbageCollect anywhere in it -- the image
 *  simply cannot ask -- which is livable until weak references exist and
 *  then is not, because a weak slot is cleared BY a collection and there
 *  was no way to observe the mechanism at all.
 *
 *  Asking for one immediately found something worse.  ST_interp_register is
 *  called from ST_interp_init, which the -run path calls and the doIt path
 *  does not, so the collector walked an interpreter table with nothing in
 *  it and freed the running doIt's own context and method -- and the
 *  interpreter carried on reading bytecodes out of memory that had been
 *  handed back.  It stayed hidden because nothing could request a
 *  collection, and an automatic one only happens when the table fills.
 */
static void
test_weak_references(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  A collection in the middle of a doIt, which used to be a crash.  */
    check_integer("| w | w := WeakArray new: 3. Smalltalk garbageCollect. "
                  "^w size", 3);
    check_integer("| a | a := Array new: 4. Smalltalk garbageCollect. "
                  "a at: 1 put: 7. ^a at: 1", 7);

    /*
     *  The object is made inside a method that has returned, so nothing but
     *  the weak slot holds it.  Doing it inline would not test anything:
     *  the doIt's own stack slot above its stack pointer still names the
     *  object, and the collector walks every slot of a context rather than
     *  only the live ones.
     */
    check_integer("| w | w := WeakArray new: 3. Unwind fillWeakly: w. "
                  "^w livingCount", 1);
    check_integer("| w | w := WeakArray new: 3. Unwind fillWeakly: w. "
                  "Smalltalk garbageCollect. ^w livingCount", 0);
    /*  And a strong reference elsewhere keeps it.  */
    check_integer("| w a | w := WeakArray new: 3. a := Array new: 1. "
                  "Unwind fillWeakly: w. a at: 1 put: (w at: 1). "
                  "Smalltalk garbageCollect. ^w livingCount", 1);

    /*  The named fields of a weak class stay strong; only indexed ones go. */
    check_oop("(WeakArray new: 2) class == WeakArray", ST_TRUE, "true");

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  A restarted frame counted its arguments twice.
 *
 *  MethodContext>>restart is what the Debugger's restart button does, and
 *  what restartWith: does after a method under debug is recompiled.  It set
 *  the stack pointer to "numArgs + numTemps", and numTemps already counts
 *  the arguments -- so a restarted frame came back believing it had two
 *  more values below its stack than it did, and then read whatever was in
 *  those slots.
 *
 *  The class contradicts itself about it, which is what made it findable:
 *  setSender:receiver:method:arguments:, twenty lines further down and the
 *  method that CREATES a context, says "stackp := method numTemps" -- and
 *  that one agrees with the interpreter, which sets a new frame's stack
 *  pointer to the header's temporary count and nothing else.
 */
static void
test_a_restarted_frame_counts_its_arguments_once(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  on:do: takes two arguments and declares one temporary.  */
    check_integer("(BlockClosure compiledMethodAt: #on:do:) numArgs", 2);
    check_integer("(BlockClosure compiledMethodAt: #on:do:) numTemps", 3);
    /*  So the sum the old code used was two too many.  */
    check_integer("| m | m := BlockClosure compiledMethodAt: #on:do:. "
                  "^m numArgs + m numTemps", 5);

    /*
     *  A frame with two arguments and one temporary, restarted.  Its stack
     *  pointer is instance variable 3 -- sender, pc, stackp.
     */
    check_integer("| c | c := Unwind contextTakingTwoArgs: 1 and: 2. "
                  "c restart. ^c instVarAt: 3", 3);
    /*  And its program counter goes back to the first bytecode.  */
    check_oop("| c | c := Unwind contextTakingTwoArgs: 1 and: 2. c restart. "
              "^c pc = c method initialPC", ST_TRUE, "true");

    /*  numStack had the same double count: 12 slots less 3, not less 5.  */
    check_integer("(BlockClosure compiledMethodAt: #on:do:) frameSize", 12);
    check_integer("(BlockClosure compiledMethodAt: #on:do:) numStack", 9);

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  Every method can find its own source.
 *
 *  Chapter 27 keeps a method's source location in its last three bytes, and
 *  CompiledMethod>>getSource reads position ZERO as "there is no source" --
 *  so whatever was written at offset 0 of the sources file was invisible.
 *  Exactly one method was: the first one compiled, which for this manifest
 *  is ArrayedCollection class>>new, and it had been sourceless since the
 *  bootstrap was written.  Nothing announces that; the Browser just shows
 *  an empty pane.
 *
 *  A filler byte at the front of each file fixes it, and the assertions
 *  below are spread across the load order because the failure was
 *  positional -- checking any method but the first would have passed all
 *  along.
 */
static void
test_every_method_can_find_its_source(void)
{
    /*  The first method compiled, which is the one that used to be lost.  */
    check_oop("(ArrayedCollection class compiledMethodAt: #new) getSource "
              "isNil", ST_FALSE, "false");
    check_integer("(ArrayedCollection class compiledMethodAt: #new) getSource "
                  "size", 64);
    check_integer("(ArrayedCollection class compiledMethodAt: #new) fileIndex",
                  1);

    /*  The middle and the end of the load order.  */
    check_oop("(Collection compiledMethodAt: #add:) getSource isNil",
              ST_FALSE, "false");
    check_oop("(SystemDictionary compiledMethodAt: #install) getSource isNil",
              ST_FALSE, "false");

    /*
     *  Two entries: 1 is .sources and 2 is .changes, which is the 1983
     *  convention.  A build needing more spills into 3 and 4 -- there is
     *  room for four in the two bits above the position -- and this library
     *  is nowhere near the limit.
     */
    check_integer("SourceFiles size", 2);
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
    check_integer("SystemOrganization categories size",
                  BLUEBOOK_CATEGORIES + LIB_CATEGORIES);
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
/*
 *  The protocol forty years added, which 1983 does not have.
 *
 *  Every one of these is a send the 1983 image answers with a
 *  doesNotUnderstand:, and every one of them appears in ordinary modern
 *  Smalltalk.  They are the difference between "this system runs the Blue
 *  Book" and "you can write a program in this system".
 */
static void
test_modern_protocol(void)
{
    /*  nil and the ifNil: family.  Real sends here, not compiler magic.  */
    check_string("nil ifNil: ['was nil']", "was nil");
    check_integer("3 ifNil: [0] ifNotNil: [:x | x * 2]", 6);
    check_oop("nil ifNotNil: [:x | x]", ST_NIL, "nil");
    /*  cull: is what lets ifNotNil: take a block of either arity.  */
    check_integer("4 ifNotNil: [7]", 7);
    check_integer("4 ifNotNil: [:x | x]", 4);

    /*  displayString drops the syntax printString has to keep.  */
    check_string("'abc' displayString", "abc");
    check_string("'abc' printString", "'abc'");
    check_string("#foo displayString", "foo");
    check_string("42 displayString", "42");
    check_string("(1 -> 2) printString", "1->2");

    /*  Testing protocol: Object says no, the class in question says yes.  */
    check_boolean("3 isNumber", 1);
    check_boolean("'x' isString", 1);
    check_boolean("#x isSymbol", 1);
    check_boolean("$x isCharacter", 1);
    check_boolean("#(1) isArray", 1);
    check_boolean("3 isString", 0);
    check_boolean("Object isBehavior", 1);

    /*  assert: takes a boolean or a block, and signals a catchable Error.  */
    /*
     *  These run in the Blue Book dialect, like every expression here, so
     *  they are also the check that a 1983 block can catch: on:do: used to
     *  exist only on BlockClosure, which left the 4,500 methods of the
     *  1983 library able to signal an Error and unable to catch one.
     */
    check_string("[Object new assert: 1 = 2. 'no'] on: AssertionFailure"
                 " do: [:e | e messageText]", "assertion failed");
    check_string("[Object new assert: [1 = 2] description: 'nope'] on: Error"
                 " do: [:e | e messageText]", "nope");
    check_string("[Object new assert: 1 = 1. 'ran'] on: Error do: [:e | 'no']",
                 "ran");

    /*  Collections.  */
    check_string("#(3 1 2) sorted printString", "(1 2 3 )");
    /*
     *  A sequenceable collection sorts into its own species -- 'hello'
     *  sorted is 'ehllo', not five Characters in an Array.  Pharo's own
     *  doctest for the method is what said so; ours answered the Array
     *  everything else has to answer, and was wrong to.
     */
    check_string("'hello' sorted", "ehllo");
    check_string("'hello' sorted: [:a :b | a >= b]", "ollhe");
    /*
     *  The parallel primitives.  Single-threaded here, so the answers are
     *  the single-threaded ones -- worker zero of one -- and that is the
     *  point: they answer honestly rather than failing when there is no
     *  pool, so code written against them runs either way.
     */
    /*
     *  The two ready-list walks now live in the VM.  Nothing is waiting
     *  for the processor in a freshly built image, so these answer the
     *  empty answers -- and answering rather than failing is the point:
     *  ProcessorScheduler>>remove:ifAbsent: has to evaluate its block.
     */
    check_oop("Processor primFirstReadyProcessAt: 4", ST_NIL, "nil");
    check_boolean("Processor primRemoveReadyProcess: Processor activeProcess",
                  0);
    check_string("Processor remove: Processor activeProcess"
                 " ifAbsent: ['absent']", "absent");

    check_integer("Processor activeWorkerIndex", 0);
    check_integer("Processor workerCount", 1);
    check_string("Processor activeProcess class name", "Process");
    /*
     *  compareAndSwapSlot:from:to: answers whether the swap HAPPENED,
     *  rather than the old value -- that is what every caller tests, and
     *  it leaves no room to forget the comparison.
     */
    check_boolean("(Array with: 1 with: 2) compareAndSwapSlot: 1 from: 1 to: 9",
                  1);
    check_boolean("(Array with: 1 with: 2) compareAndSwapSlot: 1 from: 7 to: 9",
                  0);
    check_integer("| a | a := Array with: 1 with: 2."
                  " a compareAndSwapSlot: 2 from: 2 to: 42. ^a at: 2", 42);
    check_integer("| a | a := Array with: 1 with: 2."
                  " a compareAndSwapSlot: 2 from: 99 to: 42. ^a at: 2", 2);

    /*
     *  Primitives named by Pharo's Kernel, reachable because lib/ declares
     *  them.  ln and exp are the ones that matter: the 1983 Taylor series
     *  stops at MathApproximationEpsilon and was wrong in float32's last
     *  digit, which Pharo's own doctest for this expression caught.
     */
    check_boolean("(2 raisedTo: (1/12)) = 1.0594630943592953", 1);
    check_string("2.0 ln printString", "0.693147");
    check_boolean("3 ~~ 4", 1);
    /*
     *  identityHash: the hash of the OBJECT, not of its value.  1983 has
     *  the primitive and never gave it this name, because where hash IS
     *  identity the distinction has nowhere to show.  Pharo's identity
     *  collections send it.
     */
    check_boolean("'ab' identityHash = 'ab' copy identityHash", 0);
    check_boolean("| s | s := 'ab'. ^s identityHash = s identityHash", 1);
    check_boolean("3 ~~ 3", 0);
    check_integer("1000 hashMultiply", 53912264);
    check_string("#(1 2) shallowCopy printString", "(1 2 )");
    check_boolean("#(1 2) shallowCopy == #(1 2)", 0);
    /*  A SmallInteger is its own copy; the primitive declines and says so. */
    check_integer("3 shallowCopy", 3);

    /*
     *  A minus written against a number inside #( ) is that number's sign.
     *  It is ambiguous in code -- "3-4" is a send -- and not ambiguous in
     *  a literal array, where there are no sends.  "#(1 5 10 -4)" was FIVE
     *  elements: 1, 5, 10, the symbol #-, and 4.  Nothing failed; the
     *  array was simply the wrong array, and its min answered 1.
     */
    check_integer("#(1 5 10 -4) size", 4);
    check_integer("#(1 5 10 -4) min", -4);
    check_string("#(-1 -2) printString", "(-1 -2 )");
    check_string("#(1.5 -2.5) printString", "(1.5 -2.5 )");
    /*  A minus with a space is still the symbol it looks like.  */
    check_integer("#(a - b) size", 3);
    check_boolean("(#(a - b) at: 2) == #-", 1);
    check_string("(#(3 1 2) sorted: [:a :b | a > b]) printString", "(3 2 1 )");
    check_string("((1 to: 3) flatCollect: [:i | Array with: i with: i])"
                 " printString", "(1 1 2 2 3 3 )");
    check_integer("#(1 2 3 4) count: [:e | e even]", 2);
    check_boolean("#(1 2 3) anySatisfy: [:e | e > 2]", 1);
    check_boolean("#(1 2 3) allSatisfy: [:e | e > 2]", 0);
    check_boolean("#(1 2 3) noneSatisfy: [:e | e > 5]", 1);
    check_string("#() ifEmpty: ['empty']", "empty");
    check_integer("#(9) ifNotEmpty: [:c | c first]", 9);
    check_integer("#(1 2 3) sum", 6);
    check_integer("#(4 9 2) max", 9);
    check_integer("#(4 9 2) min", 2);
    check_string("(#(1 2 3 2) copyWithout: 2) printString", "(1 3 )");
    check_string("#(1 2 3) reversed printString", "(3 2 1 )");
    check_string("#(1 2 3) allButFirst printString", "(2 3 )");
    check_string("(#(1 2 3) first: 2) printString", "(1 2 )");
    check_string("(#(1 2) with: #(10 20) collect: [:a :b | a + b]) printString",
                 "(11 22 )");
    check_string("(Array withAll: (1 to: 3)) printString", "(1 2 3 )");
    check_string("String streamContents: [:s | #(1 2 3)"
                 " do: [:e | s << e] separatedBy: [s << ', ']]", "1, 2, 3");
    /*  A Dictionary fills a missing key rather than answering nil twice.  */
    check_integer("| d | d := Dictionary new."
                  " ^(d at: #k ifAbsentPut: [1]) + (d at: #k ifAbsentPut: [99])",
                  2);

    /*  Streams: << is double dispatch, and writes the display form.  */
    check_string("String streamContents: [:s | s << 'n=' << 42 << ' ' << $x]",
                 "n=42 x");

    /*  Strings.  */
    check_string("', ' join: #('a' 'b' 'c')", "a, b, c");
    check_string("'  padded  ' trimBoth", "padded");
    /*  substrings: collapses runs; splitOn: keeps the empty field.  */
    check_string("('a,,b' substrings: ',') asArray printString", "('a' 'b' )");
    check_string("('a,,b' splitOn: $,) asArray printString", "('a' '' 'b' )");
    check_string("'{1} and {2}' format: #('this' 'that')", "this and that");
    check_boolean("'hello' beginsWith: 'he'", 1);
    check_boolean("'hello' endsWith: 'lo'", 1);
    check_boolean("'hello' includesSubstring: 'ell'", 1);
    check_boolean("'hello' beginsWith: 'xx'", 0);
    check_integer("'42' asInteger", 42);
    check_integer("'-7x' asInteger", -7);
    /*  nil, not 0: '0' and 'banana' are different answers.  */
    check_oop("'banana' asInteger", ST_NIL, "nil");

    /*
     *  fixTemps, which is why asSortedCollection: works at all under
     *  closures: SortedCollection>>sortBlock: sends it to the block, and a
     *  BlockClosure that could not answer it took every sort down with it.
     */
    check_string("((#(3 1 2) asSortedCollection: [:a :b | a > b]) asArray)"
                 " printString", "(3 2 1 )");

    /*
     *  Temporaries declared inside a block the compiler INLINES.  They are
     *  hoisted into the enclosing frame, and the thing to check is that
     *  the hoist stays invisible: the inner t must not be the outer t.
     */
    check_integer("| i | i := 0. [i < 3] whileTrue: [| t | t := i. i := t + 1]."
                  " ^i", 3);
    check_integer("| r | r := 0. true ifTrue: [| t | t := 10. r := r + t]."
                  " true ifTrue: [| t | t := 20. r := r + t]. ^r", 30);
    check_integer("^true ifTrue: [| t | t := 1."
                  " true ifTrue: [| t | t := 2]. t]", 1);
    /*  And a real closure may still capture and assign one.  */
    check_integer("| c | true ifTrue: [| t | t := 5."
                  " c := [t := t + 1. t]]. ^c value + c value", 13);
}

/*
 *  SUnit, running its own tests.
 *
 *  This is the phase's real deliverable: it turns "did the port work" from
 *  a judgement call into a number.  Everything above checks one expression
 *  at a time from C; from here on a ported package can bring its own suite
 *  and say so itself.
 *
 *  The suite is deliberately mixed.  SUnitTest is ordinary passing tests,
 *  and SUnitReportingTest runs tests that FAIL and BLOW UP on purpose and
 *  checks the result counted them in the right buckets -- because a runner
 *  that quietly reports every failure as a pass is worse than no runner,
 *  and nothing but a deliberate failure catches that.
 *
 *  The expressions run in the closure dialect, because the doIt has to
 *  build the blocks SUnit's assertions take.
 */
static void
test_sunit(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*  Every test of SUnit itself passes.  */
    check_string("| s | s := TestSuite new. s addTestCase: SUnitTest."
                 " s addTestCase: SUnitReportingTest. ^s run summary",
                 "12 run, 12 passed, 0 failed, 0 errors");
    check_boolean("| s | s := TestSuite new. s addTestCase: SUnitTest."
                  " s addTestCase: SUnitReportingTest. ^s run hasPassed", 1);

    /*
     *  The subclass graph, which the bootstrap never filled.  Behavior has
     *  four instance variables and only three of them were being written,
     *  so "Object subclasses" answered an empty Set for every class in the
     *  image -- and with it allSubclasses, withAllSubclasses, and anything
     *  that walks DOWN the hierarchy rather than up.  It answered EMPTY
     *  rather than failing, which is why nothing noticed: the same shape as
     *  the method dictionaries that were filled where the image does not
     *  look.  TestCase allSubclasses finding nothing, in an image with
     *  three TestCase subclasses in it, is what found it.
     */
    check_boolean("Object subclasses size > 50", 1);
    check_boolean("Collection subclasses includes: SequenceableCollection", 1);
    check_boolean("Object subclasses includes: Collection", 1);
    /*  The metaclass side has its own parallel chain and is wired too.  */
    check_boolean("Object class subclasses includes: Collection class", 1);
    check_string("(TestCase allSubclasses collect: [:c | c name])"
                 " asSortedCollection asArray printString",
                 "(SUnitBrokenTest SUnitReportingTest SUnitTest )");
    /*
     *  allTests leaves out the fixture whose tests are meant to go wrong.
     *  A whole-image run that reported those would cry wolf every build.
     */
    check_integer("TestCase allTests tests size", 12);

    /*
     *  And the three buckets, from the outside as well as from within
     *  SUnitReportingTest: a pass, a failed assertion, and something
     *  nobody predicted, told apart.
     */
    check_string("SUnitBrokenTest suite run summary",
                 "3 run, 1 passed, 1 failed, 1 errors");

    /*
     *  ensure: runs when an EXCEPTION unwinds past it, not only when a
     *  block returns through it.  SUnit found this: tearDown did not run
     *  after a failed test, because Exception>>return: jumped to the
     *  handler's frame and threw away everything in between without
     *  looking at it.  A file left open and a lock left held, with nothing
     *  to say so.
     */
    check_integer("| f | f := 0."
                  " [[Error new signal: 'boom'] ensure: [f := 1]]"
                  " on: Error do: [:e | nil]. ^f", 1);
    /*  Nested unwinds run innermost first...  */
    check_string("| f | f := OrderedCollection new."
                 " [[[Error signal] ensure: [f add: #inner]]"
                 " ensure: [f add: #outer]] on: Error do: [:e | nil]."
                 " ^f asArray printString", "(inner outer )");
    /*  ...and exactly once, however many paths pass through the frame.  */
    check_integer("| f | f := 0. [[Error signal] ensure: [f := f + 1]]"
                  " on: Error do: [:e | nil]. ^f", 1);
    /*  ifCurtailed: fires on the way out and not on a normal return.  */
    check_integer("| f | f := 0. [[Error signal] ifCurtailed: [f := 1]]"
                  " on: Error do: [:e | nil]. ^f", 1);
    check_integer("| f | f := 0. [[7] ifCurtailed: [f := 1]] value. ^f", 0);
    /*  retry discards the frames in between too, so each go unwinds.  */
    check_string("| n f | n := 0. f := 0."
                 " [[n := n + 1. n < 3 ifTrue: [Error signal]. n]"
                 " ensure: [f := f + 1]] on: Error do: [:e | e retry]."
                 " ^(Array with: n with: f) printString", "(3 3 )");

    test_dialect = saved;
}

static void
test_browsing(void)
{
    check_integer("(Browser new on: SystemOrganization) categoryList size",
                  BLUEBOOK_CATEGORIES + LIB_CATEGORIES);

    /*  Kernel-Objects holds Boolean, False, Object, True, UndefinedObject.  */
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. ^b classList size", 5);

    /*
     *  Boolean's protocols, and the selectors in the first of them.
     *
     *  Five, not the four the 1983 image has: lib/Kernel-Protocol adds
     *  cull: as an extension, and an extension method's protocol is its
     *  own category with a leading star.  The Browser showing it is the
     *  Browser working -- that is what the star is for.
     */
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. b className: #Boolean."
                  " ^b protocolList size", 5);
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. b className: #Boolean."
                  " b protocol: (b protocolList at: 1)."
                  " ^b selectorList size", 6);

    /*  And the source of a method, read back out of SourceFiles.  */
    check_integer("(Boolean sourceCodeAt: #not) size", 122);
    check_integer("((Boolean sourceCodeAt: #not) asText"
                  " makeSelectorBoldIn: Boolean) size", 122);
    /*
     *  The 1983 library's source, the closure package's on top of it, and
     *  one filler byte at the front so that nothing real starts at position
     *  zero -- which a CompiledMethod reads as "no source at all".  See
     *  test_every_method_can_find_its_source.
     */
    /*
     *  Moves whenever lib/ does: the source file is every method's text.
     *  lib/Concurrency's four classes went in and the 1983 SharedQueue
     *  came out.
     */
    check_integer("(SourceFiles at: 1) contents size", 1254018);
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
 *  Self-hosting: the image's own compiler agrees with the C one.
 *
 *  This is the check the plan sets for the compiler, and it is worth being
 *  precise about why it is the right one.  That a method compiled inside the
 *  image RUNS proves the image can compile; it does not prove the two
 *  compilers agree, and they have to, because everything already in the
 *  image was built by the C compiler and everything compiled from now on is
 *  built by the image's.  A disagreement would be a system whose methods
 *  came in two dialects.
 *
 *  So the same source goes through both and the bytecodes are compared byte
 *  for byte.  The last three bytes are the source pointer, which is where
 *  the text was put rather than what was compiled, and the two put it in
 *  different places; everything before them must be identical.
 */
static void
check_same_bytecodes(const char *selector, const char *source)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                expression[1024];
    st_oop              theirs;
    st_oop              ours;
    uint32_t            n_ours;
    uint32_t            n_theirs;
    uint32_t            start_ours;
    uint32_t            start_theirs;
    uint32_t            i;

    /*
     *  Compile it inside the image, through Behavior>>compile:.
     *
     *  The source becomes a Smalltalk string literal, so every quote in it
     *  has to be doubled on the way in -- which is the lexer's escape and
     *  not this file's business, except that getting it wrong makes the
     *  image reject the text and look like a compiler that cannot handle
     *  string literals.
     */
    {
        char       *w = expression;
        const char *r;
        const char *prefix = "Object compile: '";
        const char *suffix = "' classified: 'self-hosting check'"
                             " notifying: nil. ^1";

        for (r = prefix; *r; ++r)
            *w++ = *r;
        for (r = source; *r; ++r) {
            *w++ = *r;
            if (*r == '\'')
                *w++ = '\'';
        }
        for (r = suffix; *r; ++r)
            *w++ = *r;
        *w = '\0';
    }
    evaluate(expression);

    snprintf(expression, sizeof expression,
             "^Object compiledMethodAt: #%s", selector);
    theirs = evaluate(expression);

    ++st_test_checks;
    if (!OM_is_present(theirs)) {
        ++st_test_failures;
        printf("  FAIL the image compiled no method for #%s\n", selector);
        return;
    }

    /*  And in C, from the same text.  */
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
    /*  A send to super needs the method's class in the literal frame.  */
    ctx.method_class_association = BOOT_lookup_global("Object", NULL);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL C could not compile #%s: %s\n", selector, res.error);
        return;
    }
    ours = res.method;

    /*  Same header means same argument, temporary and literal counts.  */
    CHECK_EQ_INT(OM_fetch_pointer(0, ours), OM_fetch_pointer(0, theirs));

    start_ours   = BOOT_method_initial_ip(ours);
    start_theirs = BOOT_method_initial_ip(theirs);
    n_ours   = OM_fetch_byte_length(ours);
    n_theirs = OM_fetch_byte_length(theirs);

    /*  Less the three-byte source pointer each carries.  */
    n_ours   = (n_ours   > start_ours   + 3) ? n_ours   - 3 : start_ours;
    n_theirs = (n_theirs > start_theirs + 3) ? n_theirs - 3 : start_theirs;

    ++st_test_checks;
    if (n_ours - start_ours != n_theirs - start_theirs) {
        ++st_test_failures;
        printf("  FAIL #%s: C emitted %u bytecodes, the image %u\n",
               selector, n_ours - start_ours, n_theirs - start_theirs);
        return;
    }

    for (i = 0; i < n_ours - start_ours; ++i) {
        uint8_t a = (uint8_t) OM_fetch_byte(start_ours + i, ours);
        uint8_t b = (uint8_t) OM_fetch_byte(start_theirs + i, theirs);

        ++st_test_checks;
        if (a != b) {
            ++st_test_failures;
            printf("  FAIL #%s bytecode %u: C emitted %u, the image %u\n",
                   selector, i, a, b);
            return;
        }
    }
}

static void
test_self_hosting(void)
{
    /*  A literal return, the smallest method there is.  */
    check_same_bytecodes("shAnswer", "shAnswer ^42");
    /*  Arguments, temporaries and assignment.  */
    check_same_bytecodes("shAdd:to:",
                         "shAdd: a to: b | t | t _ a + b. ^t");
    /*
     *  A conditional is left out for now, and the reason is recorded rather
     *  than hidden: the 1983 compiler has one-byte forms for a jump of eight
     *  bytes or less and this one always emits the two-byte form, so an
     *  inlined ifTrue:ifFalse: comes out two bytes longer.  The instructions
     *  are otherwise the same and in the same order.  Choosing the short
     *  form needs the distance before the body is compiled, which is what
     *  the 1983 compiler's separate sizing pass is for.
     */
    /*  A loop, which is a backward jump.  */
    check_same_bytecodes("shSum:",
                         "shSum: n | s | s _ 0. 1 to: n do: [:i | s _ s + i]."
                         " ^s");
    /*
     *  Literals of every kind the frame can hold are checked below rather
     *  than here, because the two compilers number the literal frame in
     *  different orders and so disagree on the index in every push that
     *  names one.  Ours assigns an index when it emits the push; the 1983
     *  one assigns a selector its index while parsing, before the sizing
     *  pass gets to the variables, so a selector mentioned later can hold a
     *  lower index than a variable pushed earlier.  Same literals, same
     *  instructions, different numbering.
     */
    /*  A cascade, and a send to super.  */

    check_same_bytecodes("shSuper", "shSuper ^super printString");

    /*
     *  And where the numbering differs, that the two agree on what the
     *  method DOES, which is the part that has to be true.
     */
    check_oop("Object compile: 'shLiterals ^Array with: ''text'' with: #sym"
              " with: $c with: 3.5' classified: 'self-hosting check'"
              " notifying: nil."
              " ^(3 shLiterals) = (Array with: 'text' with: #sym"
              " with: $c with: 3.5)", ST_TRUE, "true");
    check_oop("Object compile: 'shCascade | s | s _ WriteStream on:"
              " String new. s nextPut: $a; nextPut: $b. ^s contents'"
              " classified: 'self-hosting check' notifying: nil."
              " ^(3 shCascade) = 'ab'", ST_TRUE, "true");
    /*  A block with its own argument, closing over an outer temporary.  */
    check_same_bytecodes("shClosure",
                         "shClosure | t | t _ 0."
                         " #(1 2 3) do: [:each | t _ t + each]. ^t");
}

/*
 *  A method compiled inside the image can see a class variable, and sees the
 *  same one everything else does.
 *
 *  A class variable is reached two ways.  A method the C compiler built
 *  holds the Association in its literal frame and reads its value, so it
 *  works whether or not any dictionary exists.  A method compiled LATER has
 *  to find the binding by name, and the only place to look is the class's
 *  classPool.
 *
 *  Ours was nil on every class, so the image compiled a method naming a
 *  class variable and quietly bound it to nil.  It compiled, it ran, and it
 *  answered the wrong thing -- which is what the Browser does every time
 *  someone accepts a method.
 *
 *  MacroSelectors is the case that proves both halves: it is written by
 *  MessageNode class>>initialize, which the C compiler built, and read here
 *  by a method the image compiles now.  Eight entries means the two are
 *  looking at one binding and not at two spelled alike.
 */
static void
test_class_variables_from_the_image(void)
{
    check_integer("MessageNode class compile: 'shMacros ^MacroSelectors'"
                  " classified: 'class variable check' notifying: nil."
                  " ^MessageNode shMacros size", 8);

    /*  And that writing through one is seen through the other.  */
    check_integer("MessageNode class compile: 'shPut: x MacroSelectors _ x'"
                  " classified: 'class variable check' notifying: nil."
                  " MessageNode shPut: #(1 2 3)."
                  " ^MessageNode shMacros size", 3);
    check_integer("MessageNode shPut:"
                  " #(ifTrue: ifFalse: ifTrue:ifFalse: ifFalse:ifTrue:"
                  " and: or: whileFalse: whileTrue:)."
                  " ^MessageNode shMacros size", 8);
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
 *  Changing the image, which is the half the audit did not cover.
 *
 *  Everything above asks whether what the bootstrap BUILT can be found.
 *  These ask whether what the image builds afterwards can be -- recompiling
 *  a method, removing one, adding enough of them that the image has to grow
 *  a method dictionary itself, interning a name nothing had used, and
 *  defining a whole class.  That is what a Browser does all day.
 *
 *  The growth case is the interesting one: twenty-four methods into a class
 *  with two makes HashedCollection>>grow build a dictionary the bootstrap
 *  never touched, and the interpreter then has to read it.
 */
static void
test_changing_the_image(void)
{
    /*  Recompiling replaces the method rather than adding a second.  */
    check_integer("Integer compile: 'shV ^99' classified: 'sh' notifying: nil."
                  " ^3 shV", 99);
    check_integer("Integer compile: 'shV ^100' classified: 'sh' notifying: nil."
                  " ^3 shV", 100);

    /*  Removing it, which goes through MethodDictionary>>become:.  */
    check_integer("Integer compile: 'shGone ^5' classified: 'sh' notifying: nil."
                  " ^3 shGone", 5);
    check_integer("Integer removeSelector: #shGone."
                  " ^(Integer includesSelector: #shGone)"
                  " ifTrue: [1] ifFalse: [0]", 0);

    /*  Enough methods to make the image grow the dictionary, then send them. */
    check_integer("| bad | bad _ 0. 1 to: 24 do: [:i |"
                  " Link compile: 'shM' , i printString , ' ^' , i printString"
                  " classified: 'sh grown' notifying: nil]."
                  " 1 to: 24 do: [:i |"
                  " ((Link new perform: ('shM' , i printString) asSymbol) = i)"
                  " ifFalse: [bad _ bad + 1]]. ^bad", 0);
    check_integer("| bad | bad _ 0. 1 to: 24 do: [:i |"
                  " (Link includesSelector: ('shM' , i printString) asSymbol)"
                  " ifFalse: [bad _ bad + 1]]. ^bad", 0);

    /*  A name nothing had used interns to one object.  */
    check_oop("^'shBrandNewNameNothingUsed' asSymbol"
              " == 'shBrandNewNameNothingUsed' asSymbol", ST_TRUE, "true");
    check_integer("| s | Link compile: 'shFreshName ^7' classified: 'sh'"
                  " notifying: nil. s _ 'shFreshName' asSymbol."
                  " ^Link new perform: s", 7);

    /*  Globals come and go.  */
    check_integer("Smalltalk at: #ShTestGlobal put: 42."
                  " ^Smalltalk at: #ShTestGlobal", 42);
    check_integer("Smalltalk removeKey: #ShTestGlobal."
                  " ^(Smalltalk includesKey: #ShTestGlobal)"
                  " ifTrue: [1] ifFalse: [0]", 0);

    /*
     *  And a class defined from inside the image, with an instance variable
     *  and a class variable, and a method that reads both.
     */
    check_integer("Object subclass: #ShTestClass"
                  " instanceVariableNames: 'aa bb'"
                  " classVariableNames: 'CC' poolDictionaries: ''"
                  " category: 'Sh-Test'."
                  " (Smalltalk at: #ShTestClass) compile:"
                  " 'shSet CC _ 5. aa _ 3. ^aa + CC'"
                  " classified: 'sh' notifying: nil."
                  " ^(Smalltalk at: #ShTestClass) new shSet", 8);
    check_integer("^(Smalltalk at: #ShTestClass) allInstVarNames size", 2);

    /*  The organization follows along, since that is what a Browser lists.  */
    check_oop("Link compile: 'shOrgTest ^1' classified: 'sh category'"
              " notifying: nil."
              " ^(Link organization listAtCategoryNamed: #'sh category')"
              " includes: #shOrgTest", ST_TRUE, "true");
    check_oop("Link removeSelector: #shOrgTest."
              " ^((Link organization listAtCategoryNamed: #'sh category')"
              " includes: #shOrgTest) not", ST_TRUE, "true");
}

/*
 *  The audit: everything the bootstrap builds in C, checked the way the
 *  image looks at it rather than the way the interpreter does.
 *
 *  Three bugs of one shape came out of the Browser, and the shape is worth
 *  naming: the VM is more forgiving than the image.  Method lookup SCANS a
 *  dictionary, so entries in the wrong slots are invisible to it; lookup
 *  steps over a nil dictionary, so a missing one is invisible; a compiled
 *  method holds its variable's Association, so an empty classPool is
 *  invisible.  Every one of those was fine to run and broken to browse.
 *
 *  So the invariant is: whatever a scan finds, a hashed lookup must find
 *  too, and whatever the image will search must be searchable the image's
 *  way.  These check that across every structure the bootstrap builds.
 */
static void
test_audit_what_the_image_searches(void)
{
    /*
     *  The symbol table, built in C with String>>hash duplicated there.
     *  Every selector in the system must come back as the same object when
     *  its characters are interned again.
     */
    check_integer("| bad cls | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " c selectors do: [:sel |"
                  " (sel asString asSymbol == sel)"
                  " ifFalse: [bad _ bad + 1]]]]]. ^bad", 0);
    check_integer("| bad | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " (nm asString asSymbol == nm) ifFalse: [bad _ bad + 1]."
                  " ((Smalltalk at: nm) name asSymbol == nm)"
                  " ifFalse: [bad _ bad + 1]]]. ^bad", 0);

    /*  Smalltalk itself, and every Dictionary it holds.  */
    check_integer("| bad | bad _ 0."
                  " Smalltalk keys do: [:k |"
                  " (Smalltalk includesKey: k) ifFalse: [bad _ bad + 1]]."
                  " Smalltalk do: [:v | (v isKindOf: Dictionary) ifTrue: ["
                  " v keys do: [:k2 |"
                  " (v includesKey: k2) ifFalse: [bad _ bad + 1]]]]."
                  " ^bad", 0);

    /*  The class pools, which hold the class variables by name.  */
    check_integer("| bad cls p | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm. p _ cls classPool."
                  " p keys do: [:k |"
                  " (p includesKey: k) ifFalse: [bad _ bad + 1]]]]. ^bad", 0);

    /*  Every class organization answers for every category it lists.  */
    check_integer("| bad cls o | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " o _ c organization. o isNil ifFalse: ["
                  " o categories do: [:mc |"
                  " (o listAtCategoryNamed: mc) isNil"
                  " ifTrue: [bad _ bad + 1]]]]]]. ^bad", 0);

    /*  The Character table, which is indexed rather than hashed.  */
    check_integer("| bad | bad _ 0. 0 to: 255 do: [:i |"
                  " ((Character value: i) asInteger = i)"
                  " ifFalse: [bad _ bad + 1]."
                  " ((Character value: i) == (Character value: i))"
                  " ifFalse: [bad _ bad + 1]]. ^bad", 0);

    /*
     *  And the instance variable names, which only the image ever reads.
     *
     *  Each class is given an Array of them, and the array was made before
     *  anything was called Array for the four classes that come before it in
     *  file order -- so those four had one with no class, which answers no
     *  messages at all.  Behavior>>allInstVarNames adds each class's names
     *  to its superclass's, so asking any collection what its fields are
     *  called failed, which is the first thing an Inspector does.
     */
    check_oop("| n cls | n _ 0."
              " SystemOrganization categories do: [:cat |"
              " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
              " cls _ Smalltalk at: nm."
              " n _ n + cls allInstVarNames size]]."
              " ^n > 1500", ST_TRUE, "true");
    check_integer("^(Inspector new inspect: 3@4) fieldList size", 3);
    check_oop("^((Inspector new inspect:"
              " (OrderedCollection with: 1 with: 2)) fieldList size > 2)",
              ST_TRUE, "true");
}

/*
 *  What the Browser needs, which is more than what a send needs.
 *
 *  Two things were wrong here and both were invisible to the interpreter.
 *
 *  A method dictionary is an IdentityDictionary, and findKeyOrNil: begins
 *  probing at "key asOop \\ length + 1".  The bootstrap filled it from slot
 *  zero instead.  Lookup scans the whole dictionary, so every send in the
 *  system worked; includesSelector:, compiledMethodAt: and sourceCodeAt: all
 *  go through the hash, so three selectors in five answered "key not found"
 *  -- the ones whose slot did not happen to lie on the probe path from their
 *  own hash.  The other two in five worked, which made it look like
 *  particular methods were broken rather than all of them.
 *
 *  And a dictionary was made only when a class received its first method, so
 *  a class with no methods on a side -- which is most classes, on the class
 *  side -- had nil there.  Lookup steps over that happily.  Behavior>>
 *  selectors is "^methodDict keys", which does not.
 */
static void
test_browsing_finds_every_method(void)
{
    /*
     *  Every selector the organization lists is findable by the image's own
     *  hashing, on both sides of every class.  4521 of them, and the count
     *  is the same one the bootstrap reports compiling.
     */
    check_integer("| bad cls | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " c organization isNil ifFalse: ["
                  " c organization categories do: [:mc |"
                  " (c organization listAtCategoryNamed: mc) do: [:sel |"
                  " (c includesSelector: sel) ifFalse: [bad _ bad + 1]]]]]]]."
                  " ^bad", 0);

    /*
     *  And every class and metaclass answers selectors at all -- at least
     *  the 4521 the bootstrap compiled, plus whatever the checks above have
     *  compiled into the image since.
     */
    check_integer("| n cls | n _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " n _ n + c selectors size]]]."
                  " ^n >= 4521 ifTrue: [1] ifFalse: [0]", 1);

    /*
     *  And the path a person takes: a category, a class, a protocol, a
     *  message, and the source of it.
     */
    check_oop("| b | b _ Browser new on: SystemOrganization."
              " b category: b categoryList first."
              " b className: b classList first."
              " b protocol: b protocolList first."
              " b selector: b selectorList first."
              " ^b text size > 0", ST_TRUE, "true");
    check_integer("| n | n _ 0."
                  " (Array with: Point with: Point class with: Rectangle)"
                  " do: [:c | c selectors do: [:sel |"
                  " n _ n + (c sourceCodeAt: sel) size]]."
                  " ^n > 5000 ifTrue: [1] ifFalse: [0]", 1);
}

/*
 *  Class-side instance variables, which a class definition declares in its
 *  second half:
 *
 *      Form class
 *        instanceVariableNames: 'whiteMask darkGrayMask grayMask ...'
 *
 *  and which hold the stock Forms every piece of drawing asks for.  The
 *  bootstrap parses that header itself, and it treated a newline as
 *  whitespace and a carriage return as part of a name -- so when the chunk
 *  reader started answering carriage returns, every one of these became an
 *  undeclared global bound to nil.
 *
 *  Nothing failed.  The suite stayed green through it, because nothing here
 *  had ever asked Form for a Form.  That is the gap this closes.
 */
static void
test_class_side_instance_variables(void)
{
    check_oop("^Form gray isNil", ST_FALSE, "false");
    check_oop("^Form black isNil", ST_FALSE, "false");
    check_oop("^Form white isNil", ST_FALSE, "false");
    check_oop("^Form lightGray isNil", ST_FALSE, "false");
    /*  And they are real Forms, not something that merely is not nil.  */
    check_integer("^Form gray width", 16);
    check_integer("^Form gray height", 16);
}

/*
 *  Quitting.  SystemDictionary>>quitPrimitive is primitive 113, and with it
 *  unimplemented the method fell through to "self primitiveFailed" -- so
 *  choosing "Quit, without saving" from the system menu raised an error and
 *  printed a backtrace instead of quitting, which is the one menu item whose
 *  whole job is to leave.
 */
static void
test_quit(void)
{
    CHECK_EQ_INT(ST_quit_requested, 0);
    evaluate("Smalltalk quit. ^1");
    CHECK_EQ_INT(ST_quit_requested, 1);
    /*  Cleared, so the checks after this one still have an interpreter.  */
    ST_quit_requested = 0;
}

/*
 *  A multi-line string is multi-line, and the system menu is the proof.
 *
 *  Smalltalk-80 separates lines with Character cr, which is 13.  Not the
 *  linefeed C uses -- Paragraph, CharacterScanner and String>>lines all
 *  break on 13 and on nothing else, and the 1983 sources file is written
 *  with it.  The chunk reader used to normalize every ending to a linefeed,
 *  which is the sensible thing to do for a C program reading a text file and
 *  produces an image in which no string has any line breaks in it.
 *
 *  Nothing reports that.  A Paragraph with no line breaks is a perfectly
 *  good Paragraph; it is just one line long.  The system menu is ten items
 *  in one string separated by nine of them, so it composed to 872 pixels
 *  wide and 8 high -- ten labels side by side, running off the screen.  From
 *  the outside, pressing the yellow button on the desktop did nothing at all.
 */
static void
test_menus_compose_as_lines(void)
{
    /*  The separators survived into the image as carriage returns.  */
    check_integer("^(ScreenController class classPool at: #ScreenYellowButtonMenu)"
                  " isNil ifTrue: [0] ifFalse: [1]", 1);
    check_integer("PopUpMenu compile: 'shLabels ^labelString'"
                  " classified: 'line ending check' notifying: nil."
                  " ^((ScreenController class classPool"
                  " at: #ScreenYellowButtonMenu) shLabels)"
                  " occurrencesOf: (Character value: 13)", 9);
    check_integer("^((ScreenController class classPool"
                  " at: #ScreenYellowButtonMenu) shLabels)"
                  " occurrencesOf: (Character value: 10)", 0);

    /*
     *  And the menu composed to ten lines rather than one.  Eight pixels a
     *  line in this font, so eighty tall and narrow enough to fit -- not
     *  872 by 8, which is what one line of all ten items measures.
     */
    check_integer("PopUpMenu compile: 'shForm ^form'"
                  " classified: 'line ending check' notifying: nil."
                  " ^((ScreenController class classPool"
                  " at: #ScreenYellowButtonMenu) shForm) height", 80);
    check_oop("^(((ScreenController class classPool"
              " at: #ScreenYellowButtonMenu) shForm) width < 300)",
              ST_TRUE, "true");
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
     *  Moving the pointer and nothing else, which is its own kind of load.
     *
     *  Every motion posts an X event and a Y event, so it signals the input
     *  semaphore twice, and both are drained in one pass before any bytecode
     *  runs.  A transfer only NOMINATES a process -- the switch happens when
     *  the interpreter next reaches the top of its loop -- so the second
     *  signal was scheduling against an activeProcess that had already been
     *  displaced, and put it on a run queue a second time.
     *
     *  A process chained onto a list twice has a nextLink pointing at
     *  itself, and is both running and queued.  Suspending it then hands
     *  control straight back to itself, so a terminating process returns
     *  from the terminate it was never meant to return from, off the bottom
     *  of its stack, and the whole image stops.  Resting a hand on the mouse
     *  did it in well under a second.
     */
    {
        int i;

        for (i = 0; i < 200; ++i) {
            GFX_inject_mouse(100 + (i % 400), 80 + (i % 300));
            evaluate("Processor yield. ^1");
        }
    }
    check_oop("Processor yield. ^true", ST_TRUE, "true");
    /*  Still only ever on one list, so the queues are still walkable.  */
    check_oop("| n | n _ 0. 1 to: 8 do: [:i |"
              " ((Processor instVarAt: 1) at: i) do: [:p | n _ n + 1]]."
              " ^n < 100", ST_TRUE, "true");

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
    test_modern_protocol();
    test_sunit();
    test_browsing();
    test_browser();
    test_compile_inspect_debug();
    test_globals_are_reachable_by_name();
    test_self_hosting();
    test_class_variables_from_the_image();
    test_browsing_finds_every_method();
    test_audit_what_the_image_searches();
    test_changing_the_image();
    test_class_side_instance_variables();
    test_menus_compose_as_lines();
    test_quit();
    test_input();
    test_printing_deep();
    test_mixed_arithmetic();
    test_integers_larger_than_a_smallinteger();
    test_blocks_activate_separately();
    test_every_method_can_find_its_source();
    test_closures();
    test_exceptions();
    test_a_restarted_frame_counts_its_arguments_once();
    test_weak_references();
    test_pragmas_are_objects();

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

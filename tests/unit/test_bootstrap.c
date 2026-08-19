/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Phase 5 gate: build an image from source text and run it.
 *
 *  Every Smalltalk-80 image in existence descends by mutation from the one
 *  Xerox shipped in 1983.  This one does not: it is built here, from the
 *  kernel sources in kernel/, by the compiler in src/compiler.  Nothing of
 *  Xerox's is in it.
 *
 *  The check that matters is not that the bootstrap completes -- it is that
 *  the image it produces computes.  So these evaluate expressions in it and
 *  compare answers.  A wrong field layout, a mis-encoded bytecode or a
 *  broken method dictionary all show up as an arithmetic error.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "compiler.h"
#include "bootstrap.h"

#include <stdio.h>
#include <string.h>

#define KERNEL  "kernel/Kernel.st"

static int  built;

static int
build_once(void)
{
    static const char  *paths[1] = { KERNEL };
    st_bootstrap_result res;

    if (BOOT_build(paths, 1, &res) != 0) {
        printf("  bootstrap failed: %s\n", res.error);
        return 0;
    }
    printf("  %u classes, %u methods, %u symbols\n", res.classes_created,
           res.methods_compiled, res.symbols_interned);
    CHECK(res.classes_created >= 30);
    CHECK(res.methods_compiled >= 50);
    built = 1;
    return 1;
}

/*
 *  Evaluate an expression the way the driver does: compile it as a method
 *  body, stand up a context whose sender is nil, and run.  A return with no
 *  sender keeps its answer in st_vm.return_value.
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
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
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
        printf("  cannot compile \"%s\": %s\n", expression, res.error);
        return ST_OOP_INVALID;
    }
    context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 32);
    if (!OM_is_object(context))
        return ST_OOP_INVALID;
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, res.method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int)
                        (BOOT_method_initial_ip(res.method) + 1)));
    /*
     *  The stack begins ABOVE the temporaries.  A stack pointer of zero puts
     *  the first push on top of temporary zero, so a method that declares
     *  any variables overwrites them with its own working stack -- which
     *  looks exactly like a compiler bug and is not one.
     */
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, res.method))));

    memset(&st_vm, 0, sizeof st_vm);
    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    ST_interp_run(1000000);
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
test_arithmetic(void)
{
    check_integer("3 + 4", 7);
    check_integer("100 - 58", 42);
    check_integer("6 * 7", 42);
    check_integer("100 // 7", 14);
    check_integer("100 \\\\ 7", 2);
    check_integer("100 quo: 7", 14);
    check_integer("-5 + 2", -3);

    /*  Precedence: unary binds tightest, then binary, then keyword.  */
    check_integer("(2 + 3) * (4 + 4)", 40);
    check_integer("2 + 3 * 4", 20);         /*  left to right, not BODMAS  */
}

static void
test_booleans(void)
{
    check_oop("3 < 4",  ST_TRUE,  "true");
    check_oop("4 < 3",  ST_FALSE, "false");
    check_oop("3 = 3",  ST_TRUE,  "true");
    check_oop("3 ~= 3", ST_FALSE, "false");
    check_oop("true & false", ST_FALSE, "false");
    check_oop("false | true", ST_TRUE,  "true");
    check_oop("true not", ST_FALSE, "false");
    check_oop("false not", ST_TRUE, "true");

    /*  Polymorphism: isNil is answered differently by nil and by 3.  */
    check_oop("nil isNil", ST_TRUE,  "true");
    check_oop("3 isNil",   ST_FALSE, "false");
    check_oop("nil notNil", ST_FALSE, "false");

    /*  A conditional compiles to a jump, so this exercises the branch.  */
    check_integer("true ifTrue: [42]", 42);
}

static void
test_objects(void)
{
    st_oop  value;

    /*  Instantiation through primitive 70, on a bootstrapped class.  */
    value = evaluate("Object new");
    CHECK(OM_is_present(value));
    CHECK_EQ_INT(OM_fetch_class(value), BOOT_global("Object"));

    /*  Identity, and the class of a class.  */
    check_oop("3 class", BOOT_global("SmallInteger"), "SmallInteger");
    check_oop("nil class", BOOT_global("UndefinedObject"), "UndefinedObject");
    check_oop("true class", BOOT_global("True"), "True");
    check_oop("false class", BOOT_global("False"), "False");

    /*  Characters are unique per code point, which is what makes == work.  */
    check_integer("$A value", 65);
    check_integer("$A asInteger", 65);
    check_oop("$A == $A", ST_TRUE, "true");

    /*  A Point, built by primitive 18 out of a bootstrapped class.  */
    value = evaluate("3 @ 4");
    CHECK(OM_is_present(value));
    CHECK_EQ_INT(OM_fetch_class(value), BOOT_global("Point"));
    check_integer("(3 @ 4) x", 3);
    check_integer("(3 @ 4) y", 4);

    /*  Keyword messages on the class side, and instance variables.  */
    check_integer("(Point x: 8 y: 9) x", 8);
    check_integer("(Point x: 8 y: 9) y", 9);
}

/*
 *  The classes the interpreter names by fixed pointer must be the real
 *  classes, not the placeholders that reserved those pointers.  If this
 *  fails, arithmetic still works but anything reaching a class through a
 *  guaranteed pointer finds an empty object.
 */
static void
test_fixed_pointers(void)
{
    static const struct { st_oop oop; const char *name; } expect[] = {
        { ST_CLASS_SMALL_INTEGER, "SmallInteger" },
        { ST_CLASS_STRING,        "String" },
        { ST_CLASS_ARRAY,         "Array" },
        { ST_CLASS_FLOAT,         "Float" },
        { ST_CLASS_POINT,         "Point" },
        { ST_CLASS_CHARACTER,     "Character" },
        { ST_CLASS_SEMAPHORE,     "Semaphore" },
        { ST_CLASS_COMPILED_METHOD, "CompiledMethod" },
        { ST_CLASS_METHOD_CONTEXT,  "MethodContext" },
        { ST_CLASS_BLOCK_CONTEXT,   "BlockContext" }
    };
    unsigned    i;

    for (i = 0; i < sizeof expect / sizeof expect[0]; ++i)
        CHECK_EQ_INT(expect[i].oop, BOOT_global(expect[i].name));

    /*  And the singletons are instances of the right classes.  */
    CHECK_EQ_INT(OM_fetch_class(ST_NIL),   BOOT_global("UndefinedObject"));
    CHECK_EQ_INT(OM_fetch_class(ST_TRUE),  BOOT_global("True"));
    CHECK_EQ_INT(OM_fetch_class(ST_FALSE), BOOT_global("False"));
}

/*
 *  The metaclass graph closes on itself, which is what the three-pass
 *  bootstrap exists to arrange.  Walking it is the cheapest way to prove it
 *  really closed.
 */
static void
test_metaclass_graph(void)
{
    st_oop  object_class = BOOT_global("Object");
    st_oop  metaclass    = BOOT_global("Metaclass");
    st_oop  object_meta;

    CHECK(OM_is_present(object_class));
    CHECK(OM_is_present(metaclass));

    object_meta = OM_fetch_class(object_class);
    CHECK(OM_is_present(object_meta));

    /*  The class of a metaclass is Metaclass.  */
    CHECK_EQ_INT(OM_fetch_class(object_meta), metaclass);

    /*  And the class of Metaclass is its own metaclass, whose class is
     *  Metaclass again -- the loop that has no valid build order.  */
    CHECK_EQ_INT(OM_fetch_class(OM_fetch_class(metaclass)), metaclass);
}

static void
test_snapshot_round_trip(void)
{
    const char *path = "build/test-bootstrap.image";
    char        err[256];
    st_oop      before;

    before = evaluate("3 + 4");
    CHECK(OM_is_int(before));

    CHECK_EQ_INT(OM_image_save(path, err, sizeof err), 0);
    if (err[0])
        printf("  save: %s\n", err);
    CHECK_EQ_INT(OM_image_load(path, err, sizeof err), 0);
    if (err[0])
        printf("  load: %s\n", err);

    /*
     *  The reloaded image must still be a Smalltalk.  Object pointers are
     *  table indices, so they survive the round trip verbatim -- which is
     *  why the fixed pointers still name the same classes.
     */
    CHECK_EQ_INT(OM_fetch_class(ST_TRUE), BOOT_global("True"));
    CHECK(OM_is_present(BOOT_global("SmallInteger")));
    remove(path);
}

/*
 *  ----------  The literal ceiling, end to end  ----------
 *
 *  test_compiler checks that the compiler refuses a 64th literal.  This
 *  checks the thing that refusal is FOR: that the count the header states
 *  is the count the method has.  The field is six bits, and when the
 *  compiler let a bigger number through, build_header masked it -- 65
 *  became 1, the interpreter looked for the first bytecode 64 words early,
 *  and ran the literal frame as instructions.
 *
 *  It needs a real method rather than the stub context, because the failure
 *  was in the object the compiler builds and not in the bytecodes it emits.
 */
static void
test_literal_ceiling_in_a_real_method(void)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                source[8192];
    unsigned            i;
    int                 n;

    printf("---- the literal ceiling ----\n");

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
    ctx.method_class_association = BOOT_lookup_global("Object", NULL);

    /*
     *  Sixty-two distinct string literals plus the #printString: symbol is
     *  63, exactly what the header can state.
     */
    n = snprintf(source, sizeof source, "doIt");
    for (i = 0; i < 62; ++i)
        n += snprintf(source + n, sizeof source - (size_t) n,
                      " self printString: 'l%u'.", i);
    snprintf(source + n, sizeof source - (size_t) n, " ^self");

    if (COMPILE_method(source, &ctx, &res) != 0) {
        printf("  62 literals should compile: %s\n", res.error);
        CHECK(0);
    }  else  {
        st_compiled_code    code;
        st_oop              header = OM_fetch_pointer(0, res.method);
        unsigned            stated = ST_header_literal_count(header);

        /*
         *  What the header says has to be what the compiler counted.  This
         *  is the whole check: a masked count reads as a small number, and
         *  the interpreter then starts reading bytecodes from the middle of
         *  the literal frame.
         */
        CHECK(stated <= 63);
        if (COMPILE_to_bytecodes(source, &ctx, &code) == 0)
            CHECK_EQ_INT((int) stated, (int) code.literal_count);
        else
            CHECK(0);
        /*  And the first bytecode sits exactly past that many words.  */
        CHECK_EQ_INT((int) BOOT_method_initial_ip(res.method),
                     (int) ((stated + 1) * sizeof(st_oop)));
    }

    /*  One literal past it is refused, not wrapped.  */
    n = snprintf(source, sizeof source, "doIt");
    for (i = 0; i < 70; ++i)
        n += snprintf(source + n, sizeof source - (size_t) n,
                      " self printString: 'l%u'.", i);
    snprintf(source + n, sizeof source - (size_t) n, " ^self");
    CHECK(COMPILE_method(source, &ctx, &res) != 0);
    CHECK(strstr(res.error, "literals") != NULL);
}

int
main(void)
{
    ST_TEST_BEGIN("bootstrap");

    if (!build_once())
        return ST_TEST_END();

    test_fixed_pointers();
    test_metaclass_graph();
    test_arithmetic();
    test_booleans();
    test_objects();
    test_snapshot_round_trip();
    test_literal_ceiling_in_a_real_method();

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

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The compiler: source text to Blue Book bytecodes.
 *
 *  Expectations below are derived from the bytecode table in Chapter 28,
 *  the same table the interpreter implements and that has already been
 *  checked against Xerox's traces.  Where the two halves of the system
 *  agree on an encoding, they agree with the 1983 machine.
 *
 *  These run without an image: the compile context supplies stand-in
 *  literals, since what is being tested is the encoding, not the object
 *  memory.
 */

#include "st_test.h"
#include "om.h"
#include "compiler.h"
#include "chunk.h"
#include "lexer.h"

#include <stdio.h>
#include <string.h>

/*  ----------  A context that fabricates literals  ----------  */

static char     symbol_names[64][64];
static unsigned symbol_count;

static st_oop
stub_symbol(const char *text, void *user)
{
    unsigned    i;

    (void) user;
    for (i = 0; i < symbol_count; ++i) {
        if (strcmp(symbol_names[i], text) == 0)
            return (st_oop) (1000 + i * 2);
    }
    if (symbol_count < 64)
        snprintf(symbol_names[symbol_count], 64, "%.63s", text);
    return (st_oop) (1000 + symbol_count++ * 2);
}

static st_oop stub_string(const char *text, void *user)
{ (void) text; (void) user; return 2000; }
static st_oop stub_float(double v, void *user)
{ (void) v; (void) user; return 2002; }
static st_oop stub_large(int64_t v, void *user)
{ (void) v; (void) user; return 2004; }
static st_oop stub_array(st_oop *e, unsigned n, void *user)
{ (void) e; (void) n; (void) user; return 2006; }
static st_oop stub_global(const char *name, void *user)
{
    (void) user;
    /*  Anything spelled with a capital resolves; everything else does not. */
    if (name[0] >= 'A' && name[0] <= 'Z')
        return 3000;
    return ST_NIL;
}

static const char *const instance_variables[] = { "x", "y", "bits" };

static st_compile_context
context(void)
{
    st_compile_context  ctx;

    memset(&ctx, 0, sizeof ctx);
    ctx.instance_variables      = instance_variables;
    ctx.instance_variable_count = 3;
    ctx.intern_symbol      = stub_symbol;
    ctx.make_string        = stub_string;
    ctx.make_float         = stub_float;
    ctx.make_large_integer = stub_large;
    ctx.make_array         = stub_array;
    ctx.lookup_global      = stub_global;
    return ctx;
}

static void
check_bytecodes(const char *source, const uint8_t *want, unsigned want_len,
                const char *label)
{
    st_compile_context  ctx = context();
    st_compiled_code    code;
    unsigned            i;
    int                 ok;

    symbol_count = 0;
    if (COMPILE_to_bytecodes(source, &ctx, &code) != 0) {
        printf("  %s: compile failed at line %u: %s\n", label,
               code.error_line, code.error);
        CHECK(0);
        return;
    }
    ok = (code.length == want_len);
    if (ok) {
        for (i = 0; i < want_len; ++i) {
            if (code.bytecodes[i] != want[i]) {
                ok = 0;
                break;
            }
        }
    }
    if (!ok) {
        printf("  %s\n    want:", label);
        for (i = 0; i < want_len; ++i)
            printf(" %u", want[i]);
        printf("\n    got :");
        for (i = 0; i < code.length; ++i)
            printf(" %u", code.bytecodes[i]);
        printf("\n");
    }
    CHECK(ok);
}

#define CHECK_CODE(src, label, ...)                                     \
    do {                                                                \
        static const uint8_t want[] = { __VA_ARGS__ };                  \
        check_bytecodes((src), want,                                    \
                        (unsigned) (sizeof want / sizeof want[0]),       \
                        (label));                                       \
    } while (0)

static void
test_returns(void)
{
    /*  112 push self, 124 return stack top.  */
    CHECK_CODE("foo ^self", "^self", 112, 124);
    CHECK_CODE("foo ^true",  "^true",  113, 124);
    CHECK_CODE("foo ^false", "^false", 114, 124);
    CHECK_CODE("foo ^nil",   "^nil",   115, 124);

    /*  117..119 push 0, 1, 2; 116 pushes -1.  */
    CHECK_CODE("foo ^0",  "^0",  117, 124);
    CHECK_CODE("foo ^1",  "^1",  118, 124);
    CHECK_CODE("foo ^2",  "^2",  119, 124);
    CHECK_CODE("foo ^-1", "^-1", 116, 124);

    /*  Anything else becomes literal 0, pushed by bytecode 32.  */
    CHECK_CODE("foo ^3", "^3", 32, 124);

    /*  A method with no return answers the receiver: 120.  */
    CHECK_CODE("foo 1", "no explicit return", 118, 120);
}

static void
test_variables(void)
{
    /*  Instance variables 0..15 push with bytecodes 0..15.  */
    CHECK_CODE("foo ^x",    "^x (instance 0)",    0, 124);
    CHECK_CODE("foo ^y",    "^y (instance 1)",    1, 124);
    CHECK_CODE("foo ^bits", "^bits (instance 2)", 2, 124);

    /*  Arguments are temporaries, pushed with 16..31.  */
    CHECK_CODE("foo: a ^a",       "^a (argument 0)", 16, 124);
    CHECK_CODE("foo: a bar: b ^b", "^b (argument 1)", 17, 124);

    /*  A global becomes a literal variable, pushed with 64..95.  */
    CHECK_CODE("foo ^Display", "^Display (global)", 64, 124);
}

static void
test_sends(void)
{
    /*  Arithmetic selectors are single bytes, 176 upward.  */
    CHECK_CODE("foo ^1 + 2",  "1 + 2",  118, 119, 176, 124);
    CHECK_CODE("foo ^1 - 2",  "1 - 2",  118, 119, 177, 124);
    CHECK_CODE("foo ^1 < 2",  "1 < 2",  118, 119, 178, 124);
    CHECK_CODE("foo ^1 * 2",  "1 * 2",  118, 119, 184, 124);
    CHECK_CODE("foo ^1 @ 2",  "1 @ 2",  118, 119, 187, 124);

    /*  So are the special selectors, 192 upward.  */
    CHECK_CODE("foo ^self class", "self class", 112, 199, 124);
    CHECK_CODE("foo ^self size",  "self size",  112, 194, 124);
    CHECK_CODE("foo ^self at: 1", "self at: 1", 112, 118, 192, 124);
    CHECK_CODE("foo ^self at: 1 put: 2", "self at:put:",
               112, 118, 119, 193, 124);

    /*  Everything else is a literal-selector send: 208 + 16*argc + index.  */
    CHECK_CODE("foo ^self bar", "unary send", 112, 208, 124);
    CHECK_CODE("foo ^self bar: 1", "one-argument send", 112, 118, 224, 124);
    CHECK_CODE("foo ^self bar: 1 baz: 2", "two-argument send",
               112, 118, 119, 240, 124);

    /*  Unary sends bind tighter than binary, which bind tighter than
     *  keyword -- so this is (self foo) + (self bar), sent as baz:.  */
    CHECK_CODE("foo ^self one + self two", "precedence",
               112, 208, 112, 224 - 16 + 1, 176, 124);
}

static void
test_super(void)
{
    /*
     *  A send to super uses the extended form even for a selector that has
     *  a one-byte encoding, because the lookup class differs.
     */
    CHECK_CODE("foo ^super bar", "super send", 112, 133, 0, 124);
}

static void
test_assignment(void)
{
    /*
     *  An assignment leaves its value on the stack, so a statement-level one
     *  is followed by a pop.  The 1983 compiler folded the two into a single
     *  pop-and-store; ours does not yet, which is a difference in bytes and
     *  not in meaning.  Recorded here so the divergence is deliberate.
     */
    CHECK_CODE("foo | a | a := 1. ^a", "assign then return",
               118, 129, 64, 135, 16, 124);
    CHECK_CODE("foo x := 1. ^x", "assign an instance variable",
               118, 129, 0, 135, 0, 124);
}

static void
test_blocks(void)
{
    /*
     *  A block is: push the home context, push the argument count,
     *  blockCopy:, jump over the body, body, return-from-block.
     *  Bytecode 200 is blockCopy: and 125 is the block return.
     */
    CHECK_CODE("foo ^[1]", "empty-ish block",
               112, 117, 200, 164, 2, 118, 125, 124);
    CHECK_CODE("foo ^[:a | a]", "block with an argument",
               112, 118, 200, 164, 3, 104, 16, 125, 124);
}

static void
test_conditionals(void)
{
    /*
     *  ifTrue: compiles to a jump, not a send.  The reference traces show
     *  the same: a conditional appears as bytecode 172, never as a send of
     *  ifTrue:.  When the branch is not taken the expression answers nil.
     */
    CHECK_CODE("foo ^true ifTrue: [1]", "ifTrue:",
               113, 172, 3, 118, 164, 1, 115, 124);
}

/*  ----------  The chunk reader  ----------  */

static void
test_chunk_reader(void)
{
    st_chunk_reader    *r;
    st_chunk            chunk;

    /*  Chunks end at a bang; a doubled bang is a literal one.  */
    r = CHUNK_open_string("first chunk! second with a bang!! inside!");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_STR(chunk.text, "first chunk");
    CHECK_EQ_INT(chunk.is_reader, 0);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_STR(chunk.text, "second with a bang! inside");
    CHECK(!CHUNK_next(r, &chunk));
    CHUNK_close(r);

    /*  A leading bang introduces a reader chunk; an empty chunk ends it.  */
    r = CHUNK_open_string("!Foo methodsFor: 'x'!\nbar ^1! !");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.is_reader, 1);
    CHECK_EQ_STR(chunk.text, "Foo methodsFor: 'x'");
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.is_reader, 0);
    CHECK_EQ_STR(chunk.text, "bar ^1");
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.is_empty, 1);
    CHUNK_close(r);

    /*  The 1983 files end lines with CR, which must read as a newline.  */
    r = CHUNK_open_string("a\rb!");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_STR(chunk.text, "a\nb");
    CHUNK_close(r);
}

/*  ----------  The lexer  ----------  */

static void
test_lexer_details(void)
{
    st_lexer   *lx;
    st_token    t;

    /*  Radix numbers and the 1983 assignment arrow.  */
    lx = LEX_open("16rFF _ 2r1010");
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_INTEGER);
    CHECK_EQ_INT(t.integer, 255);
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_ASSIGN);
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.integer, 10);
    LEX_close(lx);

    lx = LEX_open("3.5e2");
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_FLOAT);
    CHECK(t.real > 349.9 && t.real < 350.1);
    LEX_close(lx);

    /*
     *  The minus rule.  "3-4" is a send, because an operand has already been
     *  read; after a keyword there is no operand yet, so "-4" is a literal.
     */
    lx = LEX_open("3-4");
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.integer, 3);
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_BINARY);
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.integer, 4);
    LEX_close(lx);

    lx = LEX_open("foo: -4");
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_KEYWORD);
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_INTEGER);
    CHECK_EQ_INT(t.integer, -4);
    LEX_close(lx);

    /*  A keyword must not swallow the colon of an assignment.  */
    lx = LEX_open("x := 1");
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_IDENTIFIER);
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_ASSIGN);
    LEX_close(lx);

    /*  A comment may contain doubled quotes.  */
    lx = LEX_open("\"a \"\"quoted\"\" comment\" foo");
    LEX_next(lx, &t);
    CHECK_EQ_INT(t.kind, ST_TOK_IDENTIFIER);
    CHECK_EQ_STR(t.text, "foo");
    LEX_close(lx);
}

int
main(void)
{
    ST_TEST_BEGIN("compiler");

    test_lexer_details();
    test_chunk_reader();
    test_returns();
    test_variables();
    test_sends();
    test_super();
    test_assignment();
    test_blocks();
    test_conditionals();

    return ST_TEST_END();
}

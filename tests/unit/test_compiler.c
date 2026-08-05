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
static st_oop stub_character(unsigned code, void *user)
{ (void) user; return (st_oop) (4000 + code * 2); }
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
    ctx.make_character     = stub_character;
    /*  A super send needs a method class; any Association will do here.  */
    ctx.method_class_association = 5000;
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

    /*
     *  A method with no return answers the receiver: 120.  The statement's
     *  own value is dropped first, which is not merely tidiness -- 74 of the
     *  114 methods in a 250-method sample of the 1983 image do exactly this,
     *  and the other 40 end in a storing bytecode that has already consumed
     *  the value.  Object>>changed is the canonical shape:
     *      self changed: self   ->   112 112 224 135 120
     */
    CHECK_CODE("foo 1", "no explicit return", 118, 135, 120);
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

    /*
     *  A BINARY message to super is a super send too, and must not take the
     *  one-byte encoding: 181 sends >= to the receiver, which in Float>>>=
     *  is the method that is running.  The 1983 library has 29 binary super
     *  sends and comparing a Float with an Integer is one of them, so this
     *  recursed until it ran out of bytecodes.
     */
    CHECK_CODE("foo: n ^super >= n", "binary super send",
               112, 16, 133, 32, 124);
    CHECK_CODE("foo: n ^super + n", "binary super send of +",
               112, 16, 133, 32, 124);

    /*
     *  Once anything has been sent the receiver is a result, not super.
     *
     *  This is the case that mattered: "super new compositionRectangle: ..."
     *  is a unary send to super, and then a KEYWORD send to what it answered.
     *  Reporting only binary sends up to the keyword level left the keyword
     *  message marked super too, so the lookup began above the receiver's
     *  class and walked straight past the method it wanted -- Paragraph new
     *  could not find Paragraph's own initialiser.
     */
    CHECK_CODE("foo: n ^super bar + n", "super, then an ordinary binary",
               112, 133, 0, 16, 176, 124);
    CHECK_CODE("foo: n ^super bar baz: n", "super unary, then keyword",
               112, 133, 0, 16, 225, 124);
    CHECK_CODE("foo: n ^super bar + n baz: n", "super unary, binary, keyword",
               112, 133, 0, 16, 176, 16, 225, 124);
    CHECK_CODE("foo: n ^super bar: n", "keyword super send",
               112, 16, 133, 32, 124);
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
     *
     *  The context is pushed with 137, not with 112.  In a method they are
     *  easy to confuse because both leave something plausible on the stack,
     *  but 112 is self -- and a block copied from the receiver rather than
     *  from the context has no home, no outer temporaries and, in a doIt
     *  where self is nil, no anything.
     */
    CHECK_CODE("foo ^[1]", "empty-ish block",
               137, 117, 200, 164, 2, 118, 125, 124);
    CHECK_CODE("foo ^[:a | a]", "block with an argument",
               137, 118, 200, 164, 3, 104, 16, 125, 124);

    /*
     *  The bar is optional when the block has no body.  The Blue Book
     *  grammar shows it as required, but Xerox's compiler did not insist and
     *  its own sources depend on it -- [:result], [:byte ] and a dozen more
     *  appear in Smalltalk-80.sources, so the class library will not load
     *  without this.  Such a block takes its argument and answers nil.
     */
    CHECK_CODE("foo ^[:a]", "argument-only block",
               137, 118, 200, 164, 3, 104, 115, 125, 124);
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

    /*  ifFalse: branches on TRUE, bytecode 168 -- a different opcode, and
     *  emitting the unconditional jump instead is silently always-taken.  */
    CHECK_CODE("foo ^true ifFalse: [1]", "ifFalse:",
               113, 168, 3, 118, 164, 1, 115, 124);

    /*  Both arms present: no nil, and the first arm jumps over the second. */
    CHECK_CODE("foo ^true ifTrue: [1] ifFalse: [2]", "ifTrue:ifFalse:",
               113, 172, 3, 118, 164, 1, 119, 124);
    CHECK_CODE("foo ^true ifFalse: [1] ifTrue: [2]", "ifFalse:ifTrue:",
               113, 168, 3, 118, 164, 1, 119, 124);

    /*  and: and or: short-circuit to a constant rather than to nil.  */
    CHECK_CODE("foo ^true and: [false]", "and:",
               113, 172, 3, 114, 164, 1, 114, 124);
    CHECK_CODE("foo ^true or: [false]", "or:",
               113, 168, 3, 114, 164, 1, 113, 124);
}

/*
 *  A loop's test belongs at the top of the loop, re-executed each time
 *  round, so the receiver block is inlined rather than built.  The jump back
 *  is the awkward part: its offset is negative, and the high three bits are
 *  signed -- masking them turns -1 into 7, which is a pop-and-jump-on-true
 *  and falls straight out of the loop.
 */
static void
test_loops(void)
{
    /*
     *  In statement position the loop's nil answer is not emitted at all, so
     *  the backward jump is followed straight by returnSelf.  That is the
     *  1983 shape exactly -- LinkedList>>do: compiles to
     *      0 105 17 115 198 168 9 16 17 202 135 17 208 105 163 242 120
     *  which is the same skeleton: test, branch out, body, pop, jump back,
     *  return, with nothing in between.
     */
    CHECK_CODE("foo [false] whileTrue: [1]", "whileTrue:",
               114,             /*  the test: push false     */
               172, 4,          /*  leave when false         */
               118,             /*  the body                 */
               135,             /*  discard the body's value */
               163, 249,        /*  jump back -7 to the test */
               120);
    CHECK_CODE("foo [false] whileFalse: [1]", "whileFalse:",
               114, 168, 4, 118, 135, 163, 249, 120);

    /*  A test-only loop has no body to discard.  */
    CHECK_CODE("foo [false] whileTrue", "whileTrue",
               114, 172, 2, 163, 251, 120);

    /*  But where the value IS wanted the nil stays.  */
    CHECK_CODE("foo ^[false] whileTrue: [1]", "whileTrue: for value",
               114, 172, 4, 118, 135, 163, 249, 115, 124);

    /*  And a loop nested in a larger expression keeps it too, which is what
     *  makes dropping the byte safe: the test is positional.  */
    CHECK_CODE("foo ^([false] whileTrue: [1]) isNil", "nested loop",
               114, 172, 4, 118, 135, 163, 249, 115, 208, 124);
}

/*
 *  A cascade evaluates its receiver once and duplicates it before every
 *  message but the last.
 */
/*
 *  A minus sign binds to a numeric literal that follows it directly, and
 *  that rule applies inside a binary selector too.
 *
 *  Binary selectors are greedy and two characters long, so "@-" reads as one
 *  and "-2@-2" becomes "-2 @- 2".  Cursor class>>initialize is full of
 *  offsets written exactly that way, so no cursor could be built, and the
 *  Compiler could not report anything because reporting starts with
 *  "Cursor execute show".
 */
static void
test_negative_after_binary(void)
{
    /*  @ then the literal -2, not the selector @-.  Bytecode 187 is @.  */
    /*  Only -1 has a push of its own; -2 is a literal, and the same one
     *  twice is one literal.  */
    CHECK_CODE("foo ^2 @ -2", "point with a negative y", 119, 32, 187, 124);
    CHECK_CODE("foo ^-2 @ -2", "both negative", 32, 32, 187, 124);

    /*  A real two-character selector is still one token.  */
    CHECK_CODE("foo ^1 <= 2", "<= is one selector", 118, 119, 180, 124);
    CHECK_CODE("foo ^1 // 2", "// is one selector", 118, 119, 189, 124);

    /*  And a minus with a space before a digit is still a send.  */
    CHECK_CODE("foo ^3 - 4", "subtraction", 32, 33, 177, 124);
}

/*
 *  A context holds its temporaries and its working stack together, and the
 *  method header chooses the small size or the large one.  Deciding that on
 *  the temporary count alone is half the question: a method with two
 *  temporaries and a deeply nested expression overflows a small context and
 *  writes past the end of it, which corrupts the heap rather than failing.
 *
 *  These check the depth is measured, not guessed.  COMPILE_to_bytecodes
 *  reports it through needs_large_context.
 */
static void
check_context_size(const char *source, int want_large, const char *label)
{
    st_compile_context  ctx = context();
    st_compiled_code    code;

    symbol_count = 0;
    if (COMPILE_to_bytecodes(source, &ctx, &code) != 0) {
        printf("  %s: compile failed: %s\n", label, code.error);
        CHECK(0);
        return;
    }
    ++st_test_checks;
    if (code.needs_large_context != want_large) {
        ++st_test_failures;
        printf("  FAIL %s: large context %d, want %d\n", label,
               code.needs_large_context, want_large);
    }
}

static void
test_context_size(void)
{
    /*  Shallow and few temporaries: small.  */
    check_context_size("foo ^1 + 2", 0, "a small method");
    check_context_size("foo | a b c | ^a", 0, "three temporaries");

    /*
     *  Deeply nested arguments with no temporaries at all.  Each pending
     *  receiver and argument sits on the stack until its send happens, so
     *  this needs more room than a small context has -- and it has no
     *  temporaries to give it away.
     */
    check_context_size("foo ^self a: 1 b: 2 c: 3 d: 4 e: 5 f: 6 g: 7 h: 8"
                       " i: 9 j: 10 k: 11 l: 12 m: 13",
                       1, "fourteen deep");

    /*  And many temporaries still force it, as before.  */
    check_context_size("foo | a b c d e f g h i j k l m | ^a", 1,
                       "thirteen temporaries");
}


/*
 *  A block argument that shares a name with an enclosing temporary IS that
 *  temporary: it gets the same slot, not one of its own.
 *
 *  This looks like a mistake and is the 1983 rule, and a good deal of the
 *  library depends on it.  RunArray>>copyFrom:to: declares
 *  "| run1 offset1 value1 ... |" and then writes
 *
 *      self at: start setRunOffsetAndValue: [:run1 :offset1 :value1 | value1]
 *
 *  before going on to use run1 and offset1 in the METHOD.  The block is how
 *  those variables get their values.  Give the argument a slot of its own --
 *  which is what "inner scopes shadow outer ones" would mean anywhere else --
 *  and the method reads nil for ever, so no Text can be emphasised and the
 *  Browser cannot show a method's source.
 */
static void
test_block_argument_slots(void)
{
    /*
     *  "b" is temporary 1, and the block's argument is temporary 1 too: the
     *  store is 105 (pop into 1) and the read is 17 (push 1).
     */
    CHECK_CODE("foo | a b | ^[:b | b]", "block argument shares its name",
               137, 118, 200, 164, 3, 105, 17, 125, 124);

    /*  A name that is not already taken gets the next slot, 2.  */
    CHECK_CODE("foo | a b | ^[:c | c]", "block argument with a new name",
               137, 118, 200, 164, 3, 106, 18, 125, 124);

    /*  A method argument works the same way: "x" is temporary 0.  */
    CHECK_CODE("foo: x ^[:x | x]", "block argument shares an argument",
               137, 118, 200, 164, 3, 104, 16, 125, 124);

    /*  And outside the block the name still means the same slot.  */
    CHECK_CODE("foo: x ^x", "the name outside the block", 16, 124);
}

static void
test_cascades(void)
{
    CHECK_CODE("foo ^self one; two; three", "cascade of three unary sends",
               112,             /*  the receiver, once            */
               136, 208,        /*  dup, send one                 */
               135, 136, 209,   /*  drop, dup, send two           */
               135, 210,        /*  drop, send three -- no dup    */
               124);
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
    CHECK_EQ_STR(chunk.text, " second with a bang! inside");
    CHECK(!CHUNK_next(r, &chunk));
    CHUNK_close(r);

    /*
     *  A reader expression is the chunk after an empty one.  The bang before
     *  "Foo" is a separator like any other, so the whitespace ahead of it
     *  forms the empty chunk that marks what follows as a reader.
     */
    r = CHUNK_open_string("x!\n!Foo methodsFor: 'x'!\nbar ^1! !");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));           /*  x                          */
    CHECK_EQ_STR(chunk.text, "x");
    CHECK(CHUNK_next(r, &chunk));           /*  the newline: empty         */
    CHECK_EQ_INT(chunk.is_empty, 1);
    CHECK(CHUNK_next(r, &chunk));           /*  the reader expression      */
    CHECK_EQ_INT(chunk.is_reader, 1);
    CHECK_EQ_STR(chunk.text, "Foo methodsFor: 'x'");
    CHECK(CHUNK_next(r, &chunk));           /*  the method                 */
    CHECK_EQ_INT(chunk.is_reader, 0);
    CHECK_EQ_STR(chunk.text, "\nbar ^1");
    CHECK(CHUNK_next(r, &chunk));           /*  "! !" closes the category   */
    CHECK_EQ_INT(chunk.is_empty, 1);
    CHUNK_close(r);

    /*
     *  has_code and is_empty answer different questions, and the difference
     *  is load-bearing at both ends.
     *
     *  A chunk of nothing but a comment has no code, which is how the
     *  markbush sources close a method category -- they write a comment
     *  where standard fileIn writes the empty chunk of "! !".  But it is NOT
     *  empty, and it must not be, because emptiness is what marks the next
     *  chunk as a reader expression.  Conflating the two breaks one end or
     *  the other: treat a comment as empty and every class definition that
     *  follows one is mistaken for a reader and silently dropped; treat it
     *  as code and it is compiled as a method, which it cannot be.
     */
    r = CHUNK_open_string("\"just a comment\"!  !x _ 1!");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.has_code, 0);        /*  nothing to compile     */
    CHECK_EQ_INT(chunk.is_empty, 0);        /*  but not empty          */
    CHECK_EQ_INT(chunk.is_reader, 0);
    CHECK(CHUNK_next(r, &chunk));           /*  the spaces: empty      */
    CHECK_EQ_INT(chunk.is_empty, 1);
    CHECK_EQ_INT(chunk.has_code, 0);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.is_reader, 1);
    CHECK_EQ_INT(chunk.has_code, 1);
    CHUNK_close(r);

    /*  A comment ahead of real code does not make the chunk codeless.  */
    r = CHUNK_open_string("\"doc\" ^1!");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.has_code, 1);
    CHUNK_close(r);

    /*
     *  A doubled quote is the lexer's escape, not the reader's, so it must
     *  survive intact.  Un-doubling it here closes the string a quote early
     *  and swallows the remainder of the method.
     */
    r = CHUNK_open_string("f ^'it''s'!");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_STR(chunk.text, "f ^'it''s'");
    CHECK_EQ_INT(chunk.has_code, 1);
    CHUNK_close(r);

    /*  A quote of one kind inside the other is an ordinary character.  */
    r = CHUNK_open_string("\"don't stop\"!");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.has_code, 0);
    CHUNK_close(r);

    r = CHUNK_open_string("f ^'say \"hi\"'!");
    CHECK(r != NULL);
    CHECK(CHUNK_next(r, &chunk));
    CHECK_EQ_INT(chunk.has_code, 1);
    CHECK_EQ_STR(chunk.text, "f ^'say \"hi\"'");
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
    test_loops();
    test_cascades();
    test_block_argument_slots();
    test_negative_after_binary();
    test_context_size();

    return ST_TEST_END();
}

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Smalltalk-80 compiler.  See compiler.h for scope.
 *
 *  This is a single-pass recursive-descent compiler: it parses and emits at
 *  once, because Smalltalk's grammar needs no lookahead beyond one token and
 *  the bytecode set maps onto the grammar directly.  Jumps whose distance is
 *  not yet known are patched afterwards, which is the only backward step.
 */

#include "compiler.h"
#include "lexer.h"
#include "interp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEMPS       64
#define MAX_ARGS        16
#define MAX_BLOCK_DEPTH 16
/*
 *  A byte-array literal's size.
 *
 *  Raised from 1024 because Pharo embeds whole fonts as byte-array
 *  literals -- SourceSansProRegular>>fontContentsData is one method and one
 *  literal, and it is over a quarter of a megabyte.  Nothing about the
 *  format cares; this was only ever a scratch buffer.
 *
 *  Which is why it is on the HEAP.  Half a megabyte of automatic storage
 *  inside a recursive-descent parser is a stack overflow waiting for a
 *  deeply nested method, and it would arrive as a crash with nothing to
 *  read.
 */
#define MAX_BYTE_ARRAY  (1024 * 1024)
#define MAX_PRAGMAS     16
#define MAX_PRAGMA_ARGS 8

/*  Save and restore enough to compile a stretch of source a second time.  */
/*  The most names one method may declare, across every scope in it.  */
#define MAX_DECLS       192

typedef struct {
    st_lexer_state  lexer;
    st_token        token;
    unsigned        length;
    unsigned        literal_count;
    unsigned        name_count;
    /*
     *  Blocks are numbered as the parser meets them, so an abandoned parse
     *  must give its numbers back -- otherwise the two passes disagree about
     *  which scope a block is, and every frame index after it is wrong.
     */
    unsigned        block_seen;
    unsigned        decl_count;
    unsigned        decl_visible;
    unsigned        decl_seen;
} compiler_mark;

/*
 *  ----------  Scopes, for the closure dialect  ----------
 *
 *  A Blue Book block has no frame of its own: its arguments and temporaries
 *  live in the home method's, which is exactly why two activations of one
 *  block tread on each other.  A closure activation has a frame, so the
 *  compiler has to know which frame every name belongs to -- and that is a
 *  whole-method property, which is why this dialect needs two passes.
 *
 *  Each frame is laid out
 *
 *      [ arguments ][ copied values ][ vector ][ local temporaries ]
 *
 *  A name referenced from an inner scope and ASSIGNED anywhere cannot be
 *  copied by value, because the two scopes have to see each other's stores.
 *  It moves into a "vector" -- an Array in a frame slot -- and inner scopes
 *  copy the vector instead, so they share the variable rather than its
 *  value.  A name that is captured but never assigned is copied directly;
 *  that is the common case and it costs nothing.
 *
 *  The bookkeeping that makes this more than an afternoon: a block nested
 *  two deep that reads a grandparent's boxed name needs the grandparent's
 *  VECTOR, which means the block's parent must copy that vector too, even
 *  though the parent never mentions the name.  Needs therefore propagate up
 *  the scope tree, which is what record_need does.
 *
 *  Simplifying to one vector for the whole method would be wrong in a way
 *  that hides: an outer block's own captured temporaries would then be
 *  shared between that block's own activations.
 */

/*
 *  Blocks per method.  Thirty-two was generous for 1983 source and is not
 *  for a Metacello baseline, which is one method holding a spec for every
 *  package in a project -- four of Pharo's exceed it.  The bytecode limit
 *  that matters is elsewhere and unchanged: numCopied is four bits.
 */
#define MAX_SCOPES      256
#define MAX_COPIED      15      /*  numCopied is four bits in bytecode 143  */
#define MAX_NEEDS       512

typedef struct {
    char        name[64];
    unsigned    scope;
    int         is_argument;
    int         assigned;       /*  appears as an assignment target  */
    int         captured;       /*  read or written from an inner scope  */
    /*  Decided between the passes.  */
    int         remote;         /*  lives in its scope's vector  */
    unsigned    slot;           /*  frame slot, or slot within the vector  */
} var_decl;

typedef struct {
    int         is_vector;      /*  copying a scope's vector, not a value  */
    unsigned    which;          /*  scope id if is_vector, else decl index  */
    unsigned    slot;           /*  where it lands in this frame           */
} copied_item;

typedef struct {
    unsigned    parent;
    unsigned    argc;
    int         has_vector;
    unsigned    vector_size;
    unsigned    vector_slot;
    copied_item copied[MAX_COPIED];
    unsigned    copied_count;
    unsigned    locals;         /*  non-remote, non-argument names  */
    unsigned    frame_size;     /*  args + copied + vector + locals  */
} scope_info;

typedef struct {
    st_lexer                   *lx;
    st_token                    token;      /*  the current token  */
    const st_compile_context   *ctx;
    st_compiled_code           *out;

    /*
     *  Which language this is being compiled as.  Blue Book is the default
     *  and is byte-for-byte what it always was: the closure machinery below
     *  is reached only when the caller asks for it, so the 1983 library and
     *  the trace oracle cannot be affected by any of it.
     */
    int         dialect;
    int         pass;           /*  0 records names, 1 emits  */

    var_decl    decls[MAX_DECLS];
    unsigned    decl_count;
    /*
     *  How many of them are lexically in scope at this point.  See
     *  find_decl for why this is not simply decl_count.
     */
    unsigned    decl_visible;
    /*
     *  How many declarations the parser has MADE, as against how many are
     *  in scope.  The two differ after an inlined block, which puts its
     *  temporaries back out of scope without unmaking them.
     *
     *  Pass zero can use decl_count for this and pass one cannot -- the
     *  array is already built, so pass one only walks along it -- and
     *  advancing pass one's cursor by one where pass zero jumps to an
     *  absolute index is exactly the off-by-N that made a block argument
     *  resolve to a hoisted temporary of the same name.
     */
    unsigned    decl_seen;
    scope_info  scopes[MAX_SCOPES];
    unsigned    scope_count;
    unsigned    current_scope;
    unsigned    block_seen;     /*  how many real blocks so far, both passes */
    struct {
        unsigned    scope;
        unsigned    decl;
    }           needs[MAX_NEEDS];
    unsigned    need_count;

    /*
     *  Pragmas the method declared, other than the two primitive forms,
     *  which mean something to the compiler rather than to the image.
     */
    struct {
        char        keyword[64];
        st_oop      args[MAX_PRAGMA_ARGS];
        unsigned    argc;
    }           pragmas[MAX_PRAGMAS];
    unsigned    pragma_count;

    /*  Argument and temporary names, arguments first as the frame expects. */
    char        names[MAX_TEMPS][64];
    unsigned    name_count;
    unsigned    argument_count;

    /*
     *  The most names ever in scope at once.
     *
     *  name_count falls back when a block's arguments go out of scope, so it
     *  ends the method describing only the method's own temporaries -- while
     *  the frame has to be big enough for every slot any block ever used.
     *  The context holds the temporaries first and the working stack
     *  immediately above them, so undercounting does not merely waste room:
     *  the stack starts ON TOP of the block arguments, and a block storing
     *  its argument overwrites whatever the stack had put there.
     *
     *  VariableNode class>>initialize is where this surfaced.  It reads
     *  "encoder fillDict: ... mapping: ((1 to: n) collect: [:i | ...]) ...",
     *  and the block argument shared its slot with the first thing pushed
     *  for that send, which was the encoder.  The send went out with 32 --
     *  the last value of i -- as its receiver, so the compiler's own tables
     *  were never built and every method compiled inside the image came out
     *  in a larger dialect than the same source compiled in C.
     */
    unsigned    max_names;

    /*
     *  Where an inlined loop's nil answer was emitted, if the last thing
     *  emitted was one.  A loop in statement position has no value anyone
     *  wants, and pushing nil only to pop it off is two dead bytes that the
     *  1983 compiler does not emit -- see discard_statement_value.
     */
    size_t      loop_nil_end;

    /*
     *  Where a store that did NOT pop its value was emitted, if it was the
     *  last thing emitted.
     *
     *  An assignment leaves its value on the stack, because "b _ a _ 1" is
     *  legal and the inner one has to answer something.  In statement
     *  position nobody wants it, and the bytecode set has a store-and-pop
     *  for exactly that -- one byte where a store and a separate pop are
     *  three.  The 1983 compiler emits it; recording where the store began
     *  is what lets discard_statement_value go back and do the same.
     */
    size_t      store_at;
    size_t      store_end;
    int         store_kind;
    unsigned    store_index;
    st_oop      store_association;

    /*  Whether a super send was emitted, and so needs the class literal.  */
    int         used_super;

    /*
     *  Where the receiver of the outermost message just compiled ended.
     *
     *  A cascade sends to the receiver of the LAST message before the
     *  semicolon, not to the primary.  In
     *
     *      OrderedCollection new add: 1; add: 2
     *
     *  the cascade receiver is "OrderedCollection new", so add: 2 goes to
     *  the collection.  Marking after the primary instead sends it to the
     *  class, which answers something plausible and wrong.
     *
     *  Each level records its own receiver and assigns here after emitting
     *  its send, so the outermost assignment lands last -- inner calls,
     *  including the ones that compile arguments, have already returned.
     */
    compiler_mark receiver_mark;

    int         failed;
} st_compiler;

#define NO_LOOP_NIL     ((size_t) -1)
#define NO_STORE        ((size_t) -1)

/*  Which of the three store emitters left the value on the stack.  */
#define STORE_TEMPORARY         0
#define STORE_RECEIVER          1
#define STORE_LITERAL           2

/*  ----------  Diagnostics  ----------  */

static void
fail(st_compiler *c, const char *fmt, ...)
{
    va_list ap;

    if (c->failed)
        return;
    c->failed = 1;
    va_start(ap, fmt);
    vsnprintf(c->out->error, sizeof c->out->error, fmt, ap);
    va_end(ap);
    c->out->error_line = c->token.line;
}

/*  ----------  Token handling  ----------  */

static void
advance(st_compiler *c)
{
    if (c->failed)
        return;
    LEX_next(c->lx, &c->token);
    if (c->token.kind == ST_TOK_ERROR)
        fail(c, "%s", c->token.text);
}

static int
at(st_compiler *c, st_token_kind kind)
{
    return !c->failed && c->token.kind == kind;
}

static int
accept(st_compiler *c, st_token_kind kind)
{
    if (!at(c, kind))
        return 0;
    advance(c);
    return 1;
}

/*
 *  Accept the bar that closes a block's argument list.
 *
 *  "[ :index || segment | ... ]" writes that bar hard against the bar that
 *  opens the temporaries, and the lexer -- which cannot see the grammar --
 *  hands the pair over as one two-character binary selector.  Only the
 *  parser knows that a block's arguments have just ended and that what
 *  follows can only be bars, so the splitting belongs here: take the first
 *  and leave a bar token standing for the second.
 */
static int
accept_argument_bar(st_compiler *c)
{
    if (accept(c, ST_TOK_BAR))
        return 1;
    if (at(c, ST_TOK_BINARY) && strcmp(c->token.text, "||") == 0) {
        c->token.kind    = ST_TOK_BAR;
        c->token.text[0] = '|';
        c->token.text[1] = '\0';
        return 1;
    }
    return 0;
}

/*  ----------  Emission  ----------  */

static void
emit(st_compiler *c, uint8_t byte)
{
    if (c->failed)
        return;
    if (c->out->length >= sizeof c->out->bytecodes) {
        fail(c, "method is too long");
        return;
    }
    c->out->bytecodes[c->out->length++] = byte;
}

/*  Record a literal, reusing an identical one.  */
static unsigned
literal_index(st_compiler *c, st_oop value)
{
    unsigned    i;

    for (i = 0; i < c->out->literal_count; ++i) {
        if (c->out->literals[i] == value)
            return i;
    }
    if (c->out->literal_count >= 256) {
        fail(c, "too many literals");
        return 0;
    }
    c->out->literals[c->out->literal_count] = value;
    return c->out->literal_count++;
}

static void
emit_push_literal_constant(st_compiler *c, st_oop value)
{
    unsigned    index = literal_index(c, value);

    if (index < 32) {
        emit(c, (uint8_t) (32 + index));
    }  else  {
        emit(c, 128);
        emit(c, (uint8_t) (0x80 | (index & 63)));
    }
}

static void
emit_push_literal_variable(st_compiler *c, st_oop association)
{
    unsigned    index = literal_index(c, association);

    if (index < 32) {
        emit(c, (uint8_t) (64 + index));
    }  else  {
        emit(c, 128);
        emit(c, (uint8_t) (0xC0 | (index & 63)));
    }
}

static void
emit_push_temporary(st_compiler *c, unsigned index)
{
    if (index < 16)
        emit(c, (uint8_t) (16 + index));
    else {
        emit(c, 128);
        emit(c, (uint8_t) (0x40 | (index & 63)));
    }
}

static void
emit_push_receiver_variable(st_compiler *c, unsigned index)
{
    if (index < 16)
        emit(c, (uint8_t) index);
    else {
        emit(c, 128);
        emit(c, (uint8_t) (index & 63));
    }
}

/*  Remember a store that left its value, so a statement can take it back.  */
static void
note_store(st_compiler *c, size_t at, int kind, unsigned index, st_oop assoc)
{
    c->store_at          = at;
    c->store_end         = c->out->length;
    c->store_kind        = kind;
    c->store_index       = index;
    c->store_association = assoc;
}

/*  Store into a name held in a vector.  141 keeps the value, 142 pops it. */
static void
emit_store_remote(st_compiler *c, unsigned index, unsigned vector, int pop)
{
    emit(c, (uint8_t) (pop ? 142 : 141));
    emit(c, (uint8_t) index);
    emit(c, (uint8_t) vector);
}

static void
emit_store_temporary(st_compiler *c, unsigned index, int pop)
{
    size_t  at = c->out->length;

    if (pop && index < 8) {
        emit(c, (uint8_t) (104 + index));
        return;
    }
    emit(c, (uint8_t) (pop ? 130 : 129));
    emit(c, (uint8_t) (0x40 | (index & 63)));
    if (!pop)
        note_store(c, at, STORE_TEMPORARY, index, ST_NIL);
}

static void
emit_store_receiver_variable(st_compiler *c, unsigned index, int pop)
{
    size_t  at = c->out->length;

    if (pop && index < 8) {
        emit(c, (uint8_t) (96 + index));
        return;
    }
    emit(c, (uint8_t) (pop ? 130 : 129));
    emit(c, (uint8_t) (index & 63));
    if (!pop)
        note_store(c, at, STORE_RECEIVER, index, ST_NIL);
}

static void
emit_store_literal_variable(st_compiler *c, st_oop association, int pop)
{
    unsigned    index = literal_index(c, association);
    size_t      at = c->out->length;

    emit(c, (uint8_t) (pop ? 130 : 129));
    emit(c, (uint8_t) (0xC0 | (index & 63)));
    if (!pop)
        note_store(c, at, STORE_LITERAL, index, association);
}

/*
 *  The special selectors have one-byte sends.  Matching them is what makes
 *  compiled code compact, and it is required to match the 1983 bytecodes.
 */
static const char *const arithmetic_selectors[16] = {
    "+", "-", "<", ">", "<=", ">=", "=", "~=",
    "*", "/", "\\\\", "@", "bitShift:", "//", "bitAnd:", "bitOr:"
};

static const char *const special_selectors[16] = {
    "at:", "at:put:", "size", "next", "nextPut:", "atEnd", "==", "class",
    "blockCopy:", "value", "value:", "do:", "new", "new:", "x", "y"
};

static void
emit_send(st_compiler *c, const char *selector, unsigned argc, int to_super)
{
    unsigned    i;
    st_oop      symbol;
    unsigned    index;

    if (to_super)
        c->used_super = 1;
    if (!to_super) {
        for (i = 0; i < 16; ++i) {
            if (strcmp(selector, arithmetic_selectors[i]) == 0) {
                emit(c, (uint8_t) (176 + i));
                return;
            }
        }
        for (i = 0; i < 16; ++i) {
            if (strcmp(selector, special_selectors[i]) == 0) {
                emit(c, (uint8_t) (192 + i));
                return;
            }
        }
    }

    symbol = c->ctx->intern_symbol(selector, c->ctx->user);
    index  = literal_index(c, symbol);

    if (!to_super && argc <= 2 && index < 16) {
        emit(c, (uint8_t) (208 + argc * 16 + index));
        return;
    }
    if (argc <= 7 && index < 32) {
        emit(c, (uint8_t) (to_super ? 133 : 131));
        emit(c, (uint8_t) ((argc << 5) | index));
        return;
    }
    emit(c, (uint8_t) (to_super ? 134 : 132));
    emit(c, (uint8_t) argc);
    emit(c, (uint8_t) index);
}

/*
 *  Jumps.  Three forms, and picking the wrong one fails silently -- an
 *  unconditional jump where a branch belongs simply always takes the arm.
 *
 *      164..167   jump
 *      168..171   pop and jump if true
 *      172..175   pop and jump if false
 *
 *  The distance is unknown when the jump is emitted, so a placeholder goes
 *  down and is patched once the target is known.
 */
#define JUMP_ALWAYS     164
#define JUMP_IF_TRUE    168
#define JUMP_IF_FALSE   172

static unsigned
emit_jump_placeholder(st_compiler *c, uint8_t opcode)
{
    unsigned    at_byte = c->out->length;

    emit(c, opcode);
    emit(c, 0);
    return at_byte;
}

static void
patch_jump(st_compiler *c, unsigned at_byte)
{
    int32_t     distance;
    uint8_t     base;

    if (c->failed)
        return;
    /*
     *  A jump now lands where the code currently ends, so the byte before
     *  that end is no longer the last thing emitted in any sense that
     *  matters: it sits between a jump and its target.
     *
     *  discard_statement_value deletes a trailing "push nil" that an inlined
     *  loop pushed and nobody wanted, on the reasoning that the only jump
     *  reaching it is the loop's own exit, which lands on whatever follows
     *  either way.  That reasoning holds only until some OTHER jump is
     *  patched to just past it.  It happens whenever a loop is the last
     *  statement of an arm of an inlined ifTrue:ifFalse:, because the jump
     *  over the second arm is patched to exactly that point: deleting the
     *  nil afterwards left that jump pointing one byte past the end of the
     *  method, and execution ran off into the source-pointer trailer.
     *
     *  Number>>to:by:do: is written that way, so "5 to: 1 by: -1 do: [...]"
     *  read three bytes of a source position as a push and a send, and
     *  reported that nil did not understand a selector that was not a
     *  Symbol.  Anything reaching reverseDo: went the same way.
     */
    c->loop_nil_end = NO_LOOP_NIL;
    c->store_end    = NO_STORE;
    distance = (int32_t) c->out->length - (int32_t) (at_byte + 2);
    base     = (uint8_t) (c->out->bytecodes[at_byte] & 0xFC);

    if (base == JUMP_ALWAYS) {
        if (distance < -1024 || distance > 1023) {
            fail(c, "jump out of range");
            return;
        }
        c->out->bytecodes[at_byte] =
            (uint8_t) (JUMP_ALWAYS + (distance >> 8));
        c->out->bytecodes[at_byte + 1] = (uint8_t) (distance & 255);
        return;
    }
    /*  A conditional jump only ever goes forward here.  */
    if (distance < 0 || distance > 1023) {
        fail(c, "conditional jump out of range");
        return;
    }
    c->out->bytecodes[at_byte]     = (uint8_t) (base + (distance >> 8));
    c->out->bytecodes[at_byte + 1] = (uint8_t) (distance & 255);
}

/*
 *  A backward jump, for loops.  The distance is negative and known.
 *
 *  The unconditional jump encodes its offset as (opcode - 164) * 256 plus
 *  the following byte, so the high part is a SIGNED three-bit quantity and
 *  must not be masked -- masking turns -1 into 7, which is opcode 171, a
 *  pop-and-jump-on-true.  A loop compiled that way falls straight out and
 *  the body never runs.
 */
static void
emit_jump_back_to(st_compiler *c, unsigned target)
{
    int32_t     distance = (int32_t) target - (int32_t) (c->out->length + 2);
    int32_t     high     = distance >> 8;

    if (high < -4 || high > 3) {
        fail(c, "loop is too long to jump back over");
        return;
    }
    emit(c, (uint8_t) (JUMP_ALWAYS + high));
    emit(c, (uint8_t) (distance & 255));
}

/*  ----------  Variable resolution  ----------  */

typedef enum {
    VAR_NONE, VAR_TEMPORARY, VAR_INSTANCE, VAR_GLOBAL,
    VAR_SELF, VAR_SUPER, VAR_THIS_CONTEXT,
    VAR_NIL, VAR_TRUE, VAR_FALSE,
    /*
     *  A temporary held in a vector rather than a frame slot, because some
     *  inner block shares it.  `index` is the slot within the vector and
     *  `vector` is the frame slot holding the vector.  Closure dialect only.
     */
    VAR_REMOTE
} var_kind;

typedef struct {
    var_kind    kind;
    unsigned    index;
    unsigned    vector;
    st_oop      association;
} var_ref;

/*  ----------  Scope analysis  ----------  */

/*  Is `inner` `outer`, or nested inside it?  */
static int
scope_within(const st_compiler *c, unsigned inner, unsigned outer)
{
    for (;;) {
        if (inner == outer)
            return 1;
        if (inner == 0)
            return 0;
        inner = c->scopes[inner].parent;
    }
}

/*
 *  The innermost declaration of `name` visible from the current scope, or
 *  -1.  Backwards, so an inner declaration shadows an outer one of the same
 *  name -- which is the rule the flat Blue Book frame also follows, for the
 *  same reason.
 */
static long
find_decl(const st_compiler *c, const char *name)
{
    unsigned    i;

    /*
     *  From decl_visible, not decl_count.
     *
     *  The two passes have to agree on WHICH declaration a name means, and
     *  without this they do not.  An inlined block's temporaries live in
     *  the enclosing frame -- there is no other frame for them to live in
     *  -- so two sibling inlined blocks each declaring "t" put two
     *  declarations named "t" in one scope.  Searching the whole array
     *  backwards, pass zero (where only the first exists yet) finds the
     *  first and pass one finds the second.  Everything downstream is then
     *  computed about one declaration and emitted about the other: if a
     *  real closure inside the first block captures t, pass zero makes the
     *  FIRST t remote and pass one emits a plain frame access to the
     *  second.  That is a wrong answer with nothing to see.
     *
     *  decl_visible advances on every declaration in both passes and is
     *  saved and restored around an inlined block, so both passes see the
     *  same names in the same order at the same points.
     */
    for (i = c->decl_visible; i-- > 0; ) {
        if (strcmp(c->decls[i].name, name) == 0
         && scope_within(c, c->current_scope, c->decls[i].scope))
            return (long) i;
    }
    return -1;
}

/*
 *  Declare a name in the current scope.
 *
 *  Called in BOTH passes.  Pass zero builds the array; pass one only walks
 *  the cursor along it, in the same order, so that a name resolves to the
 *  same declaration in both.
 */
static void
declare(st_compiler *c, const char *name, int is_argument)
{
    var_decl   *d;

    if (c->pass != 0) {
        if (c->decl_seen < c->decl_count)
            ++c->decl_seen;
        c->decl_visible = c->decl_seen;
        return;
    }
    if (c->decl_count >= MAX_DECLS) {
        fail(c, "too many names in one method");
        return;
    }
    d = &c->decls[c->decl_count++];
    memset(d, 0, sizeof *d);
    snprintf(d->name, sizeof d->name, "%.63s", name);
    d->scope       = c->current_scope;
    d->is_argument = is_argument;
    if (is_argument)
        ++c->scopes[c->current_scope].argc;
    c->decl_seen    = c->decl_count;
    c->decl_visible = c->decl_count;
}

/*
 *  Record that `scope` needs decl `decl`, which is declared further out.
 *
 *  Every scope between the two needs it as well: a block can only copy from
 *  the frame it is being created in, so an item has to be handed down one
 *  level at a time.  This is the propagation that makes the analysis a tree
 *  walk rather than a lookup.
 */
static void
record_need(st_compiler *c, unsigned scope, unsigned decl)
{
    unsigned    declaring = c->decls[decl].scope;
    unsigned    s;

    for (s = scope; s != declaring && s != 0; s = c->scopes[s].parent) {
        unsigned    i;
        int         already = 0;

        for (i = 0; i < c->need_count; ++i) {
            if (c->needs[i].scope == s && c->needs[i].decl == decl) {
                already = 1;
                break;
            }
        }
        if (already)
            continue;
        if (c->need_count >= MAX_NEEDS) {
            fail(c, "too many captured names in one method");
            return;
        }
        c->needs[c->need_count].scope = s;
        c->needs[c->need_count].decl  = decl;
        ++c->need_count;
    }
}

/*  Note a use of decl `d` from the current scope.  */
static void
note_use(st_compiler *c, long d, int assigning)
{
    var_decl   *decl;

    if (d < 0)
        return;
    decl = &c->decls[d];
    if (assigning)
        decl->assigned = 1;
    if (decl->scope != c->current_scope) {
        decl->captured = 1;
        record_need(c, c->current_scope, (unsigned) d);
    }
}

/*
 *  Between the passes: decide what is remote and lay every frame out.
 *
 *  A name is remote when an inner scope shares it AND something assigns it.
 *  Captured-but-never-assigned is copied by value, which is the common case
 *  -- a block reading an enclosing temporary costs one slot and no
 *  indirection.
 */
static void
plan_frames(st_compiler *c)
{
    unsigned    i;
    unsigned    s;

    for (i = 0; i < c->decl_count; ++i)
        c->decls[i].remote = c->decls[i].captured && c->decls[i].assigned;

    for (s = 0; s < c->scope_count; ++s) {
        scope_info *scope = &c->scopes[s];
        unsigned    next;

        scope->copied_count = 0;
        scope->vector_size  = 0;
        scope->has_vector   = 0;
        scope->locals       = 0;

        /*  Slots within this scope's vector, in declaration order.  */
        for (i = 0; i < c->decl_count; ++i) {
            if (c->decls[i].scope == s && c->decls[i].remote)
                c->decls[i].slot = scope->vector_size++;
        }
        scope->has_vector = scope->vector_size > 0;

        /*  Arguments first, in declaration order, as an activation fills them. */
        next = 0;
        for (i = 0; i < c->decl_count; ++i) {
            if (c->decls[i].scope == s && c->decls[i].is_argument
             && !c->decls[i].remote)
                c->decls[i].slot = next;
            if (c->decls[i].scope == s && c->decls[i].is_argument)
                ++next;
        }
        /*
         *  A remote argument still ARRIVES in a frame slot -- the activation
         *  puts it there -- and the prologue moves it into the vector.  So
         *  the slot is reserved either way, and remembered separately.
         */
        next = scope->argc;

        /*  Then the copied values, one slot each.  */
        for (i = 0; i < c->need_count; ++i) {
            unsigned    decl;
            unsigned    from;
            int         is_vector;
            unsigned    which;
            unsigned    k;
            int         already = 0;

            if (c->needs[i].scope != s)
                continue;
            decl      = c->needs[i].decl;
            from      = c->decls[decl].scope;
            is_vector = c->decls[decl].remote;
            which     = is_vector ? from : decl;

            for (k = 0; k < scope->copied_count; ++k) {
                if (scope->copied[k].is_vector == is_vector
                 && scope->copied[k].which == which) {
                    already = 1;
                    break;
                }
            }
            if (already)
                continue;
            if (scope->copied_count >= MAX_COPIED) {
                fail(c, "a block captures more than %d names", MAX_COPIED);
                return;
            }
            scope->copied[scope->copied_count].is_vector = is_vector;
            scope->copied[scope->copied_count].which     = which;
            scope->copied[scope->copied_count].slot      = next++;
            ++scope->copied_count;
        }

        /*  Then this scope's own vector, if it has one.  */
        if (scope->has_vector)
            scope->vector_slot = next++;

        /*  Then the local temporaries that stayed in the frame.  */
        for (i = 0; i < c->decl_count; ++i) {
            if (c->decls[i].scope == s && !c->decls[i].is_argument
             && !c->decls[i].remote) {
                c->decls[i].slot = next++;
                ++scope->locals;
            }
        }
        scope->frame_size = next;
    }
}

/*  Where scope `s` keeps scope `from`'s vector, as a frame slot of s.  */
static int
copied_vector_slot(const st_compiler *c, unsigned s, unsigned from,
                   unsigned *slot)
{
    unsigned    k;

    if (s == from) {
        if (!c->scopes[s].has_vector)
            return 0;
        *slot = c->scopes[s].vector_slot;
        return 1;
    }
    for (k = 0; k < c->scopes[s].copied_count; ++k) {
        if (c->scopes[s].copied[k].is_vector
         && c->scopes[s].copied[k].which == from) {
            *slot = c->scopes[s].copied[k].slot;
            return 1;
        }
    }
    return 0;
}

/*  Where scope `s` keeps decl `decl`'s copied value, as a frame slot of s. */
static int
copied_value_slot(const st_compiler *c, unsigned s, unsigned decl,
                  unsigned *slot)
{
    unsigned    k;

    for (k = 0; k < c->scopes[s].copied_count; ++k) {
        if (!c->scopes[s].copied[k].is_vector
         && c->scopes[s].copied[k].which == decl) {
            *slot = c->scopes[s].copied[k].slot;
            return 1;
        }
    }
    return 0;
}

/*
 *  Resolve a name in the closure dialect.  Answers 0 if it is not a
 *  temporary at all, and the caller falls through to instance variables and
 *  globals exactly as before.
 */
static int
resolve_scoped(st_compiler *c, const char *name, int assigning, var_ref *out)
{
    long        d = find_decl(c, name);
    var_decl   *decl;
    unsigned    slot;

    if (d < 0)
        return 0;
    note_use(c, d, assigning);
    decl = &c->decls[d];

    if (c->pass == 0) {
        /*
         *  Nothing is laid out yet, so answer something structurally right
         *  and numerically meaningless.  The bytes of this pass are thrown
         *  away; only the names it records are kept.
         */
        out->kind  = VAR_TEMPORARY;
        out->index = 0;
        return 1;
    }
    if (decl->remote) {
        if (!copied_vector_slot(c, c->current_scope, decl->scope, &slot)) {
            fail(c, "'%s' is shared but its vector is not in scope", name);
            return 1;
        }
        out->kind   = VAR_REMOTE;
        out->index  = decl->slot;
        out->vector = slot;
        return 1;
    }
    if (decl->scope == c->current_scope) {
        out->kind  = VAR_TEMPORARY;
        out->index = decl->slot;
        return 1;
    }
    if (!copied_value_slot(c, c->current_scope, (unsigned) d, &slot)) {
        fail(c, "'%s' is used in a block that did not capture it", name);
        return 1;
    }
    out->kind  = VAR_TEMPORARY;
    out->index = slot;
    return 1;
}

static var_ref
resolve_for(st_compiler *c, const char *name, int assigning)
{
    var_ref     v;
    unsigned    i;

    memset(&v, 0, sizeof v);
    if (strcmp(name, "self") == 0)        { v.kind = VAR_SELF;         return v; }
    if (strcmp(name, "super") == 0)       { v.kind = VAR_SUPER;        return v; }
    if (strcmp(name, "thisContext") == 0) { v.kind = VAR_THIS_CONTEXT; return v; }
    if (strcmp(name, "nil") == 0)         { v.kind = VAR_NIL;          return v; }
    if (strcmp(name, "true") == 0)        { v.kind = VAR_TRUE;         return v; }
    if (strcmp(name, "false") == 0)       { v.kind = VAR_FALSE;        return v; }

    if (c->dialect == ST_DIALECT_CLOSURES) {
        if (resolve_scoped(c, name, assigning, &v))
            return v;
        /*  Fall through to instance variables and globals, as below.  */
        for (i = 0; i < c->ctx->instance_variable_count; ++i) {
            if (strcmp(c->ctx->instance_variables[i], name) == 0) {
                v.kind  = VAR_INSTANCE;
                v.index = i;
                return v;
            }
        }
        if (c->ctx->lookup_global) {
            st_oop  association = c->ctx->lookup_global(name, c->ctx->user);

            if (association != ST_NIL && association != ST_OOP_INVALID) {
                v.kind        = VAR_GLOBAL;
                v.association = association;
                return v;
            }
        }
        v.kind = VAR_NONE;
        return v;
    }

    /*
     *  Arguments and temporaries shadow instance variables, which shadow
     *  globals -- innermost scope first.
     *
     *  Innermost means LAST here.  Names are appended to one frame as their
     *  scopes open: the method's arguments, then its temporaries, then a
     *  block's arguments, then a nested block's.  So the newest declaration
     *  of a name is the innermost one, and the search has to run backwards.
     *
     *  Running it forwards is wrong in a way that stays hidden until a block
     *  argument happens to share a name with a method temporary.  Then the
     *  block's uses of the name read the METHOD's slot, which nothing ever
     *  writes, while the loop faithfully stores each value into the block's.
     *  Symbol class>>hasInterned:ifTrue: is exactly that shape --
     *
     *      | v i ascii |
     *      ...
     *      1 to: v size do: [:i | ... (v at: i) ... ]
     *
     *  -- so every element access became "v at: nil", and interning any
     *  symbol whose bucket was not empty walked into the error path instead
     *  of answering.
     */
    for (i = c->name_count; i-- > 0; ) {
        if (strcmp(c->names[i], name) == 0) {
            v.kind  = VAR_TEMPORARY;
            v.index = i;
            return v;
        }
    }
    for (i = 0; i < c->ctx->instance_variable_count; ++i) {
        if (strcmp(c->ctx->instance_variables[i], name) == 0) {
            v.kind  = VAR_INSTANCE;
            v.index = i;
            return v;
        }
    }
    if (c->ctx->lookup_global) {
        st_oop  association = c->ctx->lookup_global(name, c->ctx->user);

        if (association != ST_NIL && association != ST_OOP_INVALID) {
            v.kind        = VAR_GLOBAL;
            v.association = association;
            return v;
        }
    }
    v.kind = VAR_NONE;
    return v;
}

static var_ref
resolve(st_compiler *c, const char *name)
{
    return resolve_for(c, name, 0);
}

static void
emit_push_variable(st_compiler *c, const var_ref *v, const char *name)
{
    switch (v->kind) {
    case VAR_REMOTE:
        emit(c, 140);
        emit(c, (uint8_t) v->index);
        emit(c, (uint8_t) v->vector);
        break;
    case VAR_SELF:
    case VAR_SUPER:      emit(c, 112); break;
    case VAR_TRUE:       emit(c, 113); break;
    case VAR_FALSE:      emit(c, 114); break;
    case VAR_NIL:        emit(c, 115); break;
    case VAR_THIS_CONTEXT: emit(c, 137); break;
    case VAR_TEMPORARY:  emit_push_temporary(c, v->index); break;
    case VAR_INSTANCE:   emit_push_receiver_variable(c, v->index); break;
    case VAR_GLOBAL:     emit_push_literal_variable(c, v->association); break;
    default:
        fail(c, "undeclared variable '%s'", name);
        break;
    }
}

/*  ----------  Expressions  ----------  */

static void compile_expression(st_compiler *c);
static void compile_statements(st_compiler *c, int inside_block);
static void mark(st_compiler *c, compiler_mark *m);
static void rewind_to(st_compiler *c, const compiler_mark *m);
static void compile_closure(st_compiler *c);
static void emit_push_temporary(st_compiler *c, unsigned index);

/*  Push a small integer using the shortest form available.  */
static void
emit_push_integer(st_compiler *c, int64_t value)
{
    if (value >= -1 && value <= 2) {
        emit(c, (uint8_t) (116 + (value + 1)));
        return;
    }
    if (!OM_int_fits((st_int) value)) {
        emit_push_literal_constant(c,
            c->ctx->make_large_integer((int64_t) value, c->ctx->user));
        return;
    }
    emit_push_literal_constant(c, OM_int_oop((st_int) value));
}

static st_oop parse_byte_array(st_compiler *c);

/*
 *  A literal array.  Bare words inside are symbols rather than variables,
 *  and nested parentheses are nested arrays.
 */
static st_oop
parse_literal_array(st_compiler *c)
{
    st_oop      elements[256];
    unsigned    count = 0;

    while (!c->failed && !at(c, ST_TOK_RPAREN) && !at(c, ST_TOK_END)) {
        st_oop  element = ST_NIL;

        switch (c->token.kind) {
        case ST_TOK_INTEGER:
            element = OM_int_fits((st_int) c->token.integer)
                        ? OM_int_oop((st_int) c->token.integer)
                        : c->ctx->make_large_integer(c->token.integer,
                                                     c->ctx->user);
            advance(c);
            break;
        case ST_TOK_FLOAT:
            element = c->ctx->make_float(c->token.real, c->ctx->user);
            advance(c);
            break;
        case ST_TOK_STRING:
            element = c->ctx->make_string(c->token.text, c->ctx->user);
            advance(c);
            break;
        case ST_TOK_CHARACTER:
            /*  Characters live in a table the image owns.  */
            element = c->ctx->make_character
                        ? c->ctx->make_character((unsigned) c->token.integer,
                                                 c->ctx->user)
                        : OM_fetch_pointer((uint32_t) c->token.integer,
                                           ST_CHARACTER_TABLE);
            advance(c);
            break;
        case ST_TOK_KEYWORD: {
            /*
             *  Keyword parts that touch are one selector.
             *
             *  The lexer hands back a token per keyword, because that is
             *  what a message send needs; inside a literal array they have
             *  to be joined again, since "#(ifTrue:ifFalse:)" holds one
             *  symbol and "#(ifTrue: ifFalse:)" holds two, and nothing but
             *  the space between them says which.
             *
             *  Splitting them silently produced Symbols that were spelled
             *  right individually and were not the selectors meant.
             *  MessageNode class>>initialize builds MacroSelectors from a
             *  literal array of exactly this shape, and the compiler inside
             *  the image looks a selector up in it by identity -- so with
             *  the array holding #ifTrue: and #ifFalse: where
             *  #ifTrue:ifFalse: belonged, the lookup answered nothing,
             *  nothing was ever recognised as a macro, and the image
             *  compiled every conditional and every loop as a real message
             *  send with real blocks.
             */
            char        joined[256];
            size_t      n = 0;

            for (;;) {
                const char *part = c->token.text;

                while (*part && n + 1 < sizeof joined)
                    joined[n++] = *part++;
                advance(c);
                if (!at(c, ST_TOK_KEYWORD) || c->token.after_space)
                    break;
            }
            joined[n] = '\0';
            element = c->ctx->intern_symbol(joined, c->ctx->user);
            break;
        }
        case ST_TOK_SYMBOL:
        case ST_TOK_IDENTIFIER:
        case ST_TOK_BINARY:
            element = c->ctx->intern_symbol(c->token.text, c->ctx->user);
            advance(c);
            break;
        /*
         *  Inside #( ) everything that is not a literal is a SYMBOL, and
         *  that includes the punctuation the grammar uses elsewhere.
         *
         *  Pharo writes #( double sx; double shx; ) as a field descriptor
         *  and means six symbols, two of which are #; -- seventy-nine
         *  methods of its graphics code do.  Brackets get in by accident
         *  rather than design, from source that meant to close the array
         *  earlier, and Pharo reads them as symbols too rather than
         *  refusing the file.
         *
         *  ( and ) keep their meanings: a nested array, and the end.
         */
        case ST_TOK_SEMICOLON:
        case ST_TOK_BAR:
        case ST_TOK_LBRACKET:
        case ST_TOK_RBRACKET:
        case ST_TOK_LBRACE:
        case ST_TOK_RBRACE:
        case ST_TOK_COLON:
        case ST_TOK_PERIOD:
        case ST_TOK_RETURN: {
            static const char *const punctuation[] = {
                NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                NULL, NULL, NULL, ";", ".", "|", ":", NULL, NULL, "[", "]",
                "{", "}", NULL
            };
            const char *text = c->token.kind == ST_TOK_RETURN
                                ? "^" : punctuation[c->token.kind];

            element = c->ctx->intern_symbol(text ? text : "?", c->ctx->user);
            advance(c);
            break;
        }
        case ST_TOK_LPAREN:
        case ST_TOK_ARRAY_OPEN:
            advance(c);
            element = parse_literal_array(c);
            break;
        case ST_TOK_BYTE_ARRAY_OPEN:
            /*  #(#[1 2] #[3 4]) -- a byte array nests like any literal.  */
            element = parse_byte_array(c);
            break;
        default:
            fail(c, "unexpected token in a literal array");
            return ST_NIL;
        }
        if (count < 256)
            elements[count++] = element;
    }
    accept(c, ST_TOK_RPAREN);
    return c->ctx->make_array(elements, count, c->ctx->user);
}

/*
 *  A literal ByteArray, #[1 2 3].  The current token is the opening #[.
 *
 *  Every element must be an integer that fits in a byte.  Anything else is
 *  reported rather than coerced or skipped: a byte array whose elements were
 *  quietly dropped would be the wrong length, and length is the only thing
 *  the reader of a byte array has to go on.
 */
static st_oop
parse_byte_array(st_compiler *c)
{
    uint8_t    *bytes = (uint8_t *) malloc(MAX_BYTE_ARRAY);
    unsigned    count = 0;
    st_oop      result;

    if (!bytes) {
        fail(c, "out of memory for a byte array literal");
        return ST_NIL;
    }
    advance(c);
    result = ST_NIL;
    while (!c->failed && !at(c, ST_TOK_RBRACKET) && !at(c, ST_TOK_END)) {
        if (!at(c, ST_TOK_INTEGER)) {
            fail(c, "a byte array holds whole numbers 0 to 255");
            goto done;
        }
        if (c->token.integer < 0 || c->token.integer > 255) {
            fail(c, "%lld does not fit in a byte",
                 (long long) c->token.integer);
            goto done;
        }
        if (count >= MAX_BYTE_ARRAY) {
            fail(c, "a byte array literal holds at most %u bytes",
                 (unsigned) MAX_BYTE_ARRAY);
            goto done;
        }
        bytes[count++] = (uint8_t) c->token.integer;
        advance(c);
    }
    if (!accept(c, ST_TOK_RBRACKET)) {
        fail(c, "a byte array must end with ]");
        goto done;
    }
    if (c->ctx->make_byte_array)
        result = c->ctx->make_byte_array(bytes, count, c->ctx->user);
done:
    free(bytes);
    return result;
}

/*
 *  ----------  Pragmas  ----------
 *
 *  The Blue Book has exactly one, <primitive: 60>, and the lexer used to
 *  scan it as a single token.  Squeak generalised the notation to arbitrary
 *  annotations, and Pharo source is full of them, so they are recognised
 *  here instead -- in the parser, which knows that the only place a pragma
 *  can appear is between the temporaries and the first statement.
 *
 *  Everything but the two primitive forms is parsed and discarded.  Keeping
 *  them would need a CompiledMethod with somewhere to put them, which in
 *  Pharo is an AdditionalMethodState in the literal frame; that is its own
 *  piece of work and it belongs with the rest of the object model.  Parsing
 *  and discarding is what unblocks compiling the source.
 */

typedef struct {
    st_oop      value;
    int         is_integer;
    int64_t     integer;
    int         is_string;
    /*
     *  A bare identifier, which only <primitive: N error: ec> uses: the
     *  name is not a value, it is a temporary the pragma DECLARES.
     */
    int         is_identifier;
    char        text[256];
} pragma_arg;

/*  One literal argument of a pragma.  Answers 0 if the token is not one.  */
static int
pragma_literal(st_compiler *c, pragma_arg *out)
{
    memset(out, 0, sizeof *out);
    out->value = ST_NIL;

    switch (c->token.kind) {
    case ST_TOK_INTEGER:
        out->is_integer = 1;
        out->integer    = c->token.integer;
        out->value      = OM_int_fits((st_int) c->token.integer)
                            ? OM_int_oop((st_int) c->token.integer)
                            : c->ctx->make_large_integer(c->token.integer,
                                                         c->ctx->user);
        break;
    case ST_TOK_FLOAT:
        out->value = c->ctx->make_float(c->token.real, c->ctx->user);
        break;
    case ST_TOK_STRING:
        out->is_string = 1;
        snprintf(out->text, sizeof out->text, "%s", c->token.text);
        out->value = c->ctx->make_string(c->token.text, c->ctx->user);
        break;
    case ST_TOK_SYMBOL:
        snprintf(out->text, sizeof out->text, "%s", c->token.text);
        out->value = c->ctx->intern_symbol(c->token.text, c->ctx->user);
        break;
    case ST_TOK_CHARACTER:
        out->value = c->ctx->make_character
                        ? c->ctx->make_character((unsigned) c->token.integer,
                                                 c->ctx->user)
                        : OM_fetch_pointer((uint32_t) c->token.integer,
                                           ST_CHARACTER_TABLE);
        break;
    case ST_TOK_IDENTIFIER:
        /*  true, false and nil are the only bare words a pragma may hold.  */
        if (strcmp(c->token.text, "true") == 0)
            out->value = ST_TRUE;
        else if (strcmp(c->token.text, "false") == 0)
            out->value = ST_FALSE;
        else if (strcmp(c->token.text, "nil") == 0)
            out->value = ST_NIL;
        else
            return 0;
        break;
    case ST_TOK_ARRAY_OPEN:
        advance(c);
        out->value = parse_literal_array(c);
        return !c->failed;
    case ST_TOK_BYTE_ARRAY_OPEN:
        out->value = parse_byte_array(c);
        return !c->failed;
    default:
        return 0;
    }
    advance(c);
    return 1;
}

/*
 *  Act on a pragma that parsed.  Only the two primitive forms mean anything
 *  to this compiler; the rest are accepted and dropped.
 */
static void
apply_pragma(st_compiler *c, const char *selector,
             const pragma_arg *args, unsigned argc)
{
    if (strcmp(selector, "primitive:") == 0 && argc == 1
     && args[0].is_integer) {
        if (args[0].integer < 1 || args[0].integer > 65535) {
            fail(c, "primitive number %lld is out of range",
                 (long long) args[0].integer);
            return;
        }
        c->out->primitive = (unsigned) args[0].integer;
        /*
         *  Eight bits in the header extension.  A larger number is recorded
         *  and not written: see primitive_encodable in compiler.h for why
         *  that is the same thing as a primitive this VM does not have.
         */
        c->out->primitive_encodable = args[0].integer <= 255;
        return;
    }
    /*
     *  <primitive: N error: ec> -- Pharo's error-code form.
     *
     *  The second argument is not a value: it names a TEMPORARY that the
     *  VM fills in with why the primitive failed, so the fallback body can
     *  tell "insufficient object memory" from "bad argument".  This VM
     *  sets no error codes, so the temporary stays nil -- which is exactly
     *  right, because every one of those bodies tests the code against a
     *  specific symbol and takes the general path when it does not match.
     *  The method behaves as it would on a VM that failed for a reason it
     *  declined to name.
     */
    if (strcmp(selector, "primitive:error:") == 0 && argc == 2
     && args[0].is_integer && args[1].is_identifier) {
        if (args[0].integer < 1 || args[0].integer > 65535) {
            fail(c, "primitive number %lld is out of range",
                 (long long) args[0].integer);
            return;
        }
        c->out->primitive = (unsigned) args[0].integer;
        c->out->primitive_encodable = args[0].integer <= 255;
        if (c->name_count < MAX_TEMPS) {
            snprintf(c->names[c->name_count], 64, "%.63s", args[1].text);
            ++c->name_count;
            if (c->name_count > c->max_names)
                c->max_names = c->name_count;
        }
        if (c->dialect == ST_DIALECT_CLOSURES)
            declare(c, args[1].text, 0);
        return;
    }
    if (strcmp(selector, "primitive:module:") == 0 && argc == 2
     && args[0].is_string && args[1].is_string) {
        /*
         *  A named primitive, <primitive: 'fn' module: 'Mod'>.
         *
         *  Squeak's encoding, which ported source depends on by number:
         *  primitive index 117, and the method's FIRST literal is the
         *  descriptor {module. function. sessionID. address}.  The last two
         *  are the VM's own scratch and start at zero.
         *
         *  "First" is the load-bearing word.  Pragmas are parsed before any
         *  statement, so the literal frame is still empty and the descriptor
         *  necessarily lands at index 0 -- but a later change that interned
         *  something earlier would move it silently, so the check is here
         *  rather than in a comment.
         */
        st_oop      descriptor[4];
        st_oop      array;

        if (c->out->literal_count != 0) {
            fail(c, "a named primitive's descriptor must be the first "
                    "literal, and something is already there");
            return;
        }
        descriptor[0] = args[1].value;          /*  module    */
        descriptor[1] = args[0].value;          /*  function  */
        descriptor[2] = OM_int_oop(0);
        descriptor[3] = OM_int_oop(0);
        array = c->ctx->make_array(descriptor, 4, c->ctx->user);
        if (literal_index(c, array) != 0) {
            fail(c, "a named primitive's descriptor must be the first "
                    "literal");
            return;
        }
        c->out->primitive = 117;
        c->out->primitive_encodable = 1;
        snprintf(c->out->primitive_name, sizeof c->out->primitive_name,
                 "%.63s", args[0].text);
        snprintf(c->out->primitive_module, sizeof c->out->primitive_module,
                 "%.63s", args[1].text);
        return;
    }
    /*
     *  Any other pragma means nothing to the compiler and something to the
     *  image, so it is kept rather than dropped.  <shared: #serialize> is
     *  the one the parallel-safety audit is going to want; <test> and
     *  <deprecated:> are what ported code declares.
     */
    if (c->pragma_count < MAX_PRAGMAS) {
        unsigned    i;
        unsigned    slot = c->pragma_count++;

        snprintf(c->pragmas[slot].keyword, sizeof c->pragmas[slot].keyword,
                 "%.63s", selector);
        c->pragmas[slot].argc = (argc < MAX_PRAGMA_ARGS) ? argc
                                                         : MAX_PRAGMA_ARGS;
        for (i = 0; i < c->pragmas[slot].argc; ++i)
            c->pragmas[slot].args[i] = args[i].value;
    }
}

/*
 *  Read one pragma.  The current token is the opening '<'.  Answers 1 if a
 *  well-formed pragma was consumed and 0 if this is not one, in which case
 *  the caller rewinds and '<' goes back to being a binary selector.
 */
static int
parse_pragma(st_compiler *c)
{
    char        selector[256];
    pragma_arg  args[8];
    unsigned    argc = 0;
    size_t      n = 0;

    advance(c);                                 /*  past '<'  */
    selector[0] = '\0';

    if (at(c, ST_TOK_IDENTIFIER)) {
        snprintf(selector, sizeof selector, "%s", c->token.text);
        advance(c);
    } else if (at(c, ST_TOK_BINARY)) {
        snprintf(selector, sizeof selector, "%s", c->token.text);
        advance(c);
        if (argc >= 8 || !pragma_literal(c, &args[argc]))
            return 0;
        ++argc;
    } else if (at(c, ST_TOK_KEYWORD)) {
        while (at(c, ST_TOK_KEYWORD)) {
            const char *part = c->token.text;

            while (*part && n + 1 < sizeof selector)
                selector[n++] = *part++;
            selector[n] = '\0';
            advance(c);
            if (argc >= 8)
                return 0;
            /*
             *  An identifier where a literal was expected is the
             *  error-code form: <primitive: 148 error: ec>.  Nothing else
             *  in either dialect writes a bare name in a pragma, so taking
             *  one here costs nothing and is what lets Pharo's Kernel --
             *  where nine methods use it -- compile at all.
             */
            if (at(c, ST_TOK_IDENTIFIER)) {
                memset(&args[argc], 0, sizeof args[argc]);
                args[argc].is_identifier = 1;
                snprintf(args[argc].text, sizeof args[argc].text, "%s",
                         c->token.text);
                advance(c);
            }  else if (!pragma_literal(c, &args[argc])) {
                return 0;
            }
            ++argc;
        }
    } else {
        return 0;
    }

    /*
     *  The closing '>'.  It has to be a bar '>' and nothing else: binary
     *  selectors are greedy up to two characters, so a '>' immediately
     *  followed by another binary character arrives as one token and this is
     *  not the pragma it looks like.  Answering 0 rewinds, which is the
     *  right outcome -- better a compile error naming the line than a pragma
     *  that swallowed the character after it.
     */
    if (!at(c, ST_TOK_BINARY) || strcmp(c->token.text, ">") != 0)
        return 0;
    advance(c);
    apply_pragma(c, selector, args, argc);
    return !c->failed;
}

/*
 *  ----------  A block, in the closure dialect  ----------
 *
 *  The current token is '['.  Emits
 *
 *      <push each copied value from this frame>
 *      143 (numCopied<<4 | numArgs) sizeHi sizeLo
 *      <the block's prologue>
 *      <its body>
 *      125
 *
 *  and nothing else: 143 skips its own body, so there is no jump around it
 *  the way blockCopy: needs one.
 *
 *  Both passes come through here.  The first is looking only for names, so
 *  it emits a shape rather than the right bytes; the second has the frames
 *  laid out and emits for real.  Running the same parser twice is what keeps
 *  the two in step -- an independent analysis would have to re-derive which
 *  blocks are real blocks and which are inlined conditionals, and stay in
 *  agreement about it forever.
 */
static void
compile_closure(st_compiler *c)
{
    unsigned        outer_scope = c->current_scope;
    unsigned        scope;
    scope_info     *info;
    unsigned        argc = 0;
    size_t          size_at;
    size_t          body_start;
    unsigned        i;

    /*
     *  Blocks are numbered in the order the parser meets them, which is the
     *  same order in both passes because the parse is the same.  mark and
     *  rewind_to save this along with the literal count, so an abandoned
     *  attempt does not shift the numbering.
     */
    scope = ++c->block_seen;
    if (scope >= MAX_SCOPES) {
        fail(c, "blocks are nested or repeated more than %d times",
             MAX_SCOPES);
        return;
    }
    if (c->pass == 0) {
        if (scope >= c->scope_count)
            c->scope_count = scope + 1;
        memset(&c->scopes[scope], 0, sizeof c->scopes[scope]);
        c->scopes[scope].parent = outer_scope;
    }
    info = &c->scopes[scope];

    advance(c);                             /*  past '['  */
    c->current_scope = scope;

    while (at(c, ST_TOK_COLON)) {
        advance(c);
        if (!at(c, ST_TOK_IDENTIFIER)) {
            fail(c, "expected a block argument name");
            c->current_scope = outer_scope;
            return;
        }
        declare(c, c->token.text, 1);
        ++argc;
        advance(c);
    }
    if (argc > 0 && !at(c, ST_TOK_RBRACKET) && !accept_argument_bar(c)) {
        fail(c, "expected | after block arguments");
        c->current_scope = outer_scope;
        return;
    }
    /*  Block-local temporaries, which here really are local.  */
    if (at(c, ST_TOK_BAR)) {
        compiler_mark   before_temps;

        mark(c, &before_temps);
        advance(c);
        while (at(c, ST_TOK_IDENTIFIER)) {
            declare(c, c->token.text, 0);
            advance(c);
        }
        if (!accept(c, ST_TOK_BAR))
            rewind_to(c, &before_temps);
    }

    /*  The copied values, taken from the frame this block is created in.  */
    if (c->pass == 1) {
        for (i = 0; i < info->copied_count; ++i) {
            unsigned    slot;

            if (info->copied[i].is_vector) {
                if (!copied_vector_slot(c, outer_scope,
                                        info->copied[i].which, &slot)) {
                    fail(c, "a shared name's vector is not in scope");
                    c->current_scope = outer_scope;
                    return;
                }
            }  else  {
                unsigned    decl = info->copied[i].which;

                if (c->decls[decl].scope == outer_scope) {
                    slot = c->decls[decl].slot;
                }  else if (!copied_value_slot(c, outer_scope, decl, &slot)) {
                    fail(c, "a captured name is not in scope");
                    c->current_scope = outer_scope;
                    return;
                }
            }
            emit_push_temporary(c, slot);
        }
    }

    emit(c, 143);
    emit(c, (uint8_t) ((c->pass == 1 ? (info->copied_count << 4) : 0)
                       | (argc & 15)));
    size_at = c->out->length;
    emit(c, 0);
    emit(c, 0);
    body_start = c->out->length;

    /*
     *  The prologue.  The vector is made first, because a remote argument
     *  has to be moved into it; then one nil per local temporary, which is
     *  how the frame grows past the copied values.
     */
    if (c->pass == 1) {
        if (info->has_vector) {
            emit(c, 138);
            emit(c, (uint8_t) info->vector_size);
        }
        for (i = 0; i < c->decl_count; ++i) {
            if (c->decls[i].scope != scope || !c->decls[i].is_argument
             || !c->decls[i].remote)
                continue;
            /*
             *  An argument arrives in its frame slot however it is stored,
             *  so a shared one is moved across before anything reads it.
             *  Its frame slot is its position among the arguments.
             */
            {
                unsigned    k;
                unsigned    position = 0;

                for (k = 0; k < i; ++k) {
                    if (c->decls[k].scope == scope && c->decls[k].is_argument)
                        ++position;
                }
                emit_push_temporary(c, position);
                emit_store_remote(c, c->decls[i].slot, info->vector_slot, 1);
            }
        }
        for (i = 0; i < info->locals; ++i)
            emit(c, 115);               /*  push nil  */
    }

    compile_statements(c, 1);
    emit(c, 125);                       /*  return stack top from block  */
    if (!accept(c, ST_TOK_RBRACKET))
        fail(c, "expected ] closing a block");

    {
        size_t  size = c->out->length - body_start;

        if (size > 0xFFFF) {
            fail(c, "a block body is longer than 65535 bytes");
        }  else  {
            c->out->bytecodes[size_at]     = (uint8_t) (size >> 8);
            c->out->bytecodes[size_at + 1] = (uint8_t) (size & 0xFF);
        }
    }
    c->current_scope = outer_scope;
    /*
     *  A jump can no longer land where the peepholes think the end is.
     *  Same reason patch_jump clears them.
     */
    c->loop_nil_end = NO_LOOP_NIL;
    c->store_end    = NO_STORE;
}

/*  A primary: a literal, a variable, a parenthesised expression, a block.  */
static void
compile_primary(st_compiler *c, var_ref *out_var)
{
    if (out_var)
        out_var->kind = VAR_NONE;
    if (c->failed)
        return;

    switch (c->token.kind) {
    case ST_TOK_INTEGER:
        emit_push_integer(c, c->token.integer);
        advance(c);
        return;
    case ST_TOK_FLOAT:
        emit_push_literal_constant(c,
            c->ctx->make_float(c->token.real, c->ctx->user));
        advance(c);
        return;
    case ST_TOK_STRING:
        emit_push_literal_constant(c,
            c->ctx->make_string(c->token.text, c->ctx->user));
        advance(c);
        return;
    case ST_TOK_CHARACTER:
        emit_push_literal_constant(c,
            c->ctx->make_character
                ? c->ctx->make_character((unsigned) c->token.integer,
                                         c->ctx->user)
                : OM_fetch_pointer((uint32_t) c->token.integer,
                                   ST_CHARACTER_TABLE));
        advance(c);
        return;
    case ST_TOK_SYMBOL:
        emit_push_literal_constant(c,
            c->ctx->intern_symbol(c->token.text, c->ctx->user));
        advance(c);
        return;
    case ST_TOK_ARRAY_OPEN: {
        st_oop  array;

        advance(c);
        array = parse_literal_array(c);
        emit_push_literal_constant(c, array);
        return;
    }
    case ST_TOK_BYTE_ARRAY_OPEN:
        emit_push_literal_constant(c, parse_byte_array(c));
        return;
    case ST_TOK_LBRACE: {
        /*
         *  A dynamic array, { a. b. c }.
         *
         *  Unlike #(...) the elements are expressions evaluated at run time,
         *  so this is code rather than a literal:
         *
         *      push Array; push n; send new:
         *      for each element:  dup; push i; <element>; at:put:; pop
         *
         *  at:put: answers the value it stored rather than the collection,
         *  which is why each element ends with a pop rather than leaving the
         *  array on top.
         *
         *  The size has to be pushed before any element is compiled, and it
         *  is not known until they all have been -- so the elements are
         *  compiled once to count them, the buffer is rewound, and they are
         *  compiled again for real.  That is the same mark/rewind the
         *  cascade and whileTrue: receiver already use, for the same reason:
         *  a decision that can only be made after reading ahead.  The cost is
         *  compiling nested braces 2^depth times, which in practice is twice.
         */
        compiler_mark   first_element;
        unsigned        count = 0;
        unsigned        i;
        var_ref         array_class;

        advance(c);
        mark(c, &first_element);
        while (!c->failed && !at(c, ST_TOK_RBRACE) && !at(c, ST_TOK_END)) {
            compile_expression(c);
            ++count;
            if (!accept(c, ST_TOK_PERIOD))
                break;
        }
        if (c->failed)
            return;
        rewind_to(c, &first_element);
        /*
         *  The rewind moved the end of the buffer backwards, so any recorded
         *  offset into it is now stale -- the same hazard patch_jump guards
         *  against, and worth the two lines rather than the argument that
         *  nothing can reach it from here.
         */
        c->loop_nil_end = 0;
        c->store_end    = 0;

        array_class = resolve(c, "Array");
        emit_push_variable(c, &array_class, "Array");
        emit_push_integer(c, (int64_t) count);
        emit_send(c, "new:", 1, 0);
        for (i = 1; i <= count; ++i) {
            emit(c, 136);                   /*  dup the array          */
            emit_push_integer(c, (int64_t) i);
            compile_expression(c);
            emit_send(c, "at:put:", 2, 0);
            emit(c, 135);                   /*  drop at:put:'s answer  */
            accept(c, ST_TOK_PERIOD);
        }
        if (!accept(c, ST_TOK_RBRACE))
            fail(c, "a dynamic array must end with }");
        return;
    }
    case ST_TOK_IDENTIFIER: {
        char        name[256];
        var_ref     v;

        snprintf(name, sizeof name, "%s", c->token.text);
        v = resolve(c, name);
        if (out_var)
            *out_var = v;
        emit_push_variable(c, &v, name);
        advance(c);
        return;
    }
    case ST_TOK_LPAREN:
        advance(c);
        compile_expression(c);
        if (!accept(c, ST_TOK_RPAREN))
            fail(c, "expected )");
        return;
    case ST_TOK_LBRACKET:
        if (c->dialect == ST_DIALECT_CLOSURES) {
            compile_closure(c);
            return;
        }
        goto blue_book_block;
    blue_book_block: {
        /*
         *  A block compiles to blockCopy: followed by a jump over its body.
         *  The jump is what the interpreter's initial instruction pointer
         *  skips, which is why primitive 80 adds three to the pointer.
         */
        unsigned    argc = 0;
        unsigned    first_arg = c->name_count;
        unsigned    arg_slots[MAX_ARGS];
        unsigned    jump_at;
        unsigned    temp_first = c->name_count;
        unsigned    temp_count = 0;

        advance(c);
        while (at(c, ST_TOK_COLON)) {
            unsigned    slot;
            unsigned    k;

            advance(c);
            if (!at(c, ST_TOK_IDENTIFIER)) {
                fail(c, "expected a block argument name");
                return;
            }
            /*
             *  A block argument that shares a name with an enclosing
             *  temporary IS that temporary -- it is given the same slot, not
             *  one of its own.
             *
             *  This looks like a mistake and is the 1983 rule, and a good
             *  deal of the library depends on it.  RunArray>>copyFrom:to:
             *  declares "| run1 offset1 value1 ... |" and then writes
             *
             *      self at: start setRunOffsetAndValue:
             *          [:run1 :offset1 :value1 | value1]
             *
             *  before going on to use run1 and offset1 in the METHOD.  The
             *  block is how those variables get their values; give the
             *  argument a slot of its own and the method reads nil for ever.
             */
            slot = c->name_count;
            for (k = 0; k < c->name_count; ++k) {
                if (strcmp(c->names[k], c->token.text) == 0) {
                    slot = k;
                    break;
                }
            }
            if (slot == c->name_count && c->name_count < MAX_TEMPS) {
                snprintf(c->names[c->name_count], 64, "%.63s", c->token.text);
                ++c->name_count;
            }
            if (argc < MAX_ARGS)
                arg_slots[argc] = slot;
            ++argc;
            advance(c);
        }
        /*
         *  The bar is required only when a body follows it.  The Blue Book
         *  grammar shows it as mandatory, but Xerox's own compiler did not
         *  insist: the 1983 sources contain [:result], [:byte ] and a dozen
         *  more argument-only blocks, and the class library will not load
         *  without them.  Such a block takes its argument and answers nil.
         */
        if (argc > 0 && !at(c, ST_TOK_RBRACKET) && !accept_argument_bar(c)) {
            fail(c, "expected | after block arguments");
            return;
        }

        /*
         *  Block-local temporaries, [:x | | t | ...].  Post-Blue-Book.
         *
         *  The bar that opens them is the same token a binary send uses, and
         *  the difference is only visible at the end: "[:a | b | c]" is the
         *  send "b | c", while "[:a | | t | c]" is a declaration.  Both begin
         *  after the argument bar with something the parser has already
         *  consumed, so this speculatively reads BAR IDENTIFIER* BAR and
         *  rewinds if the closing bar never arrives -- the same mark/rewind
         *  the cascade and the whileTrue: receiver use.  rewind_to restores
         *  name_count too, so a failed attempt leaves no names behind.
         *
         *  Note we are only ever HERE when the token is a bar in declaration
         *  position: after arguments their own bar has been eaten, so
         *  "[:a | b | c]" is sitting on the identifier b, not on a bar, and
         *  never enters this at all.
         */
        temp_first = c->name_count;
        if (at(c, ST_TOK_BAR)) {
            compiler_mark   before_temps;

            mark(c, &before_temps);
            advance(c);
            while (at(c, ST_TOK_IDENTIFIER)) {
                /*
                 *  Unlike a block ARGUMENT, a declared temporary never shares
                 *  an enclosing slot of the same name.  The argument rule is
                 *  a 1983 artifact the library depends on (see above); a
                 *  declaration means "a new variable", and appending it gives
                 *  that for free, because resolve searches names backwards
                 *  and so finds the innermost.
                 */
                if (c->name_count < MAX_TEMPS) {
                    snprintf(c->names[c->name_count], 64, "%.63s",
                             c->token.text);
                    ++c->name_count;
                }
                advance(c);
            }
            if (accept(c, ST_TOK_BAR))
                temp_count = c->name_count - temp_first;
            else
                rewind_to(c, &before_temps);
        }

        /*
         *  blockCopy: takes the HOME CONTEXT, not the receiver.  Pushing
         *  self happens to work whenever self is a context-like object and
         *  fails everywhere else -- in a doIt, self is nil, and the block is
         *  then built out of nil.  Bytecode 137 pushes thisContext, which is
         *  what the 1983 compiler emits and what trace2 shows.
         */
        emit(c, 137);                       /*  push active context  */
        emit_push_integer(c, (int64_t) argc);
        emit(c, 200);                       /*  blockCopy:  */
        jump_at = emit_jump_placeholder(c, JUMP_ALWAYS);

        /*
         *  Block arguments arrive on the stack and are stored into their
         *  slots, last first -- which are not necessarily consecutive, since
         *  one that shares a name with an enclosing temporary shares its
         *  slot too.
         */
        {
            unsigned    i;

            for (i = 0; i < argc && i < MAX_ARGS; ++i)
                emit_store_temporary(c, arg_slots[argc - 1 - i], 1);
        }
        /*
         *  Block temporaries start at nil on every activation, and here they
         *  have to be told so explicitly.  A method's temporaries are nil
         *  because its context is freshly made; a block in this dialect
         *  borrows its home's frame, so a block evaluated a second time --
         *  inside a do:, say -- would otherwise still see what the first
         *  evaluation left in the slot.  Emitted after the arguments, which
         *  are popped off the stack and must not have nils pushed over them.
         */
        {
            unsigned    i;

            for (i = 0; i < temp_count; ++i) {
                emit(c, 115);               /*  push nil  */
                emit_store_temporary(c, temp_first + i, 1);
            }
        }
        compile_statements(c, 1);
        emit(c, 125);                       /*  return stack top from block  */
        if (!accept(c, ST_TOK_RBRACKET)) {
            fail(c, "expected ] closing a block");
            return;
        }
        patch_jump(c, jump_at);

        /*  The frame still has to hold them; see max_names.  */
        if (c->name_count > c->max_names)
            c->max_names = c->name_count;
        c->name_count = first_arg;          /*  arguments leave scope  */
        return;
    }
    default:
        fail(c, "unexpected token");
        return;
    }
}

/*
 *  ----------  Inlined control flow  ----------
 *
 *  The Blue Book compiler turns conditionals and loops into jumps rather
 *  than sends, and the reference traces confirm it: a conditional appears as
 *  bytecode 172, never as a send of ifTrue:.  Doing the same is not an
 *  optimization here, it is a requirement -- Boolean has no ifTrue: method
 *  to fall back on, and a loop compiled as sends would grow the sender chain
 *  once per iteration.
 *
 *  to:do: is deliberately NOT inlined.  The reference traces show it
 *  arriving as a real send with a block argument, so the 1983 compiler left
 *  it alone and so do we.
 */


static void
mark(st_compiler *c, compiler_mark *m)
{
    LEX_save(c->lx, &m->lexer);
    m->token         = c->token;
    m->length        = c->out->length;
    m->literal_count = c->out->literal_count;
    m->name_count    = c->name_count;
    m->block_seen    = c->block_seen;
    m->decl_count    = c->decl_count;
    m->decl_visible  = c->decl_visible;
    m->decl_seen     = c->decl_seen;

}

static void
rewind_to(st_compiler *c, const compiler_mark *m)
{
    LEX_restore(c->lx, &m->lexer);
    c->token               = m->token;
    c->out->length         = m->length;
    /*
     *  Literals added by the abandoned attempt are dropped too.  They are
     *  only reachable by index, and the indices are about to be reused.
     */
    c->out->literal_count  = m->literal_count;
    c->name_count          = m->name_count;
    c->block_seen          = m->block_seen;
    c->decl_count          = m->decl_count;
    c->decl_visible        = m->decl_visible;
    c->decl_seen           = m->decl_seen;
    /*
     *  Only in pass zero: pass one reads the analysis rather than building
     *  it, and giving any of it back there would delete conclusions.
     */



}

/*
 *  The selector that follows a literal block, found by LOOKING rather than
 *  by parsing.
 *
 *  "[cond] whileTrue: [body]" has to be compiled inlined -- a jump back,
 *  no BlockContext -- and "[cond] value" has to be compiled as a real
 *  block.  Which one it is cannot be known until the selector after the
 *  block has been read, and the block comes first.
 *
 *  This used to be settled by compiling the block as a REAL block, looking
 *  at what came next, and rewinding if the answer was whileTrue:.  That is
 *  sound for tokens and bytecodes, which rewind_to gives back, and unsound
 *  for the closure analysis, which it cannot.  The speculative reading
 *  marks every enclosing name the block touches as captured and records
 *  that its scope needs them; the real reading, inlined, needs neither.
 *  Worse, the two passes disagree about which happened: pass zero's
 *  conclusions describe the FINAL reading, so when pass one re-runs the
 *  same speculative parse it cannot resolve the names it is about to throw
 *  away, and fails a method it would have compiled.
 *
 *  So no block is parsed twice under two readings any more.  The lexer
 *  scans to the matching bracket -- it already knows what a comment, a
 *  string and a $] are -- and answers the selector after it.  Nothing in
 *  the compiler is touched, so there is nothing to give back.
 */
static void
selector_after_block(st_compiler *c, char *out, size_t out_len,
                     int *argument_is_block)
{
    st_lexer_state  saved;
    st_token        saved_token = c->token;
    st_token        tok;
    int             depth;

    out[0] = '\0';
    if (argument_is_block)
        *argument_is_block = 0;
    LEX_save(c->lx, &saved);
    /*  c->token is the opening bracket; the lexer sits just past it.  */
    depth = 1;
    for (;;) {
        if (!LEX_next(c->lx, &tok) || tok.kind == ST_TOK_END
         || tok.kind == ST_TOK_ERROR)
            break;
        if (tok.kind == ST_TOK_LBRACKET)
            ++depth;
        else if (tok.kind == ST_TOK_RBRACKET) {
            if (--depth == 0) {
                if (LEX_next(c->lx, &tok)
                 && (tok.kind == ST_TOK_KEYWORD
                  || tok.kind == ST_TOK_IDENTIFIER)) {
                    snprintf(out, out_len, "%s", tok.text);
                    /*
                     *  And whether ITS argument is a literal block too,
                     *  which is what decides whether the whole form can be
                     *  inlined -- "cond ifTrue: [a] ifFalse: aBlock" is an
                     *  ordinary send, not a malformed conditional.
                     */
                    if (argument_is_block && LEX_next(c->lx, &tok))
                        *argument_is_block = tok.kind == ST_TOK_LBRACKET;
                }
                break;
            }
        }
    }
    LEX_restore(c->lx, &saved);
    c->token = saved_token;
}

/*
 *  Compile a literal block's body in place, with no BlockContext.  Answers 0
 *  if what follows is not a block that can be inlined -- one taking
 *  arguments cannot, since there is no frame to put them in.
 */
static int
at_inlinable_block(st_compiler *c)
{
    st_token    look;

    if (!at(c, ST_TOK_LBRACKET))
        return 0;
    /*  A block with arguments begins with a colon and needs a real frame.  */
    LEX_peek(c->lx, &look);
    return look.kind != ST_TOK_COLON;
}

/*
 *  Temporaries declared inside a block that is being inlined.
 *
 *  They have nowhere of their own to live: the whole point of inlining is
 *  that there is no BlockContext and no frame, so the statements run in the
 *  enclosing method's frame and the names are HOISTED into it.  That is
 *  what every Smalltalk that inlines these does.
 *
 *  Hoisting has to keep the LEXICAL extent, though, or the hoist becomes
 *  visible: the names must stop resolving at the closing bracket, so that
 *
 *      x ifTrue: [ | t | t := 1. y ifTrue: [ | t | t := 2 ]. ^t ]
 *
 *  answers 1, and so that two sibling blocks each declaring "t" get two
 *  variables rather than one shared by accident.  Saving and restoring the
 *  end of the visible name list is the whole of it -- in the Blue Book
 *  dialect the list IS names[], and in the closure dialect it is the
 *  declaration cursor.
 *
 *  A leading bar here is unambiguous.  A statement cannot begin with a
 *  binary selector, so "[ | ..." can only be a declaration -- unlike the
 *  bar after a block's arguments, which needs the speculative read.
 */
static void
compile_inline_block(st_compiler *c)
{
    unsigned    outer_names   = c->name_count;
    unsigned    outer_visible = c->decl_visible;

    advance(c);                         /*  past [  */
    if (at(c, ST_TOK_BAR)) {
        advance(c);
        while (at(c, ST_TOK_IDENTIFIER)) {
            if (c->name_count < MAX_TEMPS) {
                snprintf(c->names[c->name_count], 64, "%.63s", c->token.text);
                ++c->name_count;
                if (c->name_count > c->max_names)
                    c->max_names = c->name_count;
            }
            if (c->dialect == ST_DIALECT_CLOSURES)
                declare(c, c->token.text, 0);
            advance(c);
        }
        if (!accept(c, ST_TOK_BAR)) {
            fail(c, "expected | after an inlined block's temporaries");
            return;
        }
    }
    compile_statements(c, 1);
    if (!accept(c, ST_TOK_RBRACKET))
        fail(c, "expected ] closing an inlined block");
    /*
     *  The names go out of scope; their frame slots stay reserved, because
     *  max_names is the high-water mark the frame is sized from.
     */
    c->name_count   = outer_names;
    c->decl_visible = outer_visible;
}

/*
 *  cond ifTrue: [a] ifFalse: [b] and its relatives.  The receiver's code has
 *  already been emitted; what is left is to branch around one arm or the
 *  other.  An arm that is absent answers nil, which is what the message
 *  would have answered.
 */
static int
compile_inline_conditional(st_compiler *c, const char *first)
{
    int         jump_on_false;
    int         has_else = 0;
    unsigned    branch;
    unsigned    skip;
    char        second[64] = "";

    if (strcmp(first, "ifTrue:") == 0)
        jump_on_false = 1;
    else if (strcmp(first, "ifFalse:") == 0)
        jump_on_false = 0;
    else
        return 0;
    if (!at_inlinable_block(c))
        return 0;

    /*
     *  Whether the form can be inlined is settled BEFORE anything is
     *  emitted, by looking past the first arm.  A second arm that is not a
     *  literal block -- "ifTrue: [a] ifFalse: aBlock" -- makes the whole
     *  thing an ordinary send, and finding that out halfway through leaves
     *  a jump and an arm already emitted with nowhere to put them.
     */
    {
        char    next[64] = "";
        int     second_is_block = 0;

        selector_after_block(c, next, sizeof next, &second_is_block);
        if (((jump_on_false && strcmp(next, "ifFalse:") == 0)
          || (!jump_on_false && strcmp(next, "ifTrue:") == 0))
         && !second_is_block)
            return 0;
    }

    branch = emit_jump_placeholder(c,
                    jump_on_false ? JUMP_IF_FALSE : JUMP_IF_TRUE);
    compile_inline_block(c);

    /*  A second keyword makes it the two-armed form.  */
    if (at(c, ST_TOK_KEYWORD)) {
        snprintf(second, sizeof second, "%.63s", c->token.text);
        if ((jump_on_false && strcmp(second, "ifFalse:") == 0)
         || (!jump_on_false && strcmp(second, "ifTrue:") == 0)) {
            advance(c);
            has_else = 1;
        }
    }

    skip = emit_jump_placeholder(c, JUMP_ALWAYS);
    patch_jump(c, branch);
    if (has_else)
        compile_inline_block(c);
    else
        emit(c, 115);                   /*  the untaken arm answers nil  */
    patch_jump(c, skip);
    return 1;
}

/*
 *  a and: [b] is a conditional that answers a boolean rather than nil, so
 *  the untaken arm pushes the constant that short-circuited it.
 */
static int
compile_inline_and_or(st_compiler *c, const char *selector)
{
    int         is_and;
    unsigned    branch;
    unsigned    skip;

    if (strcmp(selector, "and:") == 0)
        is_and = 1;
    else if (strcmp(selector, "or:") == 0)
        is_and = 0;
    else
        return 0;
    if (!at_inlinable_block(c))
        return 0;

    /*  and: short-circuits when false; or: when true.  */
    branch = emit_jump_placeholder(c, is_and ? JUMP_IF_FALSE : JUMP_IF_TRUE);
    compile_inline_block(c);
    skip = emit_jump_placeholder(c, JUMP_ALWAYS);
    patch_jump(c, branch);
    emit(c, (uint8_t) (is_and ? 114 : 113));    /*  false, or true  */
    patch_jump(c, skip);
    return 1;
}

/*
 *  [cond] whileTrue: [body].
 *
 *  Unlike the conditionals, the receiver must NOT already have been emitted:
 *  its block is the loop's test and belongs at the top of the loop, to be
 *  re-executed each time round.  The caller therefore rewinds to before the
 *  receiver and calls this, which reads both blocks itself.
 */
static int
compile_inline_while(st_compiler *c)
{
    unsigned    loop_top;
    unsigned    exit_branch;
    int         while_true;
    char        selector[64];

    if (!at_inlinable_block(c))
        return 0;

    loop_top = c->out->length;
    compile_inline_block(c);            /*  the test  */

    if (!at(c, ST_TOK_KEYWORD) && !at(c, ST_TOK_IDENTIFIER))
        return 0;
    snprintf(selector, sizeof selector, "%.63s", c->token.text);
    if (strcmp(selector, "whileTrue:") == 0 || strcmp(selector, "whileTrue") == 0)
        while_true = 1;
    else if (strcmp(selector, "whileFalse:") == 0
          || strcmp(selector, "whileFalse") == 0)
        while_true = 0;
    else
        return 0;
    advance(c);

    /*  Leave when the test answers the opposite of what the loop wants.  */
    exit_branch = emit_jump_placeholder(c,
                        while_true ? JUMP_IF_FALSE : JUMP_IF_TRUE);

    if (selector[strlen(selector) - 1] == ':') {
        if (!at_inlinable_block(c))
            return 0;                   /*  an ordinary send after all  */
        compile_inline_block(c);
        emit(c, 135);                   /*  discard the body's value  */
    }
    emit_jump_back_to(c, loop_top);
    patch_jump(c, exit_branch);
    emit(c, 115);                       /*  a loop answers nil  */
    c->loop_nil_end = c->out->length;
    return 1;
}

/*
 *  Answers whether it emitted a send, which the caller needs: after any send
 *  the value on the stack is a result, not the original receiver, so it is no
 *  longer "super" for the message that follows.
 */
static int
compile_unary_sequence(st_compiler *c, int receiver_is_super)
{
    compiler_mark   receiver;
    int             sent = 0;

    while (at(c, ST_TOK_IDENTIFIER)) {
        char    selector[256];

        mark(c, &receiver);             /*  the code so far IS the receiver */
        snprintf(selector, sizeof selector, "%s", c->token.text);
        advance(c);
        emit_send(c, selector, 0, receiver_is_super);
        receiver_is_super = 0;
        sent = 1;
    }
    if (sent)
        c->receiver_mark = receiver;
    return sent;
}

static int
compile_binary_sequence(st_compiler *c, int receiver_is_super)
{
    compiler_mark   receiver;
    int             binary_sent = 0;
    int             any_sent = 0;

    /*
     *  A binary message to super is a super send too.  This passed a hard
     *  zero, so "super >= aNumber" compiled to the ordinary one-byte send of
     *  >= -- to the receiver rather than to its superclass, which is the
     *  method that is running.  Float>>>= is exactly that, so comparing a
     *  Float with an Integer recursed until it ran out of bytecodes.  Unary
     *  and keyword sends to super were always right, which is why it took
     *  the 1983 library to find: our own kernel used neither.
     */
    /*
     *  Two answers are wanted here and they are not the same one.  The
     *  RETURN says whether anything at all was sent, because the caller uses
     *  it to decide that its own receiver is no longer super -- and a unary
     *  send is quite enough to make that so.  Reporting only binary sends
     *  compiled "super new compositionRectangle: ..." as a super send of the
     *  keyword message, which starts the lookup above the receiver's class
     *  and walks straight past the method it wanted.  The MARK, separately,
     *  belongs to the outermost send at THIS level, so it is only touched
     *  when a binary send actually happens.
     */
    if (compile_unary_sequence(c, receiver_is_super)) {
        receiver_is_super = 0;
        any_sent = 1;
    }
    while (at(c, ST_TOK_BINARY) || at(c, ST_TOK_BAR)) {
        char    selector[256];

        mark(c, &receiver);
        /*  A bar in operator position is the binary selector, not a
         *  temporaries separator; those only appear at the head of a
         *  method or block body.  */
        snprintf(selector, sizeof selector, "%s",
                 at(c, ST_TOK_BAR) ? "|" : c->token.text);
        advance(c);
        compile_primary(c, NULL);
        compile_unary_sequence(c, 0);
        emit_send(c, selector, 1, receiver_is_super);
        receiver_is_super = 0;
        binary_sent = 1;
        any_sent = 1;
    }
    if (binary_sent)
        c->receiver_mark = receiver;
    return any_sent;
}

/*
 *  A complete binary expression, primary included.  compile_binary_sequence
 *  continues from a receiver already on the stack, which is right for the
 *  receiver of a message but wrong for an argument -- an argument has to
 *  parse its own primary first.
 */
static void
compile_binary_expression(st_compiler *c)
{
    compile_primary(c, NULL);
    compile_binary_sequence(c, 0);
}

static void
compile_keyword_message(st_compiler *c, int receiver_is_super)
{
    char            selector[256] = "";
    unsigned        argc = 0;
    compiler_mark   receiver;

    if (compile_binary_sequence(c, receiver_is_super))
        receiver_is_super = 0;
    if (!at(c, ST_TOK_KEYWORD))
        return;
    mark(c, &receiver);

    /*
     *  The inlined forms, tried before building a send.  Each rewinds if the
     *  shape is not the one it handles -- an argument that is not a literal
     *  block, say -- and the ordinary keyword loop below picks it up.
     */
    if (!receiver_is_super) {
        compiler_mark   here;
        char            first[64];

        snprintf(first, sizeof first, "%.63s", c->token.text);
        mark(c, &here);
        advance(c);
        if (compile_inline_conditional(c, first))
            return;
        if (compile_inline_and_or(c, first))
            return;
        rewind_to(c, &here);
    }

    while (at(c, ST_TOK_KEYWORD)) {
        size_t  used = strlen(selector);

        snprintf(selector + used, sizeof selector - used, "%s", c->token.text);
        advance(c);
        compile_binary_expression(c);
        ++argc;
    }
    emit_send(c, selector, argc, receiver_is_super);
    c->receiver_mark = receiver;
}

/*
 *  A cascade sends several messages to one receiver, evaluated once.
 *
 *      <receiver>
 *      dup ; <args> send msg1 ; pop
 *      dup ; <args> send msg2 ; pop
 *            <args> send msgN          "the last one consumes the receiver"
 *
 *  Whether a message needs the receiver kept is only known once the NEXT
 *  token has been read, by which point the message is compiled.  So each is
 *  compiled speculatively with the duplication in place, and the last one --
 *  the one no semicolon follows -- is rewound and compiled again without it.
 *
 *  The mark is taken after the receiver, not before: the receiver's code is
 *  correct and stays, and only the messages are reconsidered.
 */
static void
compile_cascade(st_compiler *c)
{
    compiler_mark   after_receiver = c->receiver_mark;

    if (!at(c, ST_TOK_SEMICOLON))
        return;

    /*  Redo the first message, this time keeping the receiver.  */
    rewind_to(c, &after_receiver);
    emit(c, 136);                       /*  duplicate the receiver  */
    compile_keyword_message(c, 0);
    if (c->failed)
        return;

    while (at(c, ST_TOK_SEMICOLON)) {
        compiler_mark   before_message;

        advance(c);
        emit(c, 135);                   /*  drop the previous answer  */
        mark(c, &before_message);
        emit(c, 136);
        compile_keyword_message(c, 0);
        if (c->failed)
            return;
        if (!at(c, ST_TOK_SEMICOLON)) {
            /*  That was the last, so it should have consumed the receiver. */
            rewind_to(c, &before_message);
            compile_keyword_message(c, 0);
            return;
        }
    }
}

static void
compile_expression(st_compiler *c)
{
    /*  An assignment is an identifier followed by the assignment arrow.  */
    if (at(c, ST_TOK_IDENTIFIER)) {
        st_token    look;

        LEX_peek(c->lx, &look);
        if (look.kind == ST_TOK_ASSIGN) {
            char        name[256];
            var_ref     v;

            snprintf(name, sizeof name, "%s", c->token.text);
            advance(c);                 /*  past the name  */
            advance(c);                 /*  past :=        */
            compile_expression(c);
            /*
             *  Resolved as an ASSIGNMENT, which is what tells the closure
             *  analysis this name cannot be captured by value: two scopes
             *  that both see a store have to see each other's.
             */
            v = resolve_for(c, name, 1);
            switch (v.kind) {
            case VAR_TEMPORARY: emit_store_temporary(c, v.index, 0); break;
            case VAR_REMOTE:    emit_store_remote(c, v.index, v.vector, 0); break;
            case VAR_INSTANCE:  emit_store_receiver_variable(c, v.index, 0); break;
            case VAR_GLOBAL:    emit_store_literal_variable(c, v.association, 0); break;
            default:
                fail(c, "cannot assign to '%s'", name);
                break;
            }
            return;
        }
    }
    {
        int             is_super = at(c, ST_TOK_IDENTIFIER)
                                && strcmp(c->token.text, "super") == 0;
        var_ref         v;
        compiler_mark   before_receiver;
        compiler_mark   after_receiver;

        /*
         *  Marked before anything is emitted.  whileTrue: needs its receiver
         *  block compiled as the loop's test rather than built as a block,
         *  and that is only known once the block has been read past.
         */
        mark(c, &before_receiver);
        if (at_inlinable_block(c)) {
            char    next[256];
            int     body_is_block = 0;

            selector_after_block(c, next, sizeof next, &body_is_block);
            /*
             *  A loop is inlined only when its body is a literal block as
             *  well.  "[cond] whileTrue: aBlock" is a perfectly ordinary
             *  message send -- BlockClosure implements it -- and refusing
             *  it was this compiler mistaking its own optimisation for a
             *  rule of the language.
             */
            if ((strncmp(next, "whileTrue", 9) == 0
              || strncmp(next, "whileFalse", 10) == 0)
             /*
              *  A body block is required only by the keyword forms.
              *  "[...] whileTrue" takes no argument at all, so asking
              *  whether ITS argument is a literal block asks about the
              *  token after the loop and answers about someone else.
              */
             && (body_is_block || next[strlen(next) - 1] != ':')) {
                if (compile_inline_while(c))
                    return;
                /*
                 *  It looked like a loop and was not one -- the body was
                 *  not a literal block.  Nothing has been kept, because
                 *  compile_inline_while rewinds its own attempt, so this
                 *  falls through to the ordinary reading.
                 */
                rewind_to(c, &before_receiver);
            }
            compile_primary(c, &v);
        }  else  {
            compile_primary(c, &v);
        }
        /*
         *  A cascade with no message at all before the semicolon is not
         *  legal, so the receiver mark only has to be sound when a message
         *  was sent -- which is exactly when the message levels set it.
         */
        mark(c, &after_receiver);
        c->receiver_mark = after_receiver;
        compile_keyword_message(c, is_super);
        compile_cascade(c);
    }
}

/*
 *  Drop the value of a statement nobody asked for.
 *
 *  Normally that is bytecode 135.  But when the statement was an inlined
 *  loop, the value being dropped is a nil this compiler pushed one byte ago
 *  for no other purpose, so the push is removed instead and no pop is
 *  emitted.  That is what LinkedList>>do: does in the 1983 image: the
 *  backward jump is followed immediately by returnSelf.
 *
 *  The test is positional, which is what makes it safe.  If the loop was
 *  nested inside a larger expression, something will have been emitted after
 *  the nil and the lengths will not match, so the push stays.  Deleting the
 *  byte cannot disturb a jump either: the only jump that targets it is the
 *  loop exit, and with nothing after it the same offset now lands on
 *  whatever follows -- which is precisely where the loop should exit to.
 */
static void
discard_statement_value(st_compiler *c)
{
    if (c->loop_nil_end == c->out->length && c->out->length > 0) {
        --c->out->length;
        c->loop_nil_end = NO_LOOP_NIL;
        return;
    }
    /*
     *  An assignment whose value nobody wanted: emit it again as the
     *  store-and-pop it should have been.  Same positional test as above,
     *  and cleared by patch_jump for the same reason -- a jump landing at
     *  the current end means these bytes are a target and must not move.
     */
    if (c->store_end == c->out->length && c->store_end != NO_STORE) {
        c->out->length = c->store_at;
        c->store_end   = NO_STORE;
        switch (c->store_kind) {
        case STORE_TEMPORARY:
            emit_store_temporary(c, c->store_index, 1);
            break;
        case STORE_RECEIVER:
            emit_store_receiver_variable(c, c->store_index, 1);
            break;
        default:
            emit_store_literal_variable(c, c->store_association, 1);
            break;
        }
        return;
    }
    emit(c, 135);
}

static void
compile_statements(st_compiler *c, int inside_block)
{
    int emitted = 0;

    for (;;) {
        if (c->failed)
            return;
        if (at(c, ST_TOK_END) || at(c, ST_TOK_RBRACKET))
            break;
        if (accept(c, ST_TOK_PERIOD))
            continue;

        if (accept(c, ST_TOK_RETURN)) {
            compile_expression(c);
            emit(c, 124);               /*  return stack top from method  */
            emitted = 1;
            accept(c, ST_TOK_PERIOD);
            if (at(c, ST_TOK_END) || at(c, ST_TOK_RBRACKET))
                return;
            continue;
        }
        compile_expression(c);
        emitted = 1;
        if (accept(c, ST_TOK_PERIOD)) {
            if (at(c, ST_TOK_END) || at(c, ST_TOK_RBRACKET)) {
                /*  A trailing period leaves nothing on the stack.  */
                discard_statement_value(c);
                emitted = 0;
                break;
            }
            discard_statement_value(c); /*  discard the statement's value  */
            emitted = 0;
            continue;
        }
        break;
    }
    /*
     *  What the last statement leaves behind is the one place a block and a
     *  method differ.  A block answers its last statement, so the value has
     *  to stay; a method answers self, so the value is dropped and the
     *  caller's returnSelf finds a clean stack.  An empty block still has to
     *  answer something.
     */
    if (!emitted) {
        if (inside_block)
            emit(c, 115);               /*  an empty block answers nil  */
    } else if (!inside_block) {
        discard_statement_value(c);     /*  a method answers self, not this */
    }
}

/*
 *  A super send reads the method class from the last literal, so it has to be
 *  the last literal.  It is appended here, after the body, and deliberately
 *  without going through literal_index: that would reuse an existing entry if
 *  the class had already been mentioned by name, and the whole requirement is
 *  positional.
 */
static int
needs_method_class(const st_compiler *c)
{
    /*
     *  A super send needs it to look up from.  So does any method with a
     *  header extension -- flag value 7, meaning a primitive or more than
     *  four arguments -- whether or not it uses super: Xerox's
     *  SmallInteger>>bitXor: and SmallInteger>>asFloat both carry it with no
     *  super send anywhere in them.  The extension then sits second to last
     *  and the class association last, which is the order the interpreter
     *  reads them in.
     */
    /*
     *  c->argument_count, not c->out->argument_count: the latter is copied
     *  across only after the body is compiled, which is after this runs.  It
     *  read zero, so a method with more than four arguments and no primitive
     *  lost its class literal -- and then COMPILE_method spliced the header
     *  extension in ahead of what it took to be that literal, shifting a real
     *  one down a place.  Every bytecode referring to it read the extension
     *  instead, which is a SmallInteger, so the send went out with a number
     *  for a selector.
     */
    return c->used_super
        || c->out->primitive != 0
        || c->argument_count > 4;
}

/*
 *  Put the method's pragmas in its literal frame, as an
 *  AdditionalMethodState, so the image can read them back.
 *
 *  Ahead of the class association, which has to stay last: a super send
 *  takes its lookup class from literal (count - 1) and would find this
 *  instead.
 */
static void
add_method_state_literal(st_compiler *c)
{
    st_oop      entries[MAX_PRAGMAS];
    st_oop      pragmas;
    st_oop      state;
    unsigned    i;

    if (c->failed || c->pragma_count == 0 || !c->ctx->make_method_state)
        return;
    for (i = 0; i < c->pragma_count; ++i) {
        st_oop      parts[1 + MAX_PRAGMA_ARGS];
        unsigned    k;

        parts[0] = c->ctx->intern_symbol(c->pragmas[i].keyword, c->ctx->user);
        for (k = 0; k < c->pragmas[i].argc; ++k)
            parts[1 + k] = c->pragmas[i].args[k];
        entries[i] = c->ctx->make_array(parts, 1 + c->pragmas[i].argc,
                                        c->ctx->user);
    }
    pragmas = c->ctx->make_array(entries, c->pragma_count, c->ctx->user);
    state   = c->ctx->make_method_state(pragmas, c->ctx->user);
    /*
     *  A profile with no AdditionalMethodState answers nil, and the method
     *  is compiled exactly as it was before pragmas were kept.
     */
    if (state == ST_NIL || state == ST_OOP_INVALID)
        return;
    literal_index(c, state);
}

static void
append_method_class_literal(st_compiler *c)
{
    if (c->failed || !needs_method_class(c))
        return;
    if (c->ctx->method_class_association == ST_OOP_INVALID) {
        fail(c, "this method needs its class as a literal, which the compile "
                "context does not supply");
        return;
    }
    if (c->out->literal_count >= 256) {
        fail(c, "too many literals");
        return;
    }
    c->out->literals[c->out->literal_count++] =
        c->ctx->method_class_association;
}

/*
 *  How deep the working stack gets.
 *
 *  A context has room for its temporaries and its stack together, and the
 *  method header says whether the small size or the large one is wanted.
 *  This decided that on the temporary count alone, which is only half the
 *  question: a method with two temporaries and a deeply nested expression
 *  overflows a small context and writes past the end of it.  BrowserView
 *  class>>openOn: is one -- a cascade of six keyword messages, each with a
 *  Rectangle built inline -- and it corrupted the heap rather than failing.
 *
 *  The depth is measured by walking the finished bytecodes and applying each
 *  one's effect on the stack.  Doing it afterwards rather than while emitting
 *  keeps the arithmetic in one place, where it can be read against Chapter 28
 *  instead of being spread over every emit site.  Jumps are ignored: the
 *  compiler only ever generates branches whose arms leave the stack at the
 *  same depth, so a straight walk gives the true maximum.
 */
static unsigned
max_stack_depth(const st_compiled_code *code)
{
    int         depth = 0;
    int         highest = 0;
    unsigned    i;

    for (i = 0; i < code->length; ++i) {
        uint8_t     b = code->bytecodes[i];
        int         effect = 0;

        if (b <= 119) {
            /*  0..119 are all pushes: receiver variables, temporaries,
             *  literals, and the constants self, true, false, nil, -1, 0,
             *  1 and 2.  */
            effect = 1;
        }  else if (b <= 124) {
            effect = 0;                 /*  returns end the walk anyway  */
        }  else if (b == 125) {
            effect = 0;                 /*  return from block            */
        }  else if (b == 128) {
            ++i;                        /*  extended push                */
            effect = 1;
        }  else if (b == 129) {
            ++i;                        /*  extended store, keeps it     */
            effect = 0;
        }  else if (b == 130) {
            ++i;                        /*  extended pop and store       */
            effect = -1;
        }  else if (b == 131 || b == 133) {
            uint8_t desc = code->bytecodes[++i];

            effect = -(int) (desc >> 5); /*  send: argc off, result on   */
        }  else if (b == 132 || b == 134) {
            uint8_t argc = code->bytecodes[++i];

            ++i;
            effect = -(int) argc;
        }  else if (b == 135) {
            effect = -1;                /*  pop                          */
        }  else if (b == 136) {
            effect = 1;                 /*  duplicate                    */
        }  else if (b == 137) {
            effect = 1;                 /*  push active context          */
        }  else if (b == 138) {
            /*
             *  Push New Array.  The operand's top bit says the elements
             *  come off the stack, in which case n go away and the array
             *  arrives; otherwise nothing is consumed.
             */
            uint8_t size = code->bytecodes[++i];

            effect = (size & 128) ? 1 - (int) (size & 127) : 1;
        }  else if (b == 140) {
            i += 2;                     /*  push remote temp             */
            effect = 1;
        }  else if (b == 141) {
            i += 2;                     /*  store remote temp, keeps it  */
            effect = 0;
        }  else if (b == 142) {
            i += 2;                     /*  pop and store remote temp    */
            effect = -1;
        }  else if (b == 143) {
            /*
             *  Push Closure Copy.  The copied values are popped and the
             *  closure is pushed; the body is skipped rather than walked,
             *  because its depth belongs to its own frame and not to this
             *  one.
             */
            uint8_t counts = code->bytecodes[i + 1];
            unsigned block_size = (unsigned) code->bytecodes[i + 2] * 256
                                + code->bytecodes[i + 3];

            effect = 1 - (int) (counts >> 4);
            i += 3 + block_size;
        }  else if (b >= 96 && b <= 111) {
            effect = -1;                /*  pop and store                */
        }  else if (b >= 144 && b <= 175) {
            if (b >= 160)
                ++i;                    /*  long jumps carry a byte      */
            /*  A conditional jump pops its condition.  */
            effect = (b >= 152 && b <= 159) ? -1
                   : ((b >= 168 && b <= 175) ? -1 : 0);
        }  else if (b >= 176) {
            /*  Arithmetic and special selectors: one argument except the
             *  unary ones, which take none.  */
            static const int special_args[16] = {
                1, 2, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0
            };

            effect = (b < 192) ? -1
                   : ((b < 208) ? -special_args[b - 192]
                                : -(int) ((b - 208) / 16));
        }
        depth += effect;
        if (depth > highest)
            highest = depth;
        if (depth < 0)
            depth = 0;
    }
    return (unsigned) highest;
}

/*  ----------  The method pattern  ----------  */

static void
compile_pattern(st_compiler *c)
{
    if (at(c, ST_TOK_IDENTIFIER)) {
        snprintf(c->out->selector, sizeof c->out->selector, "%s",
                 c->token.text);
        advance(c);
        return;
    }
    if (at(c, ST_TOK_BINARY) || at(c, ST_TOK_BAR)) {
        /*
         *  A vertical bar is a binary selector as well as the separator for
         *  temporaries.  Only the parser can tell them apart, and here we
         *  are plainly at a method pattern.
         */
        snprintf(c->out->selector, sizeof c->out->selector, "%s",
                 at(c, ST_TOK_BAR) ? "|" : c->token.text);
        advance(c);
        if (!at(c, ST_TOK_IDENTIFIER)) {
            fail(c, "expected an argument name");
            return;
        }
        snprintf(c->names[c->name_count++], 64, "%.63s", c->token.text);
        if (c->dialect == ST_DIALECT_CLOSURES)
            declare(c, c->token.text, 1);
        ++c->argument_count;
        advance(c);
        return;
    }
    if (at(c, ST_TOK_KEYWORD)) {
        c->out->selector[0] = '\0';
        while (at(c, ST_TOK_KEYWORD)) {
            size_t  used = strlen(c->out->selector);

            snprintf(c->out->selector + used,
                     sizeof c->out->selector - used, "%s", c->token.text);
            advance(c);
            if (!at(c, ST_TOK_IDENTIFIER)) {
                fail(c, "expected an argument name");
                return;
            }
            if (c->name_count < MAX_TEMPS)
                snprintf(c->names[c->name_count++], 64, "%.63s", c->token.text);
            /*
             *  A method's arguments are names in scope zero exactly as its
             *  temporaries are.  Missing them here was invisible in every
             *  doIt, because a doIt takes none -- so the first thing to
             *  notice was a class-side method answering "nil metres".
             */
            if (c->dialect == ST_DIALECT_CLOSURES)
                declare(c, c->token.text, 1);
            ++c->argument_count;
            advance(c);
        }
        return;
    }
    fail(c, "expected a method pattern");
}

/*
 *  The selector a method's source declares, without compiling it.
 *
 *  Flattening a trait needs to know whether the using class already defines
 *  the selector BEFORE it compiles the trait's version, because the class's
 *  own method wins and compiling to find out would have installed it.
 *
 *  It runs the real pattern parser over the real lexer rather than a second
 *  little scanner, so a comment before the pattern, a keyword message split
 *  over lines and a selector like "|" are read exactly as the compiler
 *  reads them -- there is no second grammar here to drift out of step.
 */
int
COMPILE_selector_of(const char *source, char *out, size_t out_len)
{
    st_compiler         c;
    st_compiled_code    code;

    memset(&c, 0, sizeof c);
    memset(&code, 0, sizeof code);
    c.out     = &code;
    c.dialect = ST_DIALECT_BLUE_BOOK;   /*  the pattern is dialect-free  */
    c.lx      = LEX_open(source);
    if (!c.lx)
        return -1;
    LEX_set_dialect(c.lx, c.dialect);
    advance(&c);
    compile_pattern(&c);
    LEX_close(c.lx);
    if (c.failed || !code.selector[0])
        return -1;
    snprintf(out, out_len, "%s", code.selector);
    return 0;
}

int
COMPILE_to_bytecodes(const char *source, const st_compile_context *ctx,
                     st_compiled_code *out)
{
    st_compiler c;
    int         pass;

    memset(out, 0, sizeof *out);
    memset(&c, 0, sizeof c);
    c.ctx     = ctx;
    c.out     = out;
    c.dialect = ctx->dialect;

    /*
     *  The Blue Book dialect runs once.  The closure dialect runs the same
     *  parser twice: numCopied, which names are shared, and every frame's
     *  index map are whole-method facts, and all three are needed before the
     *  first byte of the first block can be emitted.
     *
     *  Twice through the parser rather than an AST because the set of blocks
     *  that are real blocks -- as opposed to the bodies of inlined
     *  conditionals and loops -- is decided by this parser, with two
     *  rewind-and-retry sites in it.  An independent analysis would have to
     *  re-derive those decisions and agree with them forever; the same
     *  parser agrees by construction.
     */
    for (pass = 0; pass <= (c.dialect == ST_DIALECT_CLOSURES); ++pass) {
        c.pass          = pass;
        c.loop_nil_end  = NO_LOOP_NIL;
        c.store_end     = NO_STORE;
        c.max_names     = 0;
        c.name_count    = 0;
        c.argument_count = 0;
        c.used_super    = 0;
        c.failed        = 0;
        c.block_seen    = 0;
        c.current_scope = 0;
        c.pragma_count  = 0;
        out->length        = 0;
        out->literal_count = 0;
        out->error[0]      = '\0';
        c.decl_visible = 0;
        c.decl_seen    = 0;
        if (pass == 0) {
            c.decl_count  = 0;
            c.need_count  = 0;
            c.scope_count = 1;
            memset(&c.scopes[0], 0, sizeof c.scopes[0]);
        }

        c.lx = LEX_open(source);
        if (c.lx)
            LEX_set_dialect(c.lx, c.dialect);
        if (!c.lx) {
            snprintf(out->error, sizeof out->error, "out of memory");
            return -1;
        }
        advance(&c);
        compile_pattern(&c);

        /*
         *  Temporaries and pragmas, in either order and any number of
         *  times.
         *
         *  The Blue Book puts the temporaries first and has one pragma.
         *  Pharo writes the pragma first at least as often, and a reader
         *  that insists on one order rejects perfectly ordinary source for
         *  a reason that is about nothing.  Looping over both until neither
         *  matches accepts every arrangement and costs a few lines.
         */
        for (;;) {
            int progress = 0;

            if (at(&c, ST_TOK_BAR)) {
                advance(&c);
                while (at(&c, ST_TOK_IDENTIFIER)) {
                    if (c.name_count < MAX_TEMPS)
                        snprintf(c.names[c.name_count++], 64, "%.63s",
                                 c.token.text);
                    if (c.dialect == ST_DIALECT_CLOSURES)
                        declare(&c, c.token.text, 0);
                    advance(&c);
                }
                if (!accept(&c, ST_TOK_BAR))
                    fail(&c, "expected | after temporaries");
                progress = 1;
            }
            /*
             *  A pragma is read speculatively, because '<' is also an
             *  ordinary binary selector and a method may begin with one --
             *  "x < 3 ifTrue: [...]" as the first statement of a method
             *  with no temporaries.  A parse that does not reach a closing
             *  '>' rewinds and the statement compiler gets the token back.
             */
            while (!c.failed && at(&c, ST_TOK_BINARY)
                && strcmp(c.token.text, "<") == 0) {
                compiler_mark   before_pragma;

                mark(&c, &before_pragma);
                if (!parse_pragma(&c)) {
                    if (c.failed)
                        break;
                    rewind_to(&c, &before_pragma);
                    goto done_prelude;
                }
                progress = 1;
            }
            if (!progress || c.failed)
                break;
        }
    done_prelude:

        /*
         *  The method's own frame, laid out once its names are known.  Its
         *  prologue builds the vector if any block shares a variable with
         *  it, and moves any shared argument into it.
         */
        if (c.dialect == ST_DIALECT_CLOSURES && pass == 1) {
            scope_info *method_scope = &c.scopes[0];
            unsigned    i;

            if (method_scope->has_vector) {
                emit(&c, 138);
                emit(&c, (uint8_t) method_scope->vector_size);
                emit_store_temporary(&c, method_scope->vector_slot, 1);
            }
            for (i = 0; i < c.decl_count; ++i) {
                unsigned    k;
                unsigned    position = 0;

                if (c.decls[i].scope != 0 || !c.decls[i].is_argument
                 || !c.decls[i].remote)
                    continue;
                for (k = 0; k < i; ++k) {
                    if (c.decls[k].scope == 0 && c.decls[k].is_argument)
                        ++position;
                }
                emit_push_temporary(&c, position);
                emit_store_remote(&c, c.decls[i].slot,
                                  method_scope->vector_slot, 1);
            }
        }

        compile_statements(&c, 0);

        /*
         *  A method with no explicit return answers the receiver, which the
         *  one-byte "return self" bytecode does directly.
         */
        if (!c.failed) {
            if (out->length == 0 || out->bytecodes[out->length - 1] != 124)
                emit(&c, 120);
        }
        add_method_state_literal(&c);
        append_method_class_literal(&c);
        LEX_close(c.lx);

        if (c.failed)
            return -1;
        if (pass == 0)
            plan_frames(&c);
        if (c.failed)
            return -1;
    }

    out->argument_count  = c.argument_count;
    if (c.dialect == ST_DIALECT_CLOSURES) {
        /*
         *  The method's frame holds its arguments, whatever it copied (it
         *  copies nothing -- it is the outermost scope), its vector and its
         *  local temporaries.  A shared name is in the vector rather than a
         *  slot, so it is not counted twice.
         */
        out->temporary_count = c.scopes[0].frame_size;
    }  else  {
        if (c.name_count > c.max_names)
            c.max_names = c.name_count;
        out->temporary_count = c.max_names;
    }
    /*
     *  Temporaries and stack share the frame, so both count.  Twelve slots
     *  is what a small context has past its fixed fields; anything more
     *  needs the large one.
     */
    out->needs_large_context =
        (out->temporary_count + max_stack_depth(out) > 12);

    return c.failed ? -1 : 0;
}

/*  ----------  Building the CompiledMethod  ----------  */

/*
 *  The method header is a SmallInteger whose fields the interpreter reads
 *  with Blue Book bit numbering, so the shifts here are the mirror image of
 *  the accessors in interp.h:
 *
 *      flag value      bits 0..2   ->  (p >> 13) & 7
 *      temp count      bits 3..7   ->  (p >>  8) & 31
 *      large context   bit  8      ->  (p >>  7) & 1
 *      literal count   bits 9..14  ->  (p >>  1) & 63
 *
 *  Flag 7 means a header extension follows in the next-to-last literal,
 *  carrying the argument count and the primitive index.  Anything with a
 *  primitive needs it; so does a method of more than four arguments.
 */
static st_oop
build_header(unsigned flag, unsigned temps, unsigned large, unsigned literals)
{
    uint64_t    bits = 0;

    bits |= (uint64_t) (flag & 7) << 13;
    bits |= (uint64_t) (temps & 31) << 8;
    bits |= (uint64_t) (large & 1) << 7;
    bits |= (uint64_t) (literals & 63) << 1;
    bits |= 1;                          /*  the SmallInteger tag  */
    return (st_oop) bits;
}

int
COMPILE_method(const char *source, const st_compile_context *ctx,
               st_compile_result *out)
{
    st_compiled_code    code;
    st_oop              method;
    unsigned            literals;
    unsigned            flag;
    unsigned            i;
    unsigned            byte_start;
    unsigned            total_bytes;

    memset(out, 0, sizeof *out);
    if (COMPILE_to_bytecodes(source, ctx, &code) != 0) {
        snprintf(out->error, sizeof out->error, "%s", code.error);
        out->error_line = code.error_line;
        out->method     = ST_OOP_INVALID;
        return -1;
    }
    snprintf(out->selector, sizeof out->selector, "%s", code.selector);
    out->argument_count  = code.argument_count;
    out->temporary_count = code.temporary_count;
    out->primitive       = code.primitive;

    literals = code.literal_count;
    if (!code.primitive_encodable)
        code.primitive = 0;             /*  kept in out->primitive below  */
    if (code.primitive != 0 || code.argument_count > 4) {
        flag = 7;
        ++literals;                     /*  room for the header extension  */
    }  else  {
        flag = code.argument_count;
    }

    /*
     *  A CompiledMethod is a byte object whose leading words are the header
     *  and the literal frame.  The object memory stores those words at the
     *  same addresses either way, so the layout is identical in both builds.
     */
    byte_start  = (literals + 1) * (unsigned) sizeof(st_oop);
    /*
     *  Three bytes past the bytecodes: the source pointer.
     *
     *  Chapter 27 keeps it in the method's trailer -- the last three bytes
     *  hold a position into the sources, and the top two bits of the last
     *  say which file.  Zero means "no source", which is what these are
     *  until something fills them in.  The interpreter never reads them
     *  because it stops at a return long before.
     */
    total_bytes = byte_start + code.length + 3;

    method = OM_instantiate_bytes(ST_CLASS_COMPILED_METHOD, total_bytes);
    if (!OM_is_object(method)) {
        snprintf(out->error, sizeof out->error, "out of memory");
        out->method = ST_OOP_INVALID;
        return -1;
    }
    OM_store_pointer(0, method,
                     build_header(flag, code.temporary_count,
                                  code.needs_large_context, literals));
    if (flag == 7) {
        /*
         *  The frame is [ ...the method's own literals, extension, class ].
         *
         *  The extension is the next-to-last literal: argument count in bits
         *  2..6, primitive index in bits 7..14, in Blue Book numbering.  The
         *  compiler has already made the class association the last of its
         *  literals, so the extension is spliced in ahead of it -- writing it
         *  at the end instead simply overwrites the class, which costs
         *  nothing until a super send goes looking for it and finds a Symbol.
         */
        uint64_t    extension = 1;
        unsigned    n = code.literal_count;

        extension |= (uint64_t) (code.argument_count & 31) << 9;
        extension |= (uint64_t) (code.primitive & 255) << 1;

        for (i = 0; i + 1 < n; ++i)
            OM_store_pointer(1 + i, method, code.literals[i]);
        OM_store_pointer(n, method, (st_oop) extension);
        if (n > 0)
            OM_store_pointer(n + 1, method, code.literals[n - 1]);
    }  else  {
        for (i = 0; i < code.literal_count; ++i)
            OM_store_pointer(1 + i, method, code.literals[i]);
    }
    for (i = 0; i < code.length; ++i)
        OM_store_byte(byte_start + i, method, code.bytecodes[i]);

    out->method = method;
    return 0;
}

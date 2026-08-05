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

/*  Save and restore enough to compile a stretch of source a second time.  */
typedef struct {
    st_lexer_state  lexer;
    st_token        token;
    unsigned        length;
    unsigned        literal_count;
    unsigned        name_count;
} compiler_mark;

typedef struct {
    st_lexer                   *lx;
    st_token                    token;      /*  the current token  */
    const st_compile_context   *ctx;
    st_compiled_code           *out;

    /*  Argument and temporary names, arguments first as the frame expects. */
    char        names[MAX_TEMPS][64];
    unsigned    name_count;
    unsigned    argument_count;

    /*
     *  Where an inlined loop's nil answer was emitted, if the last thing
     *  emitted was one.  A loop in statement position has no value anyone
     *  wants, and pushing nil only to pop it off is two dead bytes that the
     *  1983 compiler does not emit -- see discard_statement_value.
     */
    size_t      loop_nil_end;

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

static void
emit_store_temporary(st_compiler *c, unsigned index, int pop)
{
    if (pop && index < 8) {
        emit(c, (uint8_t) (104 + index));
        return;
    }
    emit(c, (uint8_t) (pop ? 130 : 129));
    emit(c, (uint8_t) (0x40 | (index & 63)));
}

static void
emit_store_receiver_variable(st_compiler *c, unsigned index, int pop)
{
    if (pop && index < 8) {
        emit(c, (uint8_t) (96 + index));
        return;
    }
    emit(c, (uint8_t) (pop ? 130 : 129));
    emit(c, (uint8_t) (index & 63));
}

static void
emit_store_literal_variable(st_compiler *c, st_oop association, int pop)
{
    unsigned    index = literal_index(c, association);

    emit(c, (uint8_t) (pop ? 130 : 129));
    emit(c, (uint8_t) (0xC0 | (index & 63)));
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
    VAR_NIL, VAR_TRUE, VAR_FALSE
} var_kind;

typedef struct {
    var_kind    kind;
    unsigned    index;
    st_oop      association;
} var_ref;

static var_ref
resolve(st_compiler *c, const char *name)
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

static void
emit_push_variable(st_compiler *c, const var_ref *v, const char *name)
{
    switch (v->kind) {
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
        case ST_TOK_SYMBOL:
        case ST_TOK_IDENTIFIER:
        case ST_TOK_KEYWORD:
        case ST_TOK_BINARY:
            element = c->ctx->intern_symbol(c->token.text, c->ctx->user);
            advance(c);
            break;
        case ST_TOK_LPAREN:
        case ST_TOK_ARRAY_OPEN:
            advance(c);
            element = parse_literal_array(c);
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
    case ST_TOK_LBRACKET: {
        /*
         *  A block compiles to blockCopy: followed by a jump over its body.
         *  The jump is what the interpreter's initial instruction pointer
         *  skips, which is why primitive 80 adds three to the pointer.
         */
        unsigned    argc = 0;
        unsigned    first_arg = c->name_count;
        unsigned    jump_at;

        advance(c);
        while (at(c, ST_TOK_COLON)) {
            advance(c);
            if (!at(c, ST_TOK_IDENTIFIER)) {
                fail(c, "expected a block argument name");
                return;
            }
            if (c->name_count < MAX_TEMPS) {
                snprintf(c->names[c->name_count], 64, "%.63s", c->token.text);
                ++c->name_count;
            }
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
        if (argc > 0 && !at(c, ST_TOK_RBRACKET) && !accept(c, ST_TOK_BAR)) {
            fail(c, "expected | after block arguments");
            return;
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

        /*  Block arguments arrive already stored in the block's frame.  */
        {
            unsigned    i;

            for (i = 0; i < argc; ++i)
                emit_store_temporary(c, first_arg + argc - 1 - i, 1);
        }
        compile_statements(c, 1);
        emit(c, 125);                       /*  return stack top from block  */
        if (!accept(c, ST_TOK_RBRACKET)) {
            fail(c, "expected ] closing a block");
            return;
        }
        patch_jump(c, jump_at);

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

static void
compile_inline_block(st_compiler *c)
{
    advance(c);                         /*  past [  */
    compile_statements(c, 1);
    if (!accept(c, ST_TOK_RBRACKET))
        fail(c, "expected ] closing an inlined block");
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

    branch = emit_jump_placeholder(c,
                    jump_on_false ? JUMP_IF_FALSE : JUMP_IF_TRUE);
    compile_inline_block(c);

    /*  A second keyword makes it the two-armed form.  */
    if (at(c, ST_TOK_KEYWORD)) {
        snprintf(second, sizeof second, "%.63s", c->token.text);
        if ((jump_on_false && strcmp(second, "ifFalse:") == 0)
         || (!jump_on_false && strcmp(second, "ifTrue:") == 0)) {
            advance(c);
            if (!at_inlinable_block(c)) {
                fail(c, "the second arm of %s%s must be a literal block",
                     first, second);
                return 1;
            }
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
        if (!at_inlinable_block(c)) {
            fail(c, "the body of %s must be a literal block", selector);
            return 1;
        }
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
            v = resolve(c, name);
            switch (v.kind) {
            case VAR_TEMPORARY: emit_store_temporary(c, v.index, 0); break;
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
            compiler_mark   after_block;

            compile_primary(c, &v);
            mark(c, &after_block);
            if (at(c, ST_TOK_KEYWORD) || at(c, ST_TOK_IDENTIFIER)) {
                const char *sel = c->token.text;

                if (strncmp(sel, "whileTrue", 9) == 0
                 || strncmp(sel, "whileFalse", 10) == 0) {
                    rewind_to(c, &before_receiver);
                    if (compile_inline_while(c))
                        return;
                    rewind_to(c, &before_receiver);
                    compile_primary(c, &v);
                }  else  {
                    rewind_to(c, &after_block);
                }
            }
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
            ++c->argument_count;
            advance(c);
        }
        return;
    }
    fail(c, "expected a method pattern");
}

int
COMPILE_to_bytecodes(const char *source, const st_compile_context *ctx,
                     st_compiled_code *out)
{
    st_compiler c;

    memset(out, 0, sizeof *out);
    memset(&c, 0, sizeof c);
    c.loop_nil_end = NO_LOOP_NIL;
    c.ctx = ctx;
    c.out = out;
    c.lx  = LEX_open(source);
    if (!c.lx) {
        snprintf(out->error, sizeof out->error, "out of memory");
        return -1;
    }
    advance(&c);
    compile_pattern(&c);

    /*  Temporaries, if any.  */
    if (at(&c, ST_TOK_BAR)) {
        advance(&c);
        while (at(&c, ST_TOK_IDENTIFIER)) {
            if (c.name_count < MAX_TEMPS)
                snprintf(c.names[c.name_count++], 64, "%.63s", c.token.text);
            advance(&c);
        }
        if (!accept(&c, ST_TOK_BAR))
            fail(&c, "expected | after temporaries");
    }

    /*  A primitive pragma, if any.  */
    if (at(&c, ST_TOK_PRIMITIVE)) {
        out->primitive = c.token.primitive;
        advance(&c);
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
    append_method_class_literal(&c);

    out->argument_count  = c.argument_count;
    out->temporary_count = c.name_count;
    /*
     *  Temporaries and stack share the frame, so both count.  Twelve slots
     *  is what a small context has past its fixed fields; anything more
     *  needs the large one.
     */
    out->needs_large_context =
        (c.name_count + max_stack_depth(out) > 12);

    LEX_close(c.lx);
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
    total_bytes = byte_start + code.length;

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

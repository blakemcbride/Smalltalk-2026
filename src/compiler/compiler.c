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

typedef struct {
    st_lexer                   *lx;
    st_token                    token;      /*  the current token  */
    const st_compile_context   *ctx;
    st_compiled_code           *out;

    /*  Argument and temporary names, arguments first as the frame expects. */
    char        names[MAX_TEMPS][64];
    unsigned    name_count;
    unsigned    argument_count;

    int         failed;
} st_compiler;

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
 *  Jumps.  The distance is unknown when the jump is emitted, so a
 *  placeholder goes down and is patched once the target is reached.  Both
 *  the one-byte and two-byte forms exist; we always emit the two-byte form
 *  for forward jumps and choose the short form only when the distance is
 *  known to fit.
 */
static unsigned
emit_jump_placeholder(st_compiler *c, int on_false)
{
    unsigned    at_byte = c->out->length;

    /*  172..175 pop and jump on false; 160..167 unconditional.  */
    emit(c, (uint8_t) (on_false ? 172 : 164));
    emit(c, 0);
    return at_byte;
}

static void
patch_jump(st_compiler *c, unsigned at_byte)
{
    int32_t     distance;
    uint8_t     opcode;

    if (c->failed)
        return;
    distance = (int32_t) c->out->length - (int32_t) (at_byte + 2);
    opcode   = c->out->bytecodes[at_byte];
    if (opcode >= 172) {
        if (distance < 0 || distance > 1023) {
            fail(c, "conditional jump out of range");
            return;
        }
        c->out->bytecodes[at_byte]     = (uint8_t) (172 + (distance >> 8));
        c->out->bytecodes[at_byte + 1] = (uint8_t) (distance & 255);
    }  else  {
        if (distance < -1024 || distance > 1023) {
            fail(c, "jump out of range");
            return;
        }
        c->out->bytecodes[at_byte]     = (uint8_t) (164 + (distance >> 8));
        c->out->bytecodes[at_byte + 1] = (uint8_t) (distance & 255);
    }
}

/*
 *  A backward jump, for loops.  Unused until whileTrue: is compiled inline;
 *  kept because the encoding is the awkward part and it is already right.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static void
emit_jump_back_to(st_compiler *c, unsigned target)
{
    int32_t     distance = (int32_t) target - (int32_t) (c->out->length + 2);

    if (distance < -1024) {
        fail(c, "loop is too long");
        return;
    }
    emit(c, (uint8_t) (164 + ((distance >> 8) & 7)));
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

    /*  Arguments and temporaries shadow instance variables, which shadow
     *  globals -- innermost scope first.  */
    for (i = 0; i < c->name_count; ++i) {
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
            element = OM_fetch_pointer((uint32_t) c->token.integer,
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
            OM_fetch_pointer((uint32_t) c->token.integer, ST_CHARACTER_TABLE));
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
        if (argc > 0 && !accept(c, ST_TOK_BAR)) {
            fail(c, "expected | after block arguments");
            return;
        }

        emit(c, 112);                       /*  push self (the home)  */
        emit_push_integer(c, (int64_t) argc);
        emit(c, 200);                       /*  blockCopy:  */
        jump_at = emit_jump_placeholder(c, 0);

        /*  Block arguments arrive already stored in the block's frame.  */
        {
            unsigned    i;

            for (i = 0; i < argc; ++i)
                emit_store_temporary(c, first_arg + argc - 1 - i, 1);
        }
        compile_statements(c, 1);
        emit(c, 125);                       /*  return stack top from block  */
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
 *  Inlined control flow.  The Blue Book compiler turns these into jumps, and
 *  the reference traces confirm it: a conditional appears as a jump
 *  bytecode, never as a send of ifTrue:.
 */
static int
try_inline_conditional(st_compiler *c, const char *selector)
{
    if (strcmp(selector, "ifTrue:") == 0 || strcmp(selector, "ifFalse:") == 0) {
        unsigned    jump_at;
        int         on_false = (selector[2] == 'T');

        if (!at(c, ST_TOK_LBRACKET))
            return 0;                   /*  not a literal block: send it  */
        jump_at = emit_jump_placeholder(c, on_false);
        advance(c);                     /*  past [  */
        compile_statements(c, 1);
        if (!accept(c, ST_TOK_RBRACKET)) {
            fail(c, "expected ] closing a conditional");
            return 1;
        }
        {
            /*  The value of the whole expression is nil when not taken.  */
            unsigned    skip = emit_jump_placeholder(c, 0);

            patch_jump(c, jump_at);
            emit(c, 115);               /*  push nil  */
            patch_jump(c, skip);
        }
        return 1;
    }
    return 0;
}

static void
compile_unary_sequence(st_compiler *c, int receiver_is_super)
{
    while (at(c, ST_TOK_IDENTIFIER)) {
        char    selector[256];

        snprintf(selector, sizeof selector, "%s", c->token.text);
        advance(c);
        emit_send(c, selector, 0, receiver_is_super);
        receiver_is_super = 0;
    }
}

static void
compile_binary_sequence(st_compiler *c, int receiver_is_super)
{
    compile_unary_sequence(c, receiver_is_super);
    while (at(c, ST_TOK_BINARY) || at(c, ST_TOK_BAR)) {
        char    selector[256];

        /*  A bar in operator position is the binary selector, not a
         *  temporaries separator; those only appear at the head of a
         *  method or block body.  */
        snprintf(selector, sizeof selector, "%s",
                 at(c, ST_TOK_BAR) ? "|" : c->token.text);
        advance(c);
        compile_primary(c, NULL);
        compile_unary_sequence(c, 0);
        emit_send(c, selector, 1, 0);
    }
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
    char        selector[256] = "";
    unsigned    argc = 0;

    compile_binary_sequence(c, receiver_is_super);
    if (!at(c, ST_TOK_KEYWORD))
        return;

    /*  Try the inlined forms before building a real send.  */
    {
        char    first[256];
        st_token look;

        snprintf(first, sizeof first, "%s", c->token.text);
        LEX_peek(c->lx, &look);
        if ((strcmp(first, "ifTrue:") == 0 || strcmp(first, "ifFalse:") == 0)
         && look.kind == ST_TOK_LBRACKET) {
            st_token    saved = c->token;

            advance(c);
            if (try_inline_conditional(c, first)) {
                /*  A trailing ifFalse: after ifTrue: is not handled by the
                 *  simple form above; fall back to a send if one appears.  */
                if (at(c, ST_TOK_KEYWORD))
                    fail(c, "chained %s is not compiled inline yet",
                         c->token.text);
                return;
            }
            c->token = saved;
        }
    }

    while (at(c, ST_TOK_KEYWORD)) {
        size_t  used = strlen(selector);

        snprintf(selector + used, sizeof selector - used, "%s", c->token.text);
        advance(c);
        compile_binary_expression(c);
        ++argc;
    }
    emit_send(c, selector, argc, receiver_is_super);
}

static void
compile_cascade(st_compiler *c)
{
    while (at(c, ST_TOK_SEMICOLON)) {
        /*
         *  A cascade re-sends to the receiver of the last message.  The
         *  receiver was duplicated before that send, so it is still below
         *  the result: drop the result and send again.
         */
        advance(c);
        emit(c, 135);                   /*  pop the previous result  */
        emit(c, 136);                   /*  duplicate the receiver   */
        compile_keyword_message(c, 0);
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
        int         is_super = at(c, ST_TOK_IDENTIFIER)
                            && strcmp(c->token.text, "super") == 0;
        var_ref     v;

        compile_primary(c, &v);
        compile_keyword_message(c, is_super);
        compile_cascade(c);
    }
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
                emit(c, 135);
                emitted = 0;
                break;
            }
            emit(c, 135);               /*  discard the statement's value  */
            emitted = 0;
            continue;
        }
        break;
    }
    if (inside_block && !emitted)
        emit(c, 115);                   /*  an empty block answers nil  */
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

    out->argument_count  = c.argument_count;
    out->temporary_count = c.name_count;
    out->needs_large_context = (c.name_count + 8 > 12);

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
    for (i = 0; i < code.literal_count; ++i)
        OM_store_pointer(1 + i, method, code.literals[i]);
    if (flag == 7) {
        /*
         *  The extension is the next-to-last literal: argument count in bits
         *  2..6, primitive index in bits 7..14, again in Blue Book numbering.
         */
        uint64_t    extension = 1;

        extension |= (uint64_t) (code.argument_count & 31) << 9;
        extension |= (uint64_t) (code.primitive & 255) << 1;
        OM_store_pointer(literals - 1, method, (st_oop) extension);
    }
    for (i = 0; i < code.length; ++i)
        OM_store_byte(byte_start + i, method, code.bytecodes[i]);

    out->method = method;
    return 0;
}

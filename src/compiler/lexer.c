/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Smalltalk-80 lexer.  See lexer.h for the syntax notes.
 */

#include "lexer.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct st_lexer {
    const char *source;
    size_t      pos;
    size_t      length;
    unsigned    line;
    int         has_peek;
    st_token    peeked;
    st_token_kind last_kind;    /*  what came before, for the minus rule  */
    char        error[160];
};

/*  The characters Smalltalk-80 allows in a binary selector.  */
static int
is_binary_char(int c)
{
    return strchr("+-*/~<>=&|@%,?!\\", c) != NULL;
}

st_lexer *
LEX_open(const char *source)
{
    st_lexer   *lx = (st_lexer *) calloc(1, sizeof *lx);

    if (!lx)
        return NULL;
    lx->source = source;
    lx->length = strlen(source);
    lx->line   = 1;
    return lx;
}

void
LEX_close(st_lexer *lx)
{
    free(lx);
}

void
LEX_save(st_lexer *lx, st_lexer_state *out)
{
    out->pos       = lx->pos;
    out->line      = lx->line;
    out->has_peek  = lx->has_peek;
    out->peeked    = lx->peeked;
    out->last_kind = lx->last_kind;
}

void
LEX_restore(st_lexer *lx, const st_lexer_state *state)
{
    lx->pos       = state->pos;
    lx->line      = state->line;
    lx->has_peek  = state->has_peek;
    lx->peeked    = state->peeked;
    lx->last_kind = state->last_kind;
}

const char *
LEX_error(const st_lexer *lx)
{
    return (lx && lx->error[0]) ? lx->error : NULL;
}

static void
lex_fail(st_lexer *lx, st_token *out, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(lx->error, sizeof lx->error, fmt, ap);
    va_end(ap);
    out->kind = ST_TOK_ERROR;
    snprintf(out->text, sizeof out->text, "%s", lx->error);
}

static int
at_end(st_lexer *lx)
{
    return lx->pos >= lx->length;
}

static char
peek_char(st_lexer *lx, size_t ahead)
{
    if (lx->pos + ahead >= lx->length)
        return '\0';
    return lx->source[lx->pos + ahead];
}

/*
 *  Skip whitespace and comments.  A comment is delimited by double quotes,
 *  and a doubled quote inside it is a literal one, so comments can quote.
 */
static void
skip_blanks(st_lexer *lx)
{
    for (;;) {
        char    c;

        while (!at_end(lx)) {
            c = lx->source[lx->pos];
            /*
             *  A line ends on either, and a CRLF pair ends one line rather
             *  than two.  Source read through the chunk reader arrives as
             *  carriage returns; an expression handed straight to -eval
             *  arrives however the shell wrote it.
             */
            if (c == '\r' || (c == '\n' && (lx->pos == 0
                                || lx->source[lx->pos - 1] != '\r')))
                ++lx->line;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f')
                ++lx->pos;
            else
                break;
        }
        if (at_end(lx) || lx->source[lx->pos] != '"')
            return;
        ++lx->pos;
        while (!at_end(lx)) {
            if (lx->source[lx->pos] == '"') {
                if (peek_char(lx, 1) == '"') {
                    lx->pos += 2;
                    continue;
                }
                ++lx->pos;
                break;
            }
            if (lx->source[lx->pos] == '\n')
                ++lx->line;
            ++lx->pos;
        }
    }
}

/*
 *  Numbers: an optional radix, digits, an optional fraction and exponent.
 *  16rFF is 255; 2r1010 is 10.  Digits above 9 are written as letters.
 */
static void
scan_number(st_lexer *lx, st_token *out, int negative)
{
    size_t      start = lx->pos;
    int64_t     value = 0;
    int         radix = 10;
    int         is_float = 0;
    double      real;

    while (!at_end(lx) && isdigit((unsigned char) lx->source[lx->pos])) {
        value = value * 10 + (lx->source[lx->pos] - '0');
        ++lx->pos;
    }
    if (!at_end(lx) && lx->source[lx->pos] == 'r' && value >= 2 && value <= 36) {
        radix = (int) value;
        value = 0;
        ++lx->pos;
        while (!at_end(lx)) {
            char    c = lx->source[lx->pos];
            int     digit;

            if (isdigit((unsigned char) c))
                digit = c - '0';
            else if (c >= 'A' && c <= 'Z')
                digit = c - 'A' + 10;
            else
                break;
            if (digit >= radix)
                break;
            value = value * radix + digit;
            ++lx->pos;
        }
        out->kind    = ST_TOK_INTEGER;
        out->integer = negative ? -value : value;
        return;
    }
    /*  A period only belongs to the number if a digit follows it.  */
    if (!at_end(lx) && lx->source[lx->pos] == '.'
     && isdigit((unsigned char) peek_char(lx, 1))) {
        is_float = 1;
        ++lx->pos;
        while (!at_end(lx) && isdigit((unsigned char) lx->source[lx->pos]))
            ++lx->pos;
    }
    if (!at_end(lx) && (lx->source[lx->pos] == 'e' || lx->source[lx->pos] == 'd')) {
        size_t  save = lx->pos;

        ++lx->pos;
        if (!at_end(lx) && (lx->source[lx->pos] == '-' || lx->source[lx->pos] == '+'))
            ++lx->pos;
        if (!at_end(lx) && isdigit((unsigned char) lx->source[lx->pos])) {
            is_float = 1;
            while (!at_end(lx) && isdigit((unsigned char) lx->source[lx->pos]))
                ++lx->pos;
        }  else  {
            lx->pos = save;
        }
    }
    if (!is_float) {
        out->kind    = ST_TOK_INTEGER;
        out->integer = negative ? -value : value;
        return;
    }
    {
        char    buf[128];
        size_t  n = lx->pos - start;

        if (n >= sizeof buf)
            n = sizeof buf - 1;
        memcpy(buf, lx->source + start, n);
        buf[n] = '\0';
        real = strtod(buf, NULL);
    }
    out->kind = ST_TOK_FLOAT;
    out->real = negative ? -real : real;
}

static void
scan_word(st_lexer *lx, char *buf, size_t buflen)
{
    size_t  n = 0;

    while (!at_end(lx)) {
        char    c = lx->source[lx->pos];

        if (!isalnum((unsigned char) c))
            break;
        if (n + 1 < buflen)
            buf[n++] = c;
        ++lx->pos;
    }
    buf[n] = '\0';
}

static int
lex_token(st_lexer *lx, st_token *out)
{
    char    c;

    {
        size_t  before = lx->pos;

        memset(out, 0, sizeof *out);
        skip_blanks(lx);
        out->after_space = (lx->pos != before) || before == 0;
    }
    out->line = lx->line;
    if (at_end(lx)) {
        out->kind = ST_TOK_END;
        return 0;
    }
    c = lx->source[lx->pos];

    /*  Identifiers and keywords.  */
    if (isalpha((unsigned char) c)) {
        size_t  n = 0;

        while (!at_end(lx)) {
            char    d = lx->source[lx->pos];

            if (!isalnum((unsigned char) d))
                break;
            if (n + 1 < sizeof out->text)
                out->text[n++] = d;
            ++lx->pos;
        }
        out->text[n] = '\0';
        /*
         *  A trailing colon makes it a keyword, unless the colon is part of
         *  an assignment -- "x := 1" must not lex "x:" as a keyword.
         */
        if (!at_end(lx) && lx->source[lx->pos] == ':' && peek_char(lx, 1) != '=') {
            if (n + 2 < sizeof out->text) {
                out->text[n]     = ':';
                out->text[n + 1] = '\0';
            }
            ++lx->pos;
            out->kind = ST_TOK_KEYWORD;
            return 1;
        }
        out->kind = ST_TOK_IDENTIFIER;
        return 1;
    }

    if (isdigit((unsigned char) c)) {
        scan_number(lx, out, 0);
        return 1;
    }

    switch (c) {
    case '$':
        ++lx->pos;
        if (at_end(lx)) {
            lex_fail(lx, out, "line %u: $ at end of input", lx->line);
            return 1;
        }
        out->kind    = ST_TOK_CHARACTER;
        out->integer = (unsigned char) lx->source[lx->pos];
        out->text[0] = lx->source[lx->pos];
        out->text[1] = '\0';
        ++lx->pos;
        return 1;

    case '\'': {
        size_t  n = 0;

        ++lx->pos;
        for (;;) {
            if (at_end(lx)) {
                lex_fail(lx, out, "line %u: unterminated string", out->line);
                return 1;
            }
            if (lx->source[lx->pos] == '\'') {
                if (peek_char(lx, 1) == '\'') {
                    if (n + 1 < sizeof out->text)
                        out->text[n++] = '\'';
                    lx->pos += 2;
                    continue;
                }
                ++lx->pos;
                break;
            }
            if (lx->source[lx->pos] == '\n')
                ++lx->line;
            if (n + 1 < sizeof out->text)
                out->text[n++] = lx->source[lx->pos];
            ++lx->pos;
        }
        out->text[n] = '\0';
        out->kind    = ST_TOK_STRING;
        return 1;
    }

    case '#':
        ++lx->pos;
        if (!at_end(lx) && lx->source[lx->pos] == '(') {
            ++lx->pos;
            out->kind = ST_TOK_ARRAY_OPEN;
            return 1;
        }
        if (!at_end(lx) && lx->source[lx->pos] == '\'') {
            /*  #'with spaces' is a symbol spelled as a string.  */
            st_token    inner;

            lex_token(lx, &inner);
            memcpy(out->text, inner.text, sizeof out->text);
            out->kind = ST_TOK_SYMBOL;
            return 1;
        }
        if (!at_end(lx) && is_binary_char((unsigned char) lx->source[lx->pos])) {
            size_t  n = 0;

            while (!at_end(lx)
                && is_binary_char((unsigned char) lx->source[lx->pos])) {
                if (n + 1 < sizeof out->text)
                    out->text[n++] = lx->source[lx->pos];
                ++lx->pos;
            }
            out->text[n] = '\0';
            out->kind    = ST_TOK_SYMBOL;
            return 1;
        }
        {
            /*  A symbol may be several keywords run together.  */
            size_t  n = 0;

            for (;;) {
                char    word[128];

                scan_word(lx, word, sizeof word);
                if (word[0] == '\0')
                    break;
                if (n + strlen(word) + 2 < sizeof out->text) {
                    memcpy(out->text + n, word, strlen(word));
                    n += strlen(word);
                }
                if (!at_end(lx) && lx->source[lx->pos] == ':') {
                    if (n + 2 < sizeof out->text)
                        out->text[n++] = ':';
                    ++lx->pos;
                    continue;
                }
                break;
            }
            out->text[n] = '\0';
            if (n == 0) {
                lex_fail(lx, out, "line %u: # must be followed by a symbol",
                         out->line);
                return 1;
            }
            out->kind = ST_TOK_SYMBOL;
            return 1;
        }

    case '^':
        ++lx->pos;
        out->kind = ST_TOK_RETURN;
        return 1;

    case ';':
        ++lx->pos;
        out->kind = ST_TOK_SEMICOLON;
        return 1;

    case '.':
        ++lx->pos;
        out->kind = ST_TOK_PERIOD;
        return 1;

    case '(':
        ++lx->pos;
        out->kind = ST_TOK_LPAREN;
        return 1;

    case ')':
        ++lx->pos;
        out->kind = ST_TOK_RPAREN;
        return 1;

    case '[':
        ++lx->pos;
        out->kind = ST_TOK_LBRACKET;
        return 1;

    case ']':
        ++lx->pos;
        out->kind = ST_TOK_RBRACKET;
        return 1;

    case '_':
        /*
         *  The 1983 assignment arrow.  A leading underscore in an
         *  identifier was not legal Smalltalk-80, so this is unambiguous.
         */
        ++lx->pos;
        out->kind = ST_TOK_ASSIGN;
        return 1;

    case ':':
        ++lx->pos;
        if (!at_end(lx) && lx->source[lx->pos] == '=') {
            ++lx->pos;
            out->kind = ST_TOK_ASSIGN;
            return 1;
        }
        out->kind = ST_TOK_COLON;
        return 1;

    case '<': {
        /*
         *  A primitive pragma, <primitive: 60>.  Anything else beginning
         *  with < is an ordinary binary selector.
         */
        size_t      save = lx->pos;
        unsigned    line_save = lx->line;

        ++lx->pos;
        skip_blanks(lx);
        if (!at_end(lx) && strncmp(lx->source + lx->pos, "primitive:", 10) == 0) {
            unsigned    value = 0;

            lx->pos += 10;
            skip_blanks(lx);
            while (!at_end(lx) && isdigit((unsigned char) lx->source[lx->pos])) {
                value = value * 10 + (unsigned) (lx->source[lx->pos] - '0');
                ++lx->pos;
            }
            skip_blanks(lx);
            if (!at_end(lx) && lx->source[lx->pos] == '>') {
                ++lx->pos;
                out->kind      = ST_TOK_PRIMITIVE;
                out->primitive = value;
                return 1;
            }
        }
        lx->pos  = save;
        lx->line = line_save;
        break;
    }

    default:
        break;
    }

    if (c == '|') {
        /*  A lone bar separates temporaries; || would be a selector.  */
        if (!is_binary_char((unsigned char) peek_char(lx, 1))) {
            ++lx->pos;
            out->kind = ST_TOK_BAR;
            return 1;
        }
    }

    if (is_binary_char((unsigned char) c)) {
        size_t  n = 0;

        /*
         *  A minus in front of a digit belongs to the literal only where an
         *  operand cannot already have been read.  "3-4" is a send of minus;
         *  "foo: -4" and "3 + -4" carry a negative literal.  The lexer
         *  cannot see the grammar, so it decides on what came before.
         */
        if (c == '-' && isdigit((unsigned char) peek_char(lx, 1))
         && (lx->last_kind == ST_TOK_END
          || lx->last_kind == ST_TOK_BINARY
          || lx->last_kind == ST_TOK_KEYWORD
          || lx->last_kind == ST_TOK_ASSIGN
          || lx->last_kind == ST_TOK_RETURN
          || lx->last_kind == ST_TOK_LPAREN
          || lx->last_kind == ST_TOK_LBRACKET
          || lx->last_kind == ST_TOK_ARRAY_OPEN
          || lx->last_kind == ST_TOK_SEMICOLON
          || lx->last_kind == ST_TOK_PERIOD
          || lx->last_kind == ST_TOK_BAR
          || lx->last_kind == ST_TOK_COLON)) {
            ++lx->pos;
            scan_number(lx, out, 1);
            return 1;
        }
        while (!at_end(lx) && is_binary_char((unsigned char) lx->source[lx->pos])) {
            /*
             *  A minus that begins a number is not part of the selector.
             *
             *  Binary selectors are greedy, and two characters are allowed,
             *  so "@-" reads as one -- which turns "-2@-2" into "-2 @- 2"
             *  and sends #@- to a number.  The minus sign binds to a numeric
             *  literal that follows it directly, which is the same rule that
             *  makes "3-4" a send and "3 -4" two literals; it just has to be
             *  applied here too.  Cursor class>>initialize is full of
             *  offsets written that way, so no cursor could be built.
             */
            if (n > 0 && lx->source[lx->pos] == '-'
             && lx->pos + 1 < lx->length
             && isdigit((unsigned char) lx->source[lx->pos + 1]))
                break;
            if (n + 1 < sizeof out->text)
                out->text[n++] = lx->source[lx->pos];
            ++lx->pos;
            /*  Selectors are at most two characters in Smalltalk-80.  */
            if (n == 2)
                break;
        }
        out->text[n] = '\0';
        out->kind    = ST_TOK_BINARY;
        return 1;
    }

    lex_fail(lx, out, "line %u: unexpected character '%c'", out->line, c);
    ++lx->pos;
    return 1;
}

int
LEX_next(st_lexer *lx, st_token *out)
{
    int result;

    if (lx->has_peek) {
        *out = lx->peeked;
        lx->has_peek = 0;
        lx->last_kind = out->kind;
        return out->kind != ST_TOK_END;
    }
    result = lex_token(lx, out);
    lx->last_kind = out->kind;
    return result;
}

int
LEX_peek(st_lexer *lx, st_token *out)
{
    if (!lx->has_peek) {
        st_token_kind   before = lx->last_kind;

        lex_token(lx, &lx->peeked);
        lx->has_peek  = 1;
        /*
         *  Peeking must not disturb the minus rule; the peeked token is not
         *  consumed yet, so what came "before" is still what it was.
         */
        lx->last_kind = before;
    }
    *out = lx->peeked;
    return out->kind != ST_TOK_END;
}

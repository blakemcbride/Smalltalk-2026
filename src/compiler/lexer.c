/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Smalltalk-80 lexer.  See lexer.h for the syntax notes.
 */

#include "lexer.h"

#include <stdint.h>

#include <ctype.h>
#include <math.h>
#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  Text of a token too long for the token's own buffer.
 *
 *  Kept by the LEXER rather than by the token, and never freed until
 *  LEX_close, because tokens here are copied by value all the time: the
 *  compiler peeks, saves and restores them, and a token that owned its own
 *  storage would either be freed while a saved copy still pointed at it or
 *  need a copy discipline nothing else in this file has.  One allocation
 *  per over-long literal, for the life of one compile, is the cheaper half
 *  of that trade by a wide margin.
 */
typedef struct lex_overflow {
    struct lex_overflow    *next;
    char                    text[1];    /*  and as much more as it needs  */
} lex_overflow;

struct st_lexer {
    const char *source;
    size_t      pos;
    size_t      length;
    unsigned    line;
    int         has_peek;
    st_token    peeked;
    st_token_kind last_kind;    /*  what came before, for the minus rule  */
    /*
     *  Which dialect is being read.  Two things depend on it, and both are
     *  places where post-1983 Smalltalk took a character 1983 had already
     *  spent: see the underscore and the binary-selector length below.
     */
    int         dialect;
    lex_overflow *overflow;
    char        error[160];
};

/*
 *  Somewhere to accumulate a token's text while it is being scanned, with
 *  no ceiling on how long it may become.
 *
 *  The common token fits in `stack' and costs no allocation at all; the
 *  rare one that does not grows a heap buffer by doubling.  Every scanner
 *  below that used to write into a fixed array and drop the overflow --
 *  the string at 255, the symbol at 127, the identifier and the binary
 *  selector -- writes into one of these instead.
 */
typedef struct {
    char       *text;
    size_t      length;
    size_t      capacity;
    int         failed;         /*  an allocation did not happen  */
    char        stack[256];
} lex_text_buffer;

static void
buffer_open(lex_text_buffer *b)
{
    b->text     = b->stack;
    b->length   = 0;
    b->capacity = sizeof b->stack;
    b->failed   = 0;
    b->text[0]  = '\0';
}

static void
buffer_push(lex_text_buffer *b, char c)
{
    if (b->failed)
        return;
    if (b->length + 2 > b->capacity) {
        size_t  want  = b->capacity * 2;
        char   *grown = (b->text == b->stack)
                            ? (char *) malloc(want)
                            : (char *) realloc(b->text, want);

        if (!grown) {
            b->failed = 1;
            return;
        }
        if (b->text == b->stack)
            memcpy(grown, b->stack, b->length);
        b->text     = grown;
        b->capacity = want;
    }
    b->text[b->length++] = c;
}

/*
 *  Hand the accumulated text to the token and let the buffer go.
 *
 *  `text' always gets as much as it holds, so every diagnostic that prints
 *  a token still prints one; `long_text' is set only when there was more,
 *  and points into storage this lexer owns for the rest of its life.
 */
static void
buffer_close(st_lexer *lx, lex_text_buffer *b, st_token *out)
{
    size_t  keep = b->length < sizeof out->text - 1
                        ? b->length : sizeof out->text - 1;

    if (b->failed) {
        snprintf(lx->error, sizeof lx->error,
                 "line %u: out of memory reading a token", out->line);
        out->kind = ST_TOK_ERROR;
        snprintf(out->text, sizeof out->text, "%s", lx->error);
        if (b->text != b->stack)
            free(b->text);
        return;
    }
    memcpy(out->text, b->text, keep);
    out->text[keep]  = '\0';
    out->text_length = b->length;
    if (b->length >= sizeof out->text) {
        lex_overflow   *held = (lex_overflow *)
                                malloc(sizeof *held + b->length);

        if (!held) {
            b->failed = 1;
            buffer_close(lx, b, out);
            return;
        }
        memcpy(held->text, b->text, b->length);
        held->text[b->length] = '\0';
        held->next     = lx->overflow;
        lx->overflow   = held;
        out->long_text = held->text;
    }
    if (b->text != b->stack)
        free(b->text);
}

const char *
LEX_text(const st_token *tok)
{
    return tok->long_text ? tok->long_text : tok->text;
}

/*
 *  The characters Smalltalk-80 allows in a binary selector.
 *
 *  NUL is excluded by name, because strchr finds the terminator of the
 *  string it searches and so answered "yes" for a NUL byte -- which made
 *  a NUL in the source a binary selector, and a binary selector made of a
 *  byte no message could ever be spelled with.  The byte could only reach
 *  here once LEX_open_n let the lexer see past one (Bugs3 B28), but it is
 *  the wrong answer at any length.
 */
static int
is_binary_char(int c)
{
    return c != '\0' && strchr("+-*/~<>=&|@%,?!\\", c) != NULL;
}

void
LEX_set_dialect(st_lexer *lx, int dialect)
{
    lx->dialect = dialect;
}

void
LEX_begin_statement(st_lexer *lx)
{
    if (lx)
        lx->last_kind = ST_TOK_END;
}

st_lexer *
LEX_open_n(const char *source, size_t length)
{
    st_lexer   *lx = (st_lexer *) calloc(1, sizeof *lx);

    if (!lx)
        return NULL;
    lx->source = source;
    lx->length = length;
    lx->line   = 1;
    return lx;
}

/*
 *  A C string.  Everything below reads `length' and never the terminator,
 *  so this is the whole of the difference: a caller with bytes that may
 *  hold a NUL uses LEX_open_n and is read to the end (see lexer.h).
 */
st_lexer *
LEX_open(const char *source)
{
    return LEX_open_n(source, strlen(source));
}

void
LEX_close(st_lexer *lx)
{
    if (lx) {
        while (lx->overflow) {
            lex_overflow   *next = lx->overflow->next;

            free(lx->overflow);
            lx->overflow = next;
        }
    }
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
 *
 *  Answers 0 when a comment was opened and never closed, and leaves the
 *  line it opened on in `*unclosed_line' for the diagnostic.  That used to
 *  be silence: the loop ran off the end of the source and the caller saw
 *  ST_TOK_END, so `zzUnterm2 "abc' compiled as a method with no body, and
 *  a stray quote anywhere in a method swallowed the rest of it -- every
 *  statement after it gone, nothing said.  1983's Scanner refuses with
 *  "Unmatched comment quote" and so does this one now.  Bugs3 B31.
 */
static int
skip_blanks(st_lexer *lx, unsigned *unclosed_line)
{
    for (;;) {
        char        c;
        unsigned    opened;

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
            return 1;
        opened = lx->line;
        ++lx->pos;
        for (;;) {
            if (at_end(lx)) {
                *unclosed_line = opened;
                return 0;
            }
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
/*  A digit's value in a given radix, or -1 if it is not one.  */
static int
radix_digit_value(int c, int radix)
{
    int value;

    if (isdigit(c))
        value = c - '0';
    else if (c >= 'A' && c <= 'Z')
        value = c - 'A' + 10;
    else
        return -1;
    return value < radix ? value : -1;
}

/*
 *  Hand the digits on when the value did not fit.  Nothing here builds the
 *  number: the lexer has no object memory, and a wrapped int64 is exactly
 *  the wrong answer that made `18446744073709551616' evaluate to zero.
 */
static void
note_big_integer(st_token *out, st_lexer *lx, size_t from, unsigned radix,
                 int too_big)
{
    size_t  n = lx->pos - from;

    out->integer_big   = 0;
    out->integer_radix = radix;
    if (!too_big)
        return;
    /*
     *  Every digit, however many there are.  A literal with more than 255
     *  of them -- 848 bits and up -- used to lose the rest of them here and
     *  answer a number that was merely plausible, which is the same fault
     *  the string scanner had and just as quiet.
     */
    out->text_length = n;
    if (n >= sizeof out->text) {
        lex_overflow   *held = (lex_overflow *) malloc(sizeof *held + n);

        if (!held) {
            lex_fail(lx, out, "line %u: out of memory reading an integer",
                     out->line);
            return;
        }
        memcpy(held->text, lx->source + from, n);
        held->text[n]  = '\0';
        held->next     = lx->overflow;
        lx->overflow   = held;
        out->long_text = held->text;
        n = sizeof out->text - 1;
    }
    memcpy(out->text, lx->source + from, n);
    out->text[n]     = '\0';
    out->integer_big = 1;
}

static void
scan_number(st_lexer *lx, st_token *out, int negative)
{
    size_t      start = lx->pos;
    size_t      digits_start = lx->pos;
    int64_t     value = 0;
    int         radix = 10;
    int         is_float = 0;
    int         too_big = 0;
    double      real;

    while (!at_end(lx) && isdigit((unsigned char) lx->source[lx->pos])) {
        int d = lx->source[lx->pos] - '0';

        if (value > (INT64_MAX - d) / 10)
            too_big = 1;
        else
            value = value * 10 + d;
        ++lx->pos;
    }
    if (!at_end(lx) && lx->source[lx->pos] == 'r' && value >= 2 && value <= 36) {
        radix = (int) value;
        value = 0;
        too_big = 0;
        ++lx->pos;
        /*
         *  The sign of a radix number goes AFTER the r.
         *
         *  That is the Blue Book's own spelling and the one the image
         *  writes: Integer>>storeStringRadix: answers '16r-FF' for -255,
         *  and Number class>>readFrom: reads it back.  This lexer did not
         *  know the form.  The minus was left standing after `16r' with no
         *  digits, so `16r' lexed as 0 and `FF' as a variable, and the
         *  storeString of every negative radix number was unreadable by
         *  the compiler that installs every method.  Worse, `2r-101' was
         *  0 followed by the send `- 101', which answers -101 -- a wrong
         *  number rather than an error.  Bugs3 B26.
         *
         *  A minus BEFORE the radix is still the lexer's usual leading
         *  sign (`-2r101' is -5), and writing both is refused rather than
         *  cancelled, because nobody who writes `-2r-101' means 5.
         */
        if (!at_end(lx) && lx->source[lx->pos] == '-') {
            if (negative) {
                lex_fail(lx, out, "line %u: a radix number has one minus "
                                  "sign, before the radix or after the r",
                         out->line);
                return;
            }
            negative = 1;
            ++lx->pos;
        }
        digits_start = lx->pos;
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
            if (value > (INT64_MAX - digit) / radix)
                too_big = 1;
            else
                value = value * radix + digit;
            ++lx->pos;
        }
        /*
         *  Something must have been read.  The loop above stops at the
         *  first byte that is not a digit of the radix, and when that is
         *  the very first byte the number has no digits at all -- `16r'
         *  by itself, or `16rff' in lower case, which the Blue Book does
         *  not allow.  Both used to fall through with value 0 and hand the
         *  rest of the text on as the next token, so `16r' was zero and
         *  `16rff' was `0 ff'.  Bugs3 B26.
         */
        if (lx->pos == digits_start) {
            lex_fail(lx, out, "line %u: digits expected after %dr",
                     out->line, radix);
            return;
        }
        /*
         *  A radix number may have a fraction and an exponent, and the
         *  digits of both are in that radix while the EXPONENT is decimal
         *  and the power is of the radix: 2r1.1 is 1.5, and 2r1.0e-10 is
         *  2 raised to -10.  The Blue Book grammar has always said so;
         *  reading only the integer part left "2r1.1" as "2r1" followed by
         *  a statement separator, which is a wrong answer rather than an
         *  error wherever a period could legally follow.
         */
        {
            double      whole = (double) value;
            double      fraction = 0.0;
            double      scale = 1.0;
            int         radix_float = 0;

            if (!at_end(lx) && lx->source[lx->pos] == '.'
             && radix_digit_value(peek_char(lx, 1), radix) >= 0) {
                radix_float = 1;
                ++lx->pos;
                for (;;) {
                    int digit = radix_digit_value(
                                    at_end(lx) ? '\0' : lx->source[lx->pos],
                                    radix);

                    if (digit < 0)
                        break;
                    scale /= radix;
                    fraction += digit * scale;
                    ++lx->pos;
                }
            }
            if (!at_end(lx) && lx->source[lx->pos] == 'e') {
                size_t  save = lx->pos;
                int     exponent = 0;
                int     exp_negative = 0;

                ++lx->pos;
                if (!at_end(lx)
                 && (lx->source[lx->pos] == '-' || lx->source[lx->pos] == '+')) {
                    exp_negative = lx->source[lx->pos] == '-';
                    ++lx->pos;
                }
                if (!at_end(lx) && isdigit((unsigned char) lx->source[lx->pos])) {
                    while (!at_end(lx)
                        && isdigit((unsigned char) lx->source[lx->pos])) {
                        exponent = exponent * 10
                                 + (lx->source[lx->pos] - '0');
                        ++lx->pos;
                    }
                    if (exp_negative)
                        exponent = -exponent;
                    out->real = (whole + fraction) * pow((double) radix,
                                                         (double) exponent);
                    /*
                     *  The same refusal the decimal path makes below, for
                     *  the same reason: `2r1e3000' compiled to a method
                     *  that answered infinity while `1e400' was refused,
                     *  and a literal that is not the number written is
                     *  the wrong kind of quiet.  Bugs3 B32.
                     */
                    if (out->real > DBL_MAX || out->real < -DBL_MAX) {
                        size_t  shown = lx->pos - start;

                        if (shown > 120)
                            shown = 120;
                        lex_fail(lx, out, "this number is too large for a "
                                          "Float: %.*s",
                                 (int) shown, lx->source + start);
                        return;
                    }
                    if (too_big) {
                        lex_fail(lx, out, "line %u: this radix Float has "
                                          "more digits than can be read",
                                 out->line);
                        return;
                    }
                    out->kind = ST_TOK_FLOAT;
                    if (negative)
                        out->real = -out->real;
                    return;
                }
                lx->pos = save;
            }
            if (radix_float) {
                /*
                 *  `whole' was accumulated in an int64 and stopped when it
                 *  would have wrapped, so a radix Float with more integer
                 *  digits than that would be built from a truncated whole
                 *  part -- silently.  Refused instead; nobody writes one.
                 */
                if (too_big) {
                    lex_fail(lx, out, "line %u: this radix Float has more "
                                      "digits than can be read", out->line);
                    return;
                }
                out->kind = ST_TOK_FLOAT;
                out->real = negative ? -(whole + fraction) : whole + fraction;
                return;
            }
        }
        out->kind    = ST_TOK_INTEGER;
        out->integer = negative ? -value : value;
        note_big_integer(out, lx, digits_start, (unsigned) radix, too_big);
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
    /*
     *  Only `e' marks an exponent.  This used to take `d' as well, on the
     *  strength of a spelling some later Smalltalks allow -- but strtod
     *  below does not know it, so `1d2' scanned as one token and was then
     *  read by strtod as far as the d: 1.0, silently, with `1d2 = 100.0'
     *  false.  The Blue Book has only `e'; `1d2' is `1 d2' there, a unary
     *  send that fails where it is written, and that is what it is here
     *  now.  Nothing in sources/ or lib/ writes the d form.  Bugs3 B25.
     */
    if (!at_end(lx) && lx->source[lx->pos] == 'e') {
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
        note_big_integer(out, lx, digits_start, (unsigned) radix, too_big);
        return;
    }
    {
        lex_text_buffer buf;
        size_t          n = lx->pos - start;
        size_t          i;

        /*
         *  The whole of it, not the first 127 characters of it.  A fixed
         *  buffer here read "1" out of "1000...0e5" once the digits ran
         *  past its end, and answered a Float that was off by whatever the
         *  discarded tail was worth -- with nothing to see.
         */
        buffer_open(&buf);
        for (i = 0; i < n; ++i)
            buffer_push(&buf, lx->source[start + i]);
        if (buf.failed) {
            if (buf.text != buf.stack)
                free(buf.text);
            lex_fail(lx, out, "line %u: out of memory reading a number",
                     out->line);
            return;
        }
        buf.text[buf.length] = '\0';
        real = strtod(buf.text, NULL);
        /*
         *  strtod answers an infinity for a decimal too big to hold, which
         *  is what IEEE 754 says an overflow produces and the wrong thing
         *  for a LITERAL: "1e1000" compiled to a method that returned
         *  infinity, with nothing to say the number in the source was not
         *  the number in the method.  It is refused here, and the image's
         *  own Number class>>readFrom: refuses it too, so both compilers
         *  say the same thing about the same text.
         *
         *  Underflow is left alone.  A decimal too SMALL for a double
         *  rounding to zero is ordinary IEEE behaviour and what every
         *  reader in every language does.
         */
        if (real > DBL_MAX || real < -DBL_MAX) {
            lex_fail(lx, out, "this number is too large for a Float: %.120s",
                     buf.text);
            if (buf.text != buf.stack)
                free(buf.text);
            return;
        }
        if (buf.text != buf.stack)
            free(buf.text);
    }
    out->kind = ST_TOK_FLOAT;
    out->real = negative ? -real : real;
}

/*
 *  Whether a character can continue an identifier.
 *
 *  The underscore is the whole question.  In 1983 it IS the assignment
 *  arrow -- "a _ b" is what every line of sources/ says -- so it cannot
 *  also be a letter there.  Post-1983 Smalltalk spells assignment ":="
 *  exclusively and spends the underscore on names, which is why Pharo has
 *  DelayBasicScheduler>>simulate_vmMilliseconds: and this compiler read it
 *  as "simulate := vmMilliseconds:".
 */
static int
is_word_char(const st_lexer *lx, int c)
{
    if (isalnum(c))
        return 1;
    if (lx->dialect != ST_DIALECT_CLOSURES)
        return 0;
    /*
     *  A byte above ASCII is a letter.  Source files are UTF-8 and Pharo
     *  writes #яблоко; a high byte cannot appear anywhere else outside a
     *  string, a comment or a character literal, all of which are scanned
     *  on their own, so taking the whole run is unambiguous -- and it
     *  keeps the bytes together, which is what a Symbol needs.
     */
    return c == '_' || c >= 0x80;
}

/*
 *  And whether one can BEGIN an identifier, which is a different question:
 *  a digit continues a name and does not start one.  Conflating the two
 *  sends "62" down the identifier branch, which sits above the number
 *  branch -- so every numeric literal in the system becomes a name.
 */
static int
is_word_start(const st_lexer *lx, int c)
{
    if (isalpha(c))
        return 1;
    if (lx->dialect != ST_DIALECT_CLOSURES)
        return 0;
    return c == '_' || c >= 0x80;
}

/*
 *  Whether the underscore under the cursor is the ASSIGNMENT ARROW.
 *
 *  In the Blue Book it always is, and there is no question.  The closure
 *  dialect made it a letter, because Pharo identifiers contain it -- and
 *  that was fine while the closure dialect only ever read lib/, which
 *  writes assignment as `:='.
 *
 *  It stopped being fine when the image's own compiler was routed through
 *  this one.  The Browser's commonest use is editing a method in sources/,
 *  and every one of those 4,500 methods is written `s _ WriteStream on:
 *  String new'.  Read as a letter, the lone `_' became an identifier,
 *  which was undeclared, and `shCascade' compiled and answered nil.
 *
 *  So: an underscore begins an identifier only when something can FOLLOW it
 *  in one.  A lone `_' -- one with a space, a bracket or anything else that
 *  is not a word character after it -- is the arrow, in both dialects.
 *  `_foo' and `a_b' are identifiers, which is what Pharo source needs;
 *  `a _ b' is an assignment, which is what 1983 source needs; and no text
 *  means both.
 */
static int
underscore_is_assignment(const st_lexer *lx)
{
    char    next;

    if (lx->dialect != ST_DIALECT_CLOSURES)
        return 1;
    if (lx->pos + 1 >= lx->length)
        return 1;
    next = lx->source[lx->pos + 1];
    return !is_word_char(lx, (unsigned char) next);
}

/*  Append a run of word characters to `into'.  Answers how many there were. */
static size_t
scan_word(st_lexer *lx, lex_text_buffer *into)
{
    size_t  n = 0;

    while (!at_end(lx)) {
        char    c = lx->source[lx->pos];

        if (!is_word_char(lx, (unsigned char) c))
            break;
        buffer_push(into, c);
        ++n;
        ++lx->pos;
    }
    return n;
}

static int
lex_token(st_lexer *lx, st_token *out)
{
    char    c;

    {
        size_t      before = lx->pos;
        unsigned    unclosed = 0;

        memset(out, 0, sizeof *out);
        if (!skip_blanks(lx, &unclosed)) {
            out->line = unclosed;
            lex_fail(lx, out, "line %u: unterminated comment", unclosed);
            return 1;
        }
        out->after_space = (lx->pos != before) || before == 0;
    }
    out->line   = lx->line;
    out->offset = lx->pos;
    if (at_end(lx)) {
        out->kind = ST_TOK_END;
        return 0;
    }
    c = lx->source[lx->pos];

    /*
     *  A NUL byte outside a string literal, a comment or a $ literal means
     *  nothing, and it is named here because the generic message at the
     *  bottom would print it -- and a NUL printed into a C string ends the
     *  string, leaving "unexpected character '" with nothing after it.
     *  Inside a string literal it is data and is kept.  Bugs3 B28.
     */
    if (c == '\0') {
        lex_fail(lx, out, "line %u: a NUL byte outside a string literal",
                 out->line);
        ++lx->pos;
        return 1;
    }

    /*
     *  Identifiers and keywords.
     *
     *  A leading underscore starts one only in the closure dialect; in the
     *  Blue Book it is the assignment arrow, handled in the switch below.
     */
    if (is_word_start(lx, (unsigned char) c)
     && !(c == '_' && underscore_is_assignment(lx))) {
        lex_text_buffer buf;

        buffer_open(&buf);
        scan_word(lx, &buf);
        /*
         *  A trailing colon makes it a keyword, unless the colon is part of
         *  an assignment -- "x := 1" must not lex "x:" as a keyword.
         */
        if (!at_end(lx) && lx->source[lx->pos] == ':' && peek_char(lx, 1) != '=') {
            buffer_push(&buf, ':');
            ++lx->pos;
            out->kind = ST_TOK_KEYWORD;
        } else {
            out->kind = ST_TOK_IDENTIFIER;
        }
        buffer_close(lx, &buf, out);
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
        out->kind = ST_TOK_CHARACTER;
        /*
         *  A UTF-8 sequence is one character, not two or three.
         *
         *  Source files are UTF-8 and Pharo's tests are full of $\u00b6 and
         *  the like; reading the lead byte alone leaves the continuation
         *  bytes in the stream, where they are neither a token nor a
         *  legal anything.  This memory's Character is the Blue Book's --
         *  a unique entry in CharacterTable, 0 to 255 -- so a code point
         *  that fits in Latin-1 is taken and anything above it is refused
         *  by number rather than mis-read.
         */
        {
            unsigned char   b0 = (unsigned char) lx->source[lx->pos];
            unsigned long   code = b0;
            unsigned        extra = 0;
            unsigned        k;
            size_t          after_lead;

            if (b0 >= 0xF0)      { extra = 3; code = b0 & 0x07u; }
            else if (b0 >= 0xE0) { extra = 2; code = b0 & 0x0Fu; }
            else if (b0 >= 0xC0) { extra = 1; code = b0 & 0x1Fu; }
            ++lx->pos;
            after_lead = lx->pos;
            for (k = 0; k < extra && !at_end(lx); ++k) {
                unsigned char   cont = (unsigned char) lx->source[lx->pos];

                if ((cont & 0xC0u) != 0x80u)
                    break;
                code = (code << 6) | (cont & 0x3Fu);
                ++lx->pos;
            }
            /*
             *  A lead byte with fewer continuation bytes behind it than
             *  it promised is not UTF-8 at all: it is a single Latin-1
             *  character, which is what a file written in that encoding
             *  holds and what `(Character value: 233) storeString'
             *  produces -- `$' followed by the one byte 0xE9.  The loop
             *  used to break out and KEEP the partial decode, the low
             *  bits of the lead byte alone, so `$é' in a Latin-1 file was
             *  $<tab> (233 & 0x1F = 9) and the storeString of every
             *  Character from 192 up read back as a different Character.
             *  Bugs3 B27.  The byte is taken as itself, from just past
             *  the lead byte, so nothing after it is lost either.
             */
            if (k < extra) {
                lx->pos = after_lead;
                code    = b0;
            }
            if (code > 255) {
                lex_fail(lx, out, "line %u: character U+%04lX is beyond this "
                                  "memory's 256-character table",
                         out->line, code);
                return 1;
            }
            out->integer = (long) code;
            out->text[0] = (char) (unsigned char) code;
            out->text[1] = '\0';
        }
        return 1;

    case '\'': {
        lex_text_buffer buf;

        buffer_open(&buf);
        ++lx->pos;
        for (;;) {
            if (at_end(lx)) {
                if (buf.text != buf.stack)
                    free(buf.text);
                lex_fail(lx, out, "line %u: unterminated string", out->line);
                return 1;
            }
            if (lx->source[lx->pos] == '\'') {
                if (peek_char(lx, 1) == '\'') {
                    buffer_push(&buf, '\'');
                    lx->pos += 2;
                    continue;
                }
                ++lx->pos;
                break;
            }
            if (lx->source[lx->pos] == '\n')
                ++lx->line;
            buffer_push(&buf, lx->source[lx->pos]);
            ++lx->pos;
        }
        out->kind = ST_TOK_STRING;
        buffer_close(lx, &buf, out);
        return 1;
    }

    case '#':
        ++lx->pos;
        if (!at_end(lx) && lx->source[lx->pos] == '(') {
            ++lx->pos;
            out->kind = ST_TOK_ARRAY_OPEN;
            return 1;
        }
        if (!at_end(lx) && lx->source[lx->pos] == '[') {
            /*
             *  A literal ByteArray.  This has to be tested before the
             *  binary-character branch below or it would never be reached:
             *  '[' is not a binary character, so #[ used to fall all the way
             *  through to "# must be followed by a symbol".
             */
            ++lx->pos;
            out->kind = ST_TOK_BYTE_ARRAY_OPEN;
            return 1;
        }
        if (!at_end(lx) && lx->source[lx->pos] == '\'') {
            /*  #'with spaces' is a symbol spelled as a string.  */
            st_token    inner;

            lex_token(lx, &inner);
            /*
             *  A Symbol is interned by its C string, and a NUL inside one
             *  would intern the prefix -- a different Symbol, quietly.  A
             *  String literal carries its NULs (see string_literal in the
             *  compiler); a Symbol may not.  Bugs3 B28.
             */
            if (inner.kind == ST_TOK_STRING
             && memchr(LEX_text(&inner), '\0', inner.text_length)) {
                lex_fail(lx, out, "line %u: a Symbol may not contain a NUL "
                                  "byte", out->line);
                return 1;
            }
            memcpy(out->text, inner.text, sizeof out->text);
            /*
             *  Including the overflow, which the inner token may carry:
             *  #'...' is spelled as a string and so has a string's length,
             *  and the storage it points at belongs to this same lexer.
             */
            out->long_text   = inner.long_text;
            out->text_length = inner.text_length;
            out->kind = inner.kind == ST_TOK_ERROR
                            ? ST_TOK_ERROR : ST_TOK_SYMBOL;
            return 1;
        }
        if (!at_end(lx) && is_binary_char((unsigned char) lx->source[lx->pos])) {
            lex_text_buffer buf;

            buffer_open(&buf);
            while (!at_end(lx)
                && is_binary_char((unsigned char) lx->source[lx->pos])) {
                buffer_push(&buf, lx->source[lx->pos]);
                ++lx->pos;
            }
            out->kind = ST_TOK_SYMBOL;
            buffer_close(lx, &buf, out);
            return 1;
        }
        {
            /*  A symbol may be several keywords run together.  */
            lex_text_buffer buf;
            size_t          n = 0;

            buffer_open(&buf);
            for (;;) {
                if (scan_word(lx, &buf) == 0)
                    break;
                n = buf.length;
                if (!at_end(lx) && lx->source[lx->pos] == ':') {
                    buffer_push(&buf, ':');
                    n = buf.length;
                    ++lx->pos;
                    continue;
                }
                break;
            }
            if (n == 0) {
                if (buf.text != buf.stack)
                    free(buf.text);
                lex_fail(lx, out, "line %u: # must be followed by a symbol",
                         out->line);
                return 1;
            }
            out->kind = ST_TOK_SYMBOL;
            buffer_close(lx, &buf, out);
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

    case '{':
        ++lx->pos;
        out->kind = ST_TOK_LBRACE;
        return 1;

    case '}':
        ++lx->pos;
        out->kind = ST_TOK_RBRACE;
        return 1;

    case '_':
        /*
         *  The 1983 assignment arrow.  The closure dialect reaches here only
         *  for a LONE underscore -- see underscore_is_assignment -- because
         *  one with a word character after it was taken by the identifier
         *  scanner above.
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

    /*
     *  '<' used to be special-cased here, scanning the whole of
     *  "<primitive: 60>" into one token by reading raw characters.  That
     *  recognised the one pragma the Blue Book has and could not be extended
     *  to the general form -- <foo: 1>, several per method, or a named
     *  primitive -- without the lexer learning the grammar.
     *
     *  So it is gone, and '<' is an ordinary binary selector again.  Pragmas
     *  are recognised by the PARSER, which knows the one position they can
     *  appear in and can speculatively read and rewind like everything else
     *  in this compiler.  Removing the case is what makes that possible; it
     *  is a special case deleted, not one added.
     */
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
        lex_text_buffer buf;
        size_t          n = 0;

        buffer_open(&buf);
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
          /*
           *  The two openers below are new, and forgetting either of them
           *  would be silent: "{-1. 2}" would lex the minus as a binary
           *  selector with no left operand and fail somewhere later, in a
           *  message that named neither the brace nor the minus.
           */
          || lx->last_kind == ST_TOK_LBRACE
          || lx->last_kind == ST_TOK_BYTE_ARRAY_OPEN
          || lx->last_kind == ST_TOK_SEMICOLON
          || lx->last_kind == ST_TOK_PERIOD
          || lx->last_kind == ST_TOK_BAR
          || lx->last_kind == ST_TOK_COLON)) {
            if (buf.text != buf.stack)
                free(buf.text);
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
            buffer_push(&buf, lx->source[lx->pos]);
            ++n;
            ++lx->pos;
            /*
             *  Two characters at most in Smalltalk-80.  Post-1983 source
             *  writes longer ones -- Boolean>>==> is in Pharo's Kernel --
             *  and the run can be taken whole there, because the rule just
             *  above already stops a minus that begins a number from being
             *  swallowed, which was the only thing the two-character limit
             *  was protecting.
             */
            if (n == 2 && lx->dialect != ST_DIALECT_CLOSURES)
                break;
        }
        out->kind = ST_TOK_BINARY;
        buffer_close(lx, &buf, out);
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

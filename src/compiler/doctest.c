/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Finding the examples in Pharo's method comments.  See doctest.h for why
 *  they are worth having.
 */

#include "doctest.h"
#include "source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *
duplicate(const char *text, size_t n)
{
    char   *copy = (char *) malloc(n + 1);

    if (!copy)
        return NULL;
    memcpy(copy, text, n);
    copy[n] = '\0';
    return copy;
}

/*  Trim in place, both ends.  */
static void
trim(char *text)
{
    size_t  n = strlen(text);
    size_t  first = 0;

    while (n > 0 && isspace((unsigned char) text[n - 1]))
        text[--n] = '\0';
    while (text[first] && isspace((unsigned char) text[first]))
        ++first;
    if (first)
        memmove(text, text + first, n - first + 1);
}

static int
add(st_doctest_list *l, const char *expression, const char *expected,
    const char *where, const char *file, unsigned line)
{
    st_doctest *d;

    if (l->count == l->capacity) {
        unsigned    want  = l->capacity ? l->capacity * 2 : 64;
        void       *grown = realloc(l->items, want * sizeof *l->items);

        if (!grown)
            return 0;
        l->items    = grown;
        l->capacity = want;
    }
    d = &l->items[l->count];
    memset(d, 0, sizeof *d);
    d->expression = duplicate(expression, strlen(expression));
    d->expected   = duplicate(expected, strlen(expected));
    d->where      = duplicate(where, strlen(where));
    d->file       = duplicate(file, strlen(file));
    d->line       = line;
    if (!d->expression || !d->expected || !d->where || !d->file) {
        free(d->expression);
        free(d->expected);
        free(d->where);
        free(d->file);
        return 0;
    }
    ++l->count;
    return 1;
}

/*
 *  One comment's worth of text, with its doubled quotes undoubled.
 *
 *  A doctest lives inside a Smalltalk comment, so any quote it contains was
 *  written twice.  "'a' , 'b' >>> 'ab'" arrives here as written; a comment
 *  containing a double-quote arrives doubled and has to come back, or the
 *  expression handed to the compiler is not the one the author wrote.
 */
static void
undouble(char *text)
{
    char   *from = text;
    char   *to   = text;

    while (*from) {
        if (from[0] == '"' && from[1] == '"') {
            *to++ = '"';
            from += 2;
        }  else  {
            *to++ = *from++;
        }
    }
    *to = '\0';
}

/*
 *  Does a line end at P?
 *
 *  The source readers hand this file the method's text with its line ends
 *  already turned into the image's own carriage returns -- a Smalltalk
 *  String ends its lines with CR -- so counting only line feeds counted
 *  nothing, and every failing example reported the line its method began
 *  on (Bugs5 COMP-10: a failure on line 12 said `Foo.class.st:8').  Count
 *  a CR, a LF, and a CR LF pair once, so the count is right whichever form
 *  a reader leaves behind.
 */
static int
line_end_at(const char *p)
{
    return *p == '\n' || (*p == '\r' && p[1] != '\n');
}

/*
 *  Step over a string or character literal starting at P, or over one
 *  character of anything else.  The three scanners below share it so that
 *  none of them reads a quote, a period or a separator that is inside a
 *  literal as the real thing.
 */
static char *
skip_literal(char *p)
{
    if (*p == '$' && p[1])
        return p + 2;
    if (*p != '\'')
        return p + 1;
    ++p;
    while (*p) {
        if (*p == '\'' && p[1] == '\'')
            p += 2;
        else if (*p == '\'')
            return p + 1;
        else
            ++p;
    }
    return p;
}

/*
 *  The first separator at or after P that is outside a string or a
 *  character literal, or NULL.  `('a>>>b' size) >>> 5' was split inside
 *  the string (Bugs6 COMP-3).
 */
static char *
find_separator(char *p)
{
    while (*p) {
        if (*p == '\'' || *p == '$') {
            p = skip_literal(p);
            continue;
        }
        if (p[0] == '>' && p[1] == '>' && p[2] == '>')
            return p;
        ++p;
    }
    return NULL;
}

/*
 *  The first period in [P, END) that ends a statement: at the top level,
 *  outside every literal and every kind of bracket, and not the point of a
 *  number, which a digit follows at once.  An expected value is a literal
 *  and holds no statement end of its own, so the first one is where the
 *  example stops.
 */
static char *
statement_end(char *p, const char *end)
{
    int     depth = 0;

    while (p < end) {
        if (*p == '\'' || *p == '$') {
            p = skip_literal(p);
            continue;
        }
        if (*p == '(' || *p == '[' || *p == '{')
            ++depth;
        else if (*p == ')' || *p == ']' || *p == '}') {
            if (depth > 0)
                --depth;
        }  else if (*p == '.' && depth == 0
                && !isdigit((unsigned char) p[1]))
            return p;
        ++p;
    }
    return NULL;
}

/*
 *  Take the doctests out of one comment.
 *
 *  The separator is ">>>", and the FIRST one is the separator: an
 *  expression may contain ">>" -- "Object>>#foo" is how Pharo names a
 *  compiled method -- but the expected value is a literal and does not.
 *  Taking the last would split "(Object>>#foo) numArgs >>> 0" in the wrong
 *  place.
 *
 *  A comment may hold several examples, and an example may end in a period
 *  (Bugs6 COMP-3).  "3 + 4 >>> 7. 2 + 2 >>> 4" used to be one doctest
 *  whose expected value was "7. 2 + 2 >>> 4", and "3 + 4 >>> 7." one whose
 *  expected value was "7." -- neither compiled, and both were counted
 *  among the examples that need something this image has not got, which
 *  is the one count that is not a bug.  Now the expected value ends at
 *  the first statement end, and the text after it is the next example.
 */
static void
scan_comment(st_doctest_list *l, char *text, const char *where,
             const char *file, unsigned line)
{
    char       *p = text;
    char       *sep = find_separator(p);
    const char *scan;
    unsigned    at_line = line;

    while (sep) {
        char   *expression = p;
        char   *expected   = sep + 3;
        char   *next       = find_separator(expected);
        char   *end        = next ? next : expected + strlen(expected);
        char   *stop       = statement_end(expected, end);
        unsigned    example_line;

        /*  The line the example begins on, for the report.  */
        for (scan = expression; scan < sep; ++scan)
            if (line_end_at(scan))
                ++at_line;
        example_line = at_line;
        *sep = '\0';
        if (stop) {
            *stop = '\0';
            p = stop + 1;
        }  else if (next) {
            /*
             *  Two separators and no statement end between them: the
             *  expected value runs to the second separator, and what
             *  follows that is the next example's expected value with no
             *  expression -- which is dropped below, as a comment with
             *  nothing on one side always was.  The first example then
             *  fails to compile and says so, which is the loud outcome a
             *  malformed comment should get.
             */
            *next = '\0';
            p = next + 3;
        }  else  {
            p = NULL;
        }
        trim(expression);
        trim(expected);
        /*
         *  Both halves have to be there.  "<Collection of<Plugin>>>" in a
         *  class comment is a type annotation that happens to end in three
         *  angle brackets, and it leaves nothing on the right.
         */
        if (expression[0] && expected[0])
            add(l, expression, expected, where, file, example_line);
        sep = p ? find_separator(p) : NULL;
    }
}

static int
doctest_method(const char *class_name, int class_side, const char *category,
               const char *source, const char *file, unsigned line,
               void *user)
{
    st_doctest_list    *l = (st_doctest_list *) user;
    char                where[192];
    const char         *p = source;
    /*
     *  The Tonel reader puts a carriage return of its own between the
     *  pattern and the body, which the count below sees as a line the
     *  file has not got; counted from one line earlier, every example
     *  lands on its own line (Bugs6 COMP-9).  The chunk reader's source
     *  begins with the chunk's own line end and is counted as it comes.
     */
    unsigned            at_line = line
        - (line > 0 && strcmp(SRC_format_of(file), "tonel") == 0 ? 1 : 0);

    (void) category;

    ++l->methods;
    snprintf(where, sizeof where, "%s%s", class_name ? class_name : "?",
             class_side ? " class" : "");

    /*
     *  Walk the method's text looking for comments, and skip the three
     *  things that can contain a double quote without opening one: a
     *  string, a character literal, and -- because a string may contain a
     *  quote doubled -- the inside of a string.
     */
    while (*p) {
        if (line_end_at(p)) {
            ++at_line;
            ++p;
        }  else if (*p == '$' && p[1]) {
            p += 2;                     /*  $" is a character, not a comment */
        }  else if (*p == '\'') {
            ++p;
            while (*p) {
                if (line_end_at(p))
                    ++at_line;
                if (*p == '\'' && p[1] == '\'')
                    p += 2;
                else if (*p == '\'')
                    break;
                else
                    ++p;
            }
            if (*p)
                ++p;
        }  else if (*p == '"') {
            const char *start = ++p;
            unsigned    start_line = at_line;
            char       *body;

            while (*p) {
                if (line_end_at(p))
                    ++at_line;
                if (*p == '"' && p[1] == '"')
                    p += 2;
                else if (*p == '"')
                    break;
                else
                    ++p;
            }
            body = duplicate(start, (size_t) (p - start));
            if (body) {
                undouble(body);
                scan_comment(l, body, where, file, start_line);
                free(body);
            }
            if (*p)
                ++p;
        }  else  {
            ++p;
        }
    }
    return 1;
}

static const st_source_sink doctest_sink = {
    NULL, NULL, NULL, doctest_method, NULL
};

int
DOCTEST_scan(const char *path, st_doctest_list *out, char *error,
             size_t error_len)
{
    char    err[512] = "";

    if (!SRC_read(path, &doctest_sink, out, err, sizeof err)) {
        if (error && error_len)
            snprintf(error, error_len, "%s", err[0] ? err : "cannot read");
        return 0;
    }
    ++out->files;
    return 1;
}

void
DOCTEST_free(st_doctest_list *l)
{
    unsigned    i;

    for (i = 0; i < l->count; ++i) {
        free(l->items[i].expression);
        free(l->items[i].expected);
        free(l->items[i].where);
        free(l->items[i].file);
    }
    free(l->items);
    memset(l, 0, sizeof *l);
}

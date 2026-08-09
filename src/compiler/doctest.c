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
 *  Take the doctests out of one comment.
 *
 *  The separator is ">>>", and the FIRST one is the separator: an
 *  expression may contain ">>" -- "Object>>#foo" is how Pharo names a
 *  compiled method -- but the expected value is a literal and does not.
 *  Taking the last would split "(Object>>#foo) numArgs >>> 0" in the wrong
 *  place.
 */
static void
scan_comment(st_doctest_list *l, char *text, const char *where,
             const char *file, unsigned line)
{
    char   *sep = strstr(text, ">>>");
    char   *expression;
    char   *expected;

    if (!sep)
        return;
    *sep = '\0';
    expression = text;
    expected   = sep + 3;
    trim(expression);
    trim(expected);
    /*
     *  Both halves have to be there.  "<Collection of<Plugin>>>" in a class
     *  comment is a type annotation that happens to end in three angle
     *  brackets, and it leaves nothing on the right.
     */
    if (!expression[0] || !expected[0])
        return;
    add(l, expression, expected, where, file, line);
}

static int
doctest_method(const char *class_name, int class_side, const char *category,
               const char *source, const char *file, unsigned line,
               void *user)
{
    st_doctest_list    *l = (st_doctest_list *) user;
    char                where[192];
    const char         *p = source;
    unsigned            at_line = line;

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
        if (*p == '\n') {
            ++at_line;
            ++p;
        }  else if (*p == '$' && p[1]) {
            p += 2;                     /*  $" is a character, not a comment */
        }  else if (*p == '\'') {
            ++p;
            while (*p) {
                if (*p == '\n')
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
                if (*p == '\n')
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

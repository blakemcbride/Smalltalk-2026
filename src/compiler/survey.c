/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The source survey.  See survey.h.
 */

#include "survey.h"
#include "compiler.h"
#include "source.h"

#include <stdlib.h>
#include <string.h>

/*
 *  Stand-in literals.  Nothing here is ever executed, so the values only have
 *  to be distinct and non-zero; making them even keeps them out of the
 *  SmallInteger tag space in case anything looks.
 */
static st_oop syn_symbol(const char *t, void *u) { (void) t; (void) u; return 1000; }
static st_oop syn_string(const char *t, void *u) { (void) t; (void) u; return 2000; }
static st_oop syn_float(double v, void *u)       { (void) v; (void) u; return 2002; }
static st_oop syn_large(int64_t v, void *u)      { (void) v; (void) u; return 2004; }
static st_oop syn_array(st_oop *e, unsigned n, void *u)
{ (void) e; (void) n; (void) u; return 2006; }
static st_oop syn_byte_array(const uint8_t *b, unsigned n, void *u)
{ (void) b; (void) n; (void) u; return 2008; }
static st_oop syn_method_state(st_oop pragmas, void *u)
{ (void) pragmas; (void) u; return 2010; }
static st_oop syn_character(unsigned c, void *u)
{ (void) u; return (st_oop) (4000 + c * 2); }

/*
 *  Every identifier resolves.  Unknown globals are a scope question and this
 *  is a grammar question; letting them all through keeps the two apart.
 */
static st_oop syn_global(const char *n, void *u) { (void) n; (void) u; return 3000; }

void
SURVEY_init(st_survey *s)
{
    memset(s, 0, sizeof *s);
}

/*
 *  Failures are grouped by message with the quoted part folded away, so a
 *  hundred methods tripping over one construct read as a line rather than a
 *  hundred lines.
 */
static void
record(st_survey *s, const char *message, const char *selector,
       const char *class_name)
{
    char        key[256];
    char       *quote;
    unsigned    i;

    snprintf(key, sizeof key, "%s", message);
    quote = strchr(key, '\'');
    if (quote)
        snprintf(quote, sizeof key - (size_t) (quote - key), "'...'");

    for (i = 0; i < s->kind_count; ++i) {
        if (strcmp(s->kinds[i].text, key) == 0) {
            ++s->kinds[i].count;
            return;
        }
    }
    if (s->kind_count >= ST_SURVEY_MAX_KINDS)
        return;
    snprintf(s->kinds[i].text, sizeof s->kinds[i].text, "%s", key);
    snprintf(s->kinds[i].example, sizeof s->kinds[i].example, "%s>>%s",
             class_name, selector);
    s->kinds[i].count = 1;
    ++s->kind_count;
}

/*  The first line of a method chunk names it well enough to report.  */
static void
selector_of(const char *source, char *out, size_t outlen)
{
    size_t  n = 0;

    while (*source == ' ' || *source == '\t' || *source == '\n')
        ++source;
    while (*source && *source != '\n' && n + 1 < outlen)
        out[n++] = *source++;
    out[n] = '\0';
}

/*
 *  One method, compiled and thrown away.
 *
 *  The survey used to drive the chunk reader itself, which meant it read
 *  exactly one of the two formats -- and read the other as a file with no
 *  methods in it, reporting "0 methods, 0 failed" for a Tonel package.  A
 *  checker that answers "nothing wrong" for a file it did not understand is
 *  worse than one that refuses, so it goes through SRC_read like everything
 *  else and gets both.
 */
static int
survey_method(const char *class_name, int class_side, const char *category,
              const char *source, const char *file, unsigned line, void *user)
{
    st_survey          *s = (st_survey *) user;
    st_compile_context  ctx;
    st_compiled_code    code;

    (void) class_side;
    (void) category;
    (void) file;
    (void) line;

    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = syn_symbol;
    ctx.make_string        = syn_string;
    ctx.make_float         = syn_float;
    ctx.make_large_integer = syn_large;
    ctx.make_array         = syn_array;
    ctx.make_byte_array    = syn_byte_array;
    ctx.make_method_state  = syn_method_state;
    ctx.make_character     = syn_character;
    ctx.lookup_global      = syn_global;
    ctx.method_class_association = 5000;

    ++s->methods;
    if (COMPILE_to_bytecodes(source, &ctx, &code) != 0) {
        char    selector[160];

        ++s->failed;
        selector_of(source, selector, sizeof selector);
        record(s, code.error, selector, class_name);
    }
    return 1;
}

static void
survey_diagnostic(const char *file, unsigned line, const char *message,
                  void *user)
{
    st_survey  *s = (st_survey *) user;

    (void) file;
    (void) line;
    record(s, message, "", "");
    ++s->failed;
}

static const st_source_sink survey_sink = {
    NULL, NULL, NULL, survey_method, survey_diagnostic
};

void
SURVEY_file(st_survey *s, const char *path)
{
    char    err[512];

    if (!SRC_read(path, &survey_sink, s, err, sizeof err)) {
        ++s->unreadable;
        return;
    }
    ++s->files;
}

static int
by_count(const void *a, const void *b)
{
    const st_survey_failure    *x = (const st_survey_failure *) a;
    const st_survey_failure    *y = (const st_survey_failure *) b;

    if (x->count != y->count)
        return (x->count < y->count) ? 1 : -1;
    return strcmp(x->text, y->text);
}

void
SURVEY_report(st_survey *s, FILE *out)
{
    unsigned    i;

    qsort(s->kinds, s->kind_count, sizeof s->kinds[0], by_count);

    fprintf(out, "%u files, %u methods, %u compiled, %u failed (%.2f%%)\n",
            s->files, s->methods, s->methods - s->failed, s->failed,
            s->methods ? 100.0 * (double) s->failed / (double) s->methods
                       : 0.0);
    if (s->unreadable)
        fprintf(out, "%u file(s) could not be read\n", s->unreadable);
    if (s->kind_count)
        fprintf(out, "\n%-8s  %-52s  %s\n", "count", "error", "first example");
    for (i = 0; i < s->kind_count; ++i)
        fprintf(out, "%-8u  %-52s  %s\n", s->kinds[i].count, s->kinds[i].text,
                s->kinds[i].example);
}

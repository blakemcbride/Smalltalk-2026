/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The source survey.  See survey.h.
 */

#include "survey.h"
#include "prim.h"
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
static st_oop syn_large_digits(const char *d, unsigned r, int n, void *u)
{ (void) d; (void) r; (void) n; (void) u; return 2004; }
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
 *
 *  A MATCHED pair of quotes, and the tail is kept.  Folding from the first
 *  quote to the end of the message instead turned "a shared name's vector
 *  is not in scope" into "a shared name'...'" -- and, worse, made two
 *  different errors that happen to open with a quote read as one line
 *  saying nothing at all.  A report that hides the message it is reporting
 *  is not a small problem: it cost an hour here.
 */
static void
record(st_survey *s, const char *message, const char *selector,
       const char *class_name)
{
    char        key[256];
    char       *open_quote;
    char       *close_quote;
    unsigned    i;

    snprintf(key, sizeof key, "%s", message);
    open_quote = strchr(key, '\'');
    close_quote = open_quote ? strchr(open_quote + 1, '\'') : NULL;
    if (close_quote) {
        char    tail[256];

        snprintf(tail, sizeof tail, "%s", close_quote + 1);
        snprintf(open_quote, sizeof key - (size_t) (open_quote - key),
                 "'...'%s", tail);
    }

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
 *  Remember that a method asked for a primitive.
 *
 *  Keyed on the number AND, for a named primitive, on the module and
 *  function -- primitive 117 is a doorway, not a primitive, and collapsing
 *  every callout through it into one row would hide the whole question.
 */
static void
record_primitive(st_survey *s, const st_compiled_code *code,
                 const char *class_name, int class_side, const char *source)
{
    char        name[160] = "";
    unsigned    i;

    if (code->primitive == 117 && code->primitive_name[0])
        snprintf(name, sizeof name, "'%.63s' module: '%.63s'",
                 code->primitive_name, code->primitive_module);

    for (i = 0; i < s->primitive_count; ++i) {
        if (s->primitives[i].number == code->primitive
         && strcmp(s->primitives[i].name, name) == 0) {
            ++s->primitives[i].methods;
            return;
        }
    }
    if (s->primitive_count == ST_SURVEY_MAX_PRIMITIVES) {
        ++s->primitives_overflowed;
        return;
    }
    i = s->primitive_count++;
    s->primitives[i].number  = code->primitive;
    s->primitives[i].methods = 1;
    snprintf(s->primitives[i].name, sizeof s->primitives[i].name, "%s", name);
    {
        char    selector[160];

        /*
         *  The real selector, not the first line: a 1983 method puts its
         *  pattern and its comment on one line, so the line is a paragraph.
         */
        if (COMPILE_selector_of(source, selector, sizeof selector) != 0)
            snprintf(selector, sizeof selector, "?");
        snprintf(s->primitives[i].example, sizeof s->primitives[i].example,
                 "%s%s>>%s", class_name ? class_name : "?",
                 class_side ? " class" : "", selector);
    }
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
    (void) line;

    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = syn_symbol;
    ctx.make_string        = syn_string;
    ctx.make_float         = syn_float;
    ctx.make_large_integer = syn_large;
    ctx.make_large_integer_digits = syn_large_digits;
    ctx.make_array         = syn_array;
    ctx.make_byte_array    = syn_byte_array;
    ctx.make_method_state  = syn_method_state;
    ctx.make_character     = syn_character;
    ctx.lookup_global      = syn_global;
    ctx.method_class_association = 5000;
    /*
     *  A package-format file is post-1983 source and is read as such; a
     *  chunk file is 1983 source and is read as that.  The dialect decides
     *  what an underscore means and how long a binary selector may be, and
     *  the file's own format is the best evidence available -- better than
     *  a flag the caller has to remember, and right for every file either
     *  reader has ever been handed.
     */
    ctx.dialect = strcmp(SRC_format_of(file), "tonel") == 0
                    ? ST_DIALECT_CLOSURES : ST_DIALECT_BLUE_BOOK;

    ++s->methods;
    if (COMPILE_to_bytecodes(source, &ctx, &code) != 0) {
        char    selector[160];

        ++s->failed;
        selector_of(source, selector, sizeof selector);
        record(s, code.error, selector, class_name);
        return 1;
    }
    if (code.primitive)
        record_primitive(s, &code, class_name, class_side, source);
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

/*
 *  Every primitive the surveyed source asked for, against what this VM does
 *  with it.
 *
 *  Four outcomes, not two, because two of them would be a lie.  A primitive
 *  that is ACCEPTED succeeds and does nothing, so the method's Smalltalk
 *  fallback -- which is where the real work usually lives -- never runs: it
 *  belongs on the list of things to look at, but not on the list of things
 *  to implement.  A primitive that is a TAG must keep failing, because it
 *  is a label read by a walk up the sender chain and implementing it would
 *  break the exception system.  Only ABSENT is work.
 */
static int
primitive_order(const void *a, const void *b)
{
    const st_survey_primitive *x = a;
    const st_survey_primitive *y = b;

    if (x->number != y->number)
        return x->number < y->number ? -1 : 1;
    return strcmp(x->name, y->name);
}

unsigned
SURVEY_primitive_report(st_survey *s, FILE *out)
{
    static const char *const heading[] = {
        "not implemented here -- this is the work",
        "implemented",
        "accepted, and does nothing",
        "deliberately absent: a mark, not an operation"
    };
    static const st_primitive_status order[] = {
        ST_PRIM_ABSENT, ST_PRIM_ACCEPTED, ST_PRIM_TAG, ST_PRIM_PRESENT
    };
    unsigned    totals[4] = { 0, 0, 0, 0 };
    unsigned    group;
    unsigned    i;

    qsort(s->primitives, s->primitive_count, sizeof s->primitives[0],
          primitive_order);

    fprintf(out, "%u file%s, %u method%s, %u distinct primitive%s\n",
            s->files, s->files == 1 ? "" : "s",
            s->methods, s->methods == 1 ? "" : "s",
            s->primitive_count, s->primitive_count == 1 ? "" : "s");

    for (group = 0; group < 4; ++group) {
        int     printed_heading = 0;

        for (i = 0; i < s->primitive_count; ++i) {
            const char         *name = NULL;
            st_primitive_status status =
                ST_primitive_status_of(s->primitives[i].number, &name);

            if (status != order[group])
                continue;
            ++totals[order[group]];
            if (!printed_heading) {
                fprintf(out, "\n  %s\n", heading[order[group]]);
                printed_heading = 1;
            }
            fprintf(out, "  %4u  %-44.44s %5u  %s\n",
                    s->primitives[i].number,
                    s->primitives[i].name[0] ? s->primitives[i].name
                                             : (name ? name : ""),
                    s->primitives[i].methods,
                    s->primitives[i].example);
        }
    }

    fprintf(out, "\n  %u implemented, %u accepted-and-inert, %u deliberately "
                 "absent, %u to implement\n",
            totals[ST_PRIM_PRESENT], totals[ST_PRIM_ACCEPTED],
            totals[ST_PRIM_TAG], totals[ST_PRIM_ABSENT]);
    if (s->primitives_overflowed)
        fprintf(out, "  %u more did not fit in the table and are NOT "
                     "counted above\n", s->primitives_overflowed);
    if (s->failed)
        fprintf(out, "  %u method%s did not compile and asked for nothing\n",
                s->failed, s->failed == 1 ? "" : "s");
    return totals[ST_PRIM_ABSENT];
}

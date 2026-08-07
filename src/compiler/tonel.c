/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Pharo's Tonel format, read as source events.
 *
 *  A Tonel file is a type definition followed by methods:
 *
 *      "An optional class comment."
 *      Class {
 *          #name : 'OrderedCollection',
 *          #superclass : 'SequenceableCollection',
 *          #instVars : [ 'array', 'firstIndex' ],
 *          #category : 'Collections-Sequenceable'
 *      }
 *
 *      { #category : 'accessing' }
 *      OrderedCollection >> first [
 *          ^array at: firstIndex
 *      ]
 *
 *      OrderedCollection class >> new [ ^self new: 10 ]
 *
 *  The type is Class, Trait, Extension or Package.  An Extension defines no
 *  class and only adds methods to one defined elsewhere, which costs this
 *  reader nothing: it emits no class_def and several method events, which is
 *  precisely what a second chunk file of "!Foo methodsFor: 'x'!" already
 *  produced.
 *
 *  ----------  Finding the end of a method  ----------
 *
 *  The one genuinely fiddly part.  A method body ends at the ']' that
 *  balances its opening '[', and four constructs can hold a bracket that
 *  must not be counted:
 *
 *      "a comment"     skipped;  "" is a literal quote inside one
 *      'a string'      skipped;  '' is a literal quote inside one
 *      $[              a character literal: '$' takes the next character
 *                      whatever it is, which is what makes $[ $] $' and $"
 *                      safe
 *      #( ... )        a literal array, skipped with its own paren counter,
 *                      because it may contain any of the above
 *
 *  #[1 2 3] needs no special case: it contributes one '[' and one ']' and
 *  balances on its own.  Getting this set wrong does not produce a parse
 *  error -- it produces a method that silently swallows the next one, which
 *  is why it is written out rather than assumed.
 */

#include "tonel.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t      length;
    size_t      pos;
    unsigned    line;
    const char *path;

    const st_source_sink   *sink;
    void                   *user;
    char                   *error;
    size_t                  error_len;
} tonel;

static void
fail(tonel *t, const char *fmt, ...)
{
    va_list ap;
    char    detail[256];

    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    if (t->error && t->error_len && !t->error[0])
        snprintf(t->error, t->error_len, "%s:%u: %s", t->path, t->line,
                 detail);
}

static void
note(tonel *t, const char *fmt, ...)
{
    va_list ap;
    char    detail[256];

    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    if (t->sink->diagnostic)
        t->sink->diagnostic(t->path, t->line, detail, t->user);
}

static int  at_end(const tonel *t)  { return t->pos >= t->length; }
static char here(const tonel *t)    { return t->pos < t->length
                                             ? t->text[t->pos] : '\0'; }

static void
advance(tonel *t)
{
    if (t->pos < t->length && t->text[t->pos] == '\n')
        ++t->line;
    ++t->pos;
}

/*  ----------  Skipping  ----------  */

/*
 *  Whitespace and comments.  The last comment seen is kept, because a class
 *  comment in Tonel is simply the comment that precedes the definition.
 */
static void
skip_separators(tonel *t, char *comment, size_t comment_len)
{
    for (;;) {
        while (!at_end(t) && isspace((unsigned char) here(t)))
            advance(t);
        if (here(t) != '"')
            return;
        advance(t);
        {
            size_t  n = 0;

            while (!at_end(t)) {
                if (here(t) == '"') {
                    advance(t);
                    if (here(t) != '"')
                        break;      /*  the comment ends  */
                }
                if (comment && n + 1 < comment_len)
                    comment[n++] = here(t);
                advance(t);
            }
            if (comment && comment_len)
                comment[n] = '\0';
        }
    }
}

/*  ----------  A very small STON  ----------  */

/*
 *  Only what a Tonel header holds: an object of #key : value pairs, where a
 *  value is a symbol, a string, a list of them, a number, or a constant.
 *  Values arrive as text; the caller knows which keys mean what.
 */

typedef struct {
    char        key[64];
    char        value[256];         /*  scalars  */
    st_names    list;               /*  [ ... ]  */
    int         is_list;
    int         is_nil;
} ston_pair;

#define STON_MAX_PAIRS  24

static int
ston_scalar(tonel *t, char *out, size_t out_len, int *is_nil)
{
    size_t  n = 0;

    *is_nil = 0;
    if (here(t) == '\'' || here(t) == '"') {
        char    quote = here(t);

        advance(t);
        while (!at_end(t)) {
            if (here(t) == quote) {
                advance(t);
                if (here(t) != quote)
                    break;
            }
            if (n + 1 < out_len)
                out[n++] = here(t);
            advance(t);
        }
        out[n] = '\0';
        return 1;
    }
    if (here(t) == '#') {
        advance(t);
        if (here(t) == '\'')
            return ston_scalar(t, out, out_len, is_nil);
    }
    while (!at_end(t) && (isalnum((unsigned char) here(t)) || here(t) == '_'
                       || here(t) == ':' || here(t) == '-' || here(t) == '.'
                       || here(t) == '+' || here(t) == '*' || here(t) == '@')) {
        if (n + 1 < out_len)
            out[n++] = here(t);
        advance(t);
    }
    out[n] = '\0';
    if (strcmp(out, "nil") == 0)
        *is_nil = 1;
    return n > 0;
}

/*  Parse "{ #a : b, #c : [ d, e ] }".  The current character is '{'.  */
static int
ston_object(tonel *t, ston_pair *pairs, unsigned *count, unsigned max)
{
    *count = 0;
    if (here(t) != '{') {
        fail(t, "expected { to open a Tonel header");
        return 0;
    }
    advance(t);
    for (;;) {
        ston_pair  *p;
        int         ignored;

        skip_separators(t, NULL, 0);
        if (here(t) == '}') {
            advance(t);
            return 1;
        }
        if (at_end(t)) {
            fail(t, "a Tonel header is not closed");
            return 0;
        }
        if (here(t) == ',') {
            advance(t);
            continue;
        }
        p = (*count < max) ? &pairs[(*count)++] : NULL;
        {
            char    key[64];

            if (!ston_scalar(t, key, sizeof key, &ignored)) {
                fail(t, "expected a key in a Tonel header");
                return 0;
            }
            if (p)
                snprintf(p->key, sizeof p->key, "%s", key);
        }
        skip_separators(t, NULL, 0);
        if (here(t) != ':') {
            fail(t, "expected : after a Tonel header key");
            return 0;
        }
        advance(t);
        skip_separators(t, NULL, 0);

        if (here(t) == '[') {
            advance(t);
            for (;;) {
                char    item[256];
                int     item_nil;

                skip_separators(t, NULL, 0);
                if (here(t) == ']') {
                    advance(t);
                    break;
                }
                if (at_end(t)) {
                    fail(t, "a Tonel list is not closed");
                    return 0;
                }
                if (here(t) == ',') {
                    advance(t);
                    continue;
                }
                if (!ston_scalar(t, item, sizeof item, &item_nil)) {
                    fail(t, "expected a name in a Tonel list");
                    return 0;
                }
                if (p && item[0])
                    SRC_names_add(&p->list, item);
            }
            if (p)
                p->is_list = 1;
        }  else  {
            char    value[256];
            int     value_nil;

            if (!ston_scalar(t, value, sizeof value, &value_nil)) {
                fail(t, "expected a value in a Tonel header");
                return 0;
            }
            if (p) {
                snprintf(p->value, sizeof p->value, "%s", value);
                p->is_nil = value_nil;
            }
        }
    }
}

static const ston_pair *
pair_named(const ston_pair *pairs, unsigned count, const char *key)
{
    unsigned    i;

    for (i = 0; i < count; ++i) {
        if (strcmp(pairs[i].key, key) == 0)
            return &pairs[i];
    }
    return NULL;
}

static const char *
value_named(const ston_pair *pairs, unsigned count, const char *key)
{
    const ston_pair *p = pair_named(pairs, count, key);

    return (p && !p->is_nil) ? p->value : NULL;
}

/*  ----------  Method bodies  ----------  */

/*
 *  Advance past a bracket-balanced body.  The current character is the
 *  opening '['.  Answers the offset just past the closing ']', or 0.
 */
static size_t
skip_method_body(tonel *t)
{
    int     depth = 0;

    for (;;) {
        char    c;

        if (at_end(t)) {
            fail(t, "a method body is not closed");
            return 0;
        }
        c = here(t);
        if (c == '$') {                 /*  $] and $[ are characters  */
            advance(t);
            advance(t);
            continue;
        }
        if (c == '"' || c == '\'') {    /*  comments and strings      */
            char    quote = c;

            advance(t);
            while (!at_end(t)) {
                if (here(t) == quote) {
                    advance(t);
                    if (here(t) != quote)
                        break;
                }
                advance(t);
            }
            continue;
        }
        if (c == '#' && t->pos + 1 < t->length && t->text[t->pos + 1] == '(') {
            int parens = 0;         /*  a literal array, with its own rules */

            advance(t);
            for (;;) {
                if (at_end(t)) {
                    fail(t, "a literal array is not closed");
                    return 0;
                }
                if (here(t) == '$') {
                    advance(t);
                    advance(t);
                    continue;
                }
                if (here(t) == '"' || here(t) == '\'') {
                    char    q = here(t);

                    advance(t);
                    while (!at_end(t)) {
                        if (here(t) == q) {
                            advance(t);
                            if (here(t) != q)
                                break;
                        }
                        advance(t);
                    }
                    continue;
                }
                if (here(t) == '(')
                    ++parens;
                if (here(t) == ')') {
                    --parens;
                    advance(t);
                    if (parens == 0)
                        break;
                    continue;
                }
                advance(t);
            }
            continue;
        }
        if (c == '[')
            ++depth;
        if (c == ']') {
            --depth;
            advance(t);
            if (depth == 0)
                return t->pos;
            continue;
        }
        advance(t);
    }
}

/*  ----------  The document  ----------  */

/*
 *  A method: "Foo >> bar: x [ ... ]" or "Foo class >> bar [ ... ]".
 *
 *  The pattern and the body are handed to the sink joined by a carriage
 *  return, because that is a complete method as COMPILE_method already
 *  understands one -- so Tonel needs no idea what a selector is, and the
 *  two formats cannot disagree about how a method is read.
 */
static int
read_method(tonel *t, const char *category)
{
    char        class_name[128];
    char        pattern[512];
    char       *source;
    size_t      n = 0;
    size_t      body_start;
    size_t      body_end;
    unsigned    line_at_pattern;
    int         class_side = 0;
    int         ok;

    skip_separators(t, NULL, 0);
    line_at_pattern = t->line;
    while (!at_end(t) && (isalnum((unsigned char) here(t)) || here(t) == '_')
        && n + 1 < sizeof class_name) {
        class_name[n++] = here(t);
        advance(t);
    }
    class_name[n] = '\0';
    if (!class_name[0]) {
        fail(t, "expected a class name before >>");
        return 0;
    }
    skip_separators(t, NULL, 0);
    if (strncmp(t->text + t->pos, "class", 5) == 0
     && !isalnum((unsigned char) t->text[t->pos + 5])) {
        class_side = 1;
        t->pos += 5;
        skip_separators(t, NULL, 0);
    }
    if (strncmp(t->text + t->pos, ">>", 2) != 0) {
        fail(t, "expected >> after %s", class_name);
        return 0;
    }
    t->pos += 2;

    /*  Everything up to the opening bracket is the message pattern.  */
    skip_separators(t, NULL, 0);
    n = 0;
    while (!at_end(t) && here(t) != '[') {
        if (n + 1 < sizeof pattern)
            pattern[n++] = here(t);
        advance(t);
    }
    while (n > 0 && isspace((unsigned char) pattern[n - 1]))
        --n;
    pattern[n] = '\0';
    if (at_end(t)) {
        fail(t, "expected [ to open the body of %s", pattern);
        return 0;
    }

    body_start = t->pos + 1;
    body_end   = skip_method_body(t);
    if (body_end == 0)
        return 0;
    --body_end;                     /*  drop the closing bracket  */

    source = (char *) malloc(strlen(pattern) + (body_end - body_start) + 3);
    if (!source) {
        fail(t, "out of memory reading a method");
        return 0;
    }
    {
        size_t  i;
        size_t  k = strlen(pattern);

        memcpy(source, pattern, k);
        source[k++] = '\r';
        /*
         *  Normalised to CR, which is what a line ending IS in Smalltalk-80
         *  -- the chunk reader does the same, and the image's own Paragraph
         *  and String>>lines only ever split on it.
         */
        for (i = body_start; i < body_end; ++i) {
            char    c = t->text[i];

            if (c == '\r' && i + 1 < body_end && t->text[i + 1] == '\n')
                continue;
            source[k++] = (c == '\n') ? '\r' : c;
        }
        source[k] = '\0';
    }

    ok = t->sink->method
           ? t->sink->method(class_name, class_side, category, source,
                             t->path, line_at_pattern, t->user)
           : 1;
    free(source);
    return ok;
}

/*  Map Tonel's #type onto the shape words the sink understands.  */
static void
apply_type(const char *type, st_source_class_def *def)
{
    if (!type || !type[0])
        return;
    if (strcmp(type, "variable") == 0) {
        def->indexable = 1;
    }  else if (strcmp(type, "bytes") == 0) {
        def->indexable = 1;
        def->bytes     = 1;
    }  else if (strcmp(type, "words") == 0) {
        def->indexable = 1;
        def->words     = 1;
    }  else if (strcmp(type, "normal") != 0) {
        /*
         *  immediate, weak, ephemeron, compiledMethod.  Every one needs
         *  object-memory support this system does not have, and building
         *  the class as an ordinary one would be worse than refusing: it
         *  would load, and then behave differently in a way nothing
         *  reported.
         */
        def->unsupported_shape = type;
    }
}

int
TONEL_read(const char *path, const st_source_sink *sink, void *user,
           char *error, size_t error_len)
{
    tonel       t;
    FILE       *f;
    long        size;
    char       *text;
    char        comment[4096];
    char        type[64];
    ston_pair   pairs[STON_MAX_PAIRS];
    unsigned    pair_count = 0;
    unsigned    i;
    int         ok = 1;
    int         is_extension = 0;

    f = fopen(path, "rb");
    if (!f) {
        snprintf(error, error_len, "cannot open %s", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        snprintf(error, error_len, "cannot size %s", path);
        return 0;
    }
    text = (char *) malloc((size_t) size + 1);
    if (!text) {
        fclose(f);
        snprintf(error, error_len, "out of memory reading %s", path);
        return 0;
    }
    if (fread(text, 1, (size_t) size, f) != (size_t) size) {
        fclose(f);
        free(text);
        snprintf(error, error_len, "cannot read %s", path);
        return 0;
    }
    fclose(f);
    text[size] = '\0';

    memset(&t, 0, sizeof t);
    memset(pairs, 0, sizeof pairs);
    t.text      = text;
    t.length    = (size_t) size;
    t.line      = 1;
    t.path      = path;
    t.sink      = sink;
    t.user      = user;
    t.error     = error;
    t.error_len = error_len;

    comment[0] = '\0';
    skip_separators(&t, comment, sizeof comment);

    /*  The type word.  */
    {
        size_t  n = 0;

        while (!at_end(&t) && isalpha((unsigned char) here(&t))
            && n + 1 < sizeof type) {
            type[n++] = here(&t);
            advance(&t);
        }
        type[n] = '\0';
    }
    if (!type[0]) {
        free(text);
        snprintf(error, error_len, "%s: not a Tonel file", path);
        return 0;
    }
    skip_separators(&t, NULL, 0);
    if (!ston_object(&t, pairs, &pair_count, STON_MAX_PAIRS)) {
        free(text);
        return 0;
    }

    if (strcmp(type, "Package") == 0) {
        /*  Nothing to define; a package file only names the package.  */
        free(text);
        return 1;
    }
    if (strcmp(type, "Extension") == 0) {
        is_extension = 1;
    }  else if (strcmp(type, "Trait") == 0) {
        note(&t, "%s: traits are not supported here",
             value_named(pairs, pair_count, "name"));
        free(text);
        return 1;
    }  else if (strcmp(type, "Class") != 0) {
        free(text);
        snprintf(error, error_len, "%s: unknown Tonel type '%s'", path, type);
        return 0;
    }

    if (!is_extension) {
        st_source_class_def def;
        const ston_pair    *ivars  = pair_named(pairs, pair_count, "instVars");
        const ston_pair    *cvars  = pair_named(pairs, pair_count, "classVars");
        const ston_pair    *civars = pair_named(pairs, pair_count,
                                                "classInstVars");
        const ston_pair    *pools  = pair_named(pairs, pair_count, "pools");
        const char         *super  = value_named(pairs, pair_count,
                                                 "superclass");
        const char         *category;
        st_names            empty;

        memset(&empty, 0, sizeof empty);
        memset(&def, 0, sizeof def);
        def.name       = value_named(pairs, pair_count, "name");
        def.superclass = super ? super : "nil";
        /*
         *  Tonel v3 splits what v1 called a category into a package and a
         *  tag, and keeps #category for compatibility.  Either answers the
         *  question the Browser asks.
         */
        category = value_named(pairs, pair_count, "category");
        if (!category)
            category = value_named(pairs, pair_count, "package");
        def.category    = category ? category : "";
        def.ivars       = ivars  ? &ivars->list  : &empty;
        def.cvars       = cvars  ? &cvars->list  : &empty;
        def.class_ivars = civars ? &civars->list : &empty;
        def.pools       = pools  ? &pools->list  : &empty;
        def.traits      = value_named(pairs, pair_count, "traits");
        apply_type(value_named(pairs, pair_count, "type"), &def);

        if (!def.name) {
            free(text);
            snprintf(error, error_len, "%s: a Tonel class has no #name", path);
            return 0;
        }
        if (sink->class_def && !sink->class_def(&def, user))
            ok = 0;
        if (ok && comment[0] && sink->comment)
            ok = sink->comment(def.name, 0, comment, user);
    }

    /*  Methods, each optionally preceded by its own metadata.  */
    while (ok) {
        char        category[256] = "";
        ston_pair   method_meta[STON_MAX_PAIRS];
        unsigned    meta_count = 0;

        skip_separators(&t, NULL, 0);
        if (at_end(&t))
            break;
        memset(method_meta, 0, sizeof method_meta);
        if (here(&t) == '{') {
            const char *named;

            if (!ston_object(&t, method_meta, &meta_count, STON_MAX_PAIRS)) {
                ok = 0;
                break;
            }
            named = value_named(method_meta, meta_count, "category");
            if (named)
                snprintf(category, sizeof category, "%s", named);
            skip_separators(&t, NULL, 0);
            if (at_end(&t)) {
                for (i = 0; i < meta_count; ++i)
                    SRC_names_free(&method_meta[i].list);
                break;
            }
        }
        ok = read_method(&t, category);
        for (i = 0; i < meta_count; ++i)
            SRC_names_free(&method_meta[i].list);
    }

    for (i = 0; i < pair_count; ++i)
        SRC_names_free(&pairs[i].list);
    free(text);
    return ok;
}

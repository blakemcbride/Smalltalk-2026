/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Source files to events.  See source.h.
 *
 *  This file holds the shared name-list helper and the chunk producer --
 *  the 1983 bang format, whose class-definition parsing moved here from the
 *  bootstrap unchanged.  The Tonel producer is in tonel.c and is reached
 *  through the same SRC_read.
 */

#include "source.h"
#include "chunk.h"
#include "tonel.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  ----------  Names  ----------  */

int
SRC_names_add(st_names *l, const char *text)
{
    if (l->count == l->capacity) {
        unsigned    want = l->capacity ? l->capacity * 2 : 8;
        char      **grown = (char **) realloc(l->items, want * sizeof *grown);

        if (!grown)
            return 0;
        l->items    = grown;
        l->capacity = want;
    }
    l->items[l->count] = strdup(text);
    if (!l->items[l->count])
        return 0;
    ++l->count;
    return 1;
}

void
SRC_names_free(st_names *l)
{
    unsigned    i;

    for (i = 0; i < l->count; ++i)
        free(l->items[i]);
    free(l->items);
    l->items    = NULL;
    l->count    = 0;
    l->capacity = 0;
}

/*  ----------  Files  ----------  */

char *
SRC_slurp(const char *path, size_t *length, char *error, size_t error_len)
{
    FILE   *f = fopen(path, "rb");
    long    size;
    char   *text;

    if (length)
        *length = 0;
    if (!f) {
        snprintf(error, error_len, "cannot open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        snprintf(error, error_len, "cannot size %s", path);
        return NULL;
    }
    text = (char *) malloc((size_t) size + 1);
    if (!text) {
        fclose(f);
        snprintf(error, error_len, "out of memory reading %s", path);
        return NULL;
    }
    if (fread(text, 1, (size_t) size, f) != (size_t) size) {
        fclose(f);
        free(text);
        snprintf(error, error_len, "cannot read %s", path);
        return NULL;
    }
    fclose(f);
    text[size] = '\0';
    if (length)
        *length = (size_t) size;
    return text;
}

/*  ----------  A very small STON  ----------  */

static int  cur_at_end(const st_cursor *c) { return c->pos >= c->length; }
static char cur_here(const st_cursor *c)
{ return c->pos < c->length ? c->text[c->pos] : '\0'; }

static void
cur_advance(st_cursor *c)
{
    if (c->pos < c->length && c->text[c->pos] == '\n')
        ++c->line;
    ++c->pos;
}

void
SRC_skip_separators(st_cursor *c, char *comment, size_t comment_len)
{
    for (;;) {
        while (!cur_at_end(c) && isspace((unsigned char) cur_here(c)))
            cur_advance(c);
        if (cur_here(c) != '"')
            return;
        cur_advance(c);
        {
            size_t  n = 0;

            while (!cur_at_end(c)) {
                if (cur_here(c) == '"') {
                    cur_advance(c);
                    if (cur_here(c) != '"')
                        break;      /*  the comment ends  */
                }
                if (comment && n + 1 < comment_len)
                    comment[n++] = cur_here(c);
                cur_advance(c);
            }
            if (comment && comment_len)
                comment[n] = '\0';
        }
    }
}

static int
ston_scalar(st_cursor *c, char *out, size_t out_len, int *is_nil)
{
    size_t  n = 0;

    *is_nil = 0;
    if (cur_here(c) == '\'' || cur_here(c) == '"') {
        char    quote = cur_here(c);

        cur_advance(c);
        while (!cur_at_end(c)) {
            if (cur_here(c) == quote) {
                cur_advance(c);
                if (cur_here(c) != quote)
                    break;
            }
            if (n + 1 < out_len)
                out[n++] = cur_here(c);
            cur_advance(c);
        }
        out[n] = '\0';
        return 1;
    }
    if (cur_here(c) == '#') {
        cur_advance(c);
        if (cur_here(c) == '\'')
            return ston_scalar(c, out, out_len, is_nil);
    }
    while (!cur_at_end(c)
        && (isalnum((unsigned char) cur_here(c)) || cur_here(c) == '_'
         || cur_here(c) == ':' || cur_here(c) == '-' || cur_here(c) == '.'
         || cur_here(c) == '+' || cur_here(c) == '*' || cur_here(c) == '@'
         || cur_here(c) == '/')) {
        if (n + 1 < out_len)
            out[n++] = cur_here(c);
        cur_advance(c);
    }
    out[n] = '\0';
    if (strcmp(out, "nil") == 0)
        *is_nil = 1;
    return n > 0;
}

int
SRC_ston_object(st_cursor *c, st_ston_pair *pairs, unsigned *count,
                unsigned max, char *error, size_t error_len)
{
    *count = 0;
    if (cur_here(c) != '{') {
        snprintf(error, error_len, "line %u: expected { to open a header",
                 c->line);
        return 0;
    }
    cur_advance(c);
    for (;;) {
        st_ston_pair   *p;
        int             ignored;

        SRC_skip_separators(c, NULL, 0);
        if (cur_here(c) == '}') {
            cur_advance(c);
            return 1;
        }
        if (cur_at_end(c)) {
            snprintf(error, error_len, "line %u: a header is not closed",
                     c->line);
            return 0;
        }
        if (cur_here(c) == ',') {
            cur_advance(c);
            continue;
        }
        p = (*count < max) ? &pairs[(*count)++] : NULL;
        {
            char    key[64];

            if (!ston_scalar(c, key, sizeof key, &ignored)) {
                snprintf(error, error_len, "line %u: expected a key", c->line);
                return 0;
            }
            if (p)
                snprintf(p->key, sizeof p->key, "%s", key);
        }
        SRC_skip_separators(c, NULL, 0);
        if (cur_here(c) != ':') {
            snprintf(error, error_len, "line %u: expected : after a key",
                     c->line);
            return 0;
        }
        cur_advance(c);
        SRC_skip_separators(c, NULL, 0);

        if (cur_here(c) == '[') {
            cur_advance(c);
            for (;;) {
                char    item[256];
                int     item_nil;

                SRC_skip_separators(c, NULL, 0);
                if (cur_here(c) == ']') {
                    cur_advance(c);
                    break;
                }
                if (cur_at_end(c)) {
                    snprintf(error, error_len, "line %u: a list is not closed",
                             c->line);
                    return 0;
                }
                if (cur_here(c) == ',') {
                    cur_advance(c);
                    continue;
                }
                if (!ston_scalar(c, item, sizeof item, &item_nil)) {
                    snprintf(error, error_len, "line %u: expected a name",
                             c->line);
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

            if (!ston_scalar(c, value, sizeof value, &value_nil)) {
                snprintf(error, error_len, "line %u: expected a value",
                         c->line);
                return 0;
            }
            if (p) {
                snprintf(p->value, sizeof p->value, "%s", value);
                p->is_nil = value_nil;
            }
        }
    }
}

void
SRC_ston_free(st_ston_pair *pairs, unsigned count)
{
    unsigned    i;

    for (i = 0; i < count; ++i)
        SRC_names_free(&pairs[i].list);
}

static const st_ston_pair *
ston_pair_named(const st_ston_pair *pairs, unsigned count, const char *key)
{
    unsigned    i;

    for (i = 0; i < count; ++i) {
        if (strcmp(pairs[i].key, key) == 0)
            return &pairs[i];
    }
    return NULL;
}

const char *
SRC_ston_value(const st_ston_pair *pairs, unsigned count, const char *key)
{
    const st_ston_pair *p = ston_pair_named(pairs, count, key);

    return (p && !p->is_nil && !p->is_list) ? p->value : NULL;
}

const st_names *
SRC_ston_list(const st_ston_pair *pairs, unsigned count, const char *key)
{
    const st_ston_pair *p = ston_pair_named(pairs, count, key);

    return (p && p->is_list) ? &p->list : NULL;
}

/*  ----------  Scraping chunk text  ----------  */

/*  Pull the contents of the first single-quoted string out of a chunk.  */
static int
quoted_after(const char *text, const char *keyword, char *out, size_t outlen)
{
    const char *p = strstr(text, keyword);
    const char *q;
    size_t      n = 0;

    out[0] = '\0';
    if (!p)
        return 0;
    p += strlen(keyword);
    q = strchr(p, '\'');
    if (!q)
        return 0;
    ++q;
    while (*q && *q != '\'' && n + 1 < outlen)
        out[n++] = *q++;
    out[n] = '\0';
    return 1;
}

static void
split_words(const char *text, st_names *out, unsigned limit)
{
    const char *p = text;

    while (*p && out->count < limit) {
        char    word[256];
        size_t  n = 0;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            ++p;
        if (!*p)
            break;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r'
            && n + 1 < sizeof word)
            word[n++] = *p++;
        word[n] = '\0';
        if (n)
            SRC_names_add(out, word);
    }
}

#define CHUNK_MAX_NAMES 256

/*
 *  "Foo class instanceVariableNames: 'a b'" -- the metaclass side.  Tried
 *  before the class-definition form because both mention
 *  instanceVariableNames:.
 */
static int
chunk_class_side_definition(const char *text, const st_source_sink *sink,
                            void *user, int *stopped)
{
    const char *at = strstr(text, " class");
    const char *p;
    char        name[256];
    char        ivars[512];
    size_t      n = 0;
    st_names    list;
    int         ok;

    if (!at || !strstr(text, "instanceVariableNames:"))
        return 0;
    /*  Anything else on the line means this is not a bare "Foo class".  */
    if (strstr(text, "subclass:"))
        return 0;

    p = text;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        ++p;
    while (p < at && (isalnum((unsigned char) *p) || *p == '_')
        && n + 1 < sizeof name)
        name[n++] = *p++;
    name[n] = '\0';
    if (!name[0] || p != at)
        return 0;

    memset(&list, 0, sizeof list);
    if (quoted_after(text, "instanceVariableNames:", ivars, sizeof ivars))
        split_words(ivars, &list, CHUNK_MAX_NAMES);
    ok = sink->class_side_def ? sink->class_side_def(name, &list, user) : 1;
    SRC_names_free(&list);
    if (!ok)
        *stopped = 1;
    return 1;
}

static int
chunk_class_definition(const char *text, const st_source_sink *sink,
                       void *user, int *stopped)
{
    static const struct {
        const char *keyword;
        int         indexable;
        int         bytes;
        int         words;
    } forms[] = {
        { " variableByteSubclass: #", 1, 1, 0 },
        { " variableWordSubclass: #", 1, 0, 1 },
        { " variableSubclass: #",     1, 0, 0 },
        { " subclass: #",             0, 0, 0 }
    };
    unsigned            f;
    const char         *at = NULL;
    unsigned            form = 0;
    const char         *p;
    size_t              n;
    char                buffer[512];
    char                name[256];
    char                superclass[256];
    char                category[256] = "";
    st_names            ivars, cvars, pools;
    st_source_class_def def;
    int                 ok;

    for (f = 0; f < sizeof forms / sizeof forms[0]; ++f) {
        at = strstr(text, forms[f].keyword);
        if (at) {
            form = f;
            break;
        }
    }
    if (!at)
        return 0;

    /*  The superclass name is the word before the keyword.  */
    p = at;
    while (p > text && (p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\r'
                     || p[-1] == '\t'))
        --p;
    {
        const char *end = p;

        while (p > text && (isalnum((unsigned char) p[-1]) || p[-1] == '_'))
            --p;
        n = (size_t) (end - p);
        if (n >= sizeof superclass)
            n = sizeof superclass - 1;
        memcpy(superclass, p, n);
        superclass[n] = '\0';
    }

    p = at + strlen(forms[form].keyword);
    n = 0;
    while (*p && (isalnum((unsigned char) *p) || *p == '_')
        && n + 1 < sizeof name)
        name[n++] = *p++;
    name[n] = '\0';
    if (!name[0])
        return 0;

    memset(&ivars, 0, sizeof ivars);
    memset(&cvars, 0, sizeof cvars);
    memset(&pools, 0, sizeof pools);
    if (quoted_after(text, "instanceVariableNames:", buffer, sizeof buffer))
        split_words(buffer, &ivars, CHUNK_MAX_NAMES);
    if (quoted_after(text, "classVariableNames:", buffer, sizeof buffer))
        split_words(buffer, &cvars, CHUNK_MAX_NAMES);
    if (quoted_after(text, "poolDictionaries:", buffer, sizeof buffer))
        split_words(buffer, &pools, 4);
    if (quoted_after(text, "category:", buffer, sizeof buffer))
        snprintf(category, sizeof category, "%.63s", buffer);

    memset(&def, 0, sizeof def);
    def.name        = name;
    def.superclass  = superclass;
    def.category    = category;
    def.ivars       = &ivars;
    def.cvars       = &cvars;
    def.pools       = &pools;
    def.indexable   = forms[form].indexable;
    def.bytes       = forms[form].bytes;
    def.words       = forms[form].words;

    ok = sink->class_def ? sink->class_def(&def, user) : 1;
    SRC_names_free(&ivars);
    SRC_names_free(&cvars);
    SRC_names_free(&pools);
    if (!ok)
        *stopped = 1;
    return 1;
}

/*
 *  "!Foo methodsFor: 'accessing'!" or "!Foo class methodsFor: '...'!".
 */
static int
chunk_methods_for(const char *text, char *name, size_t name_len,
                  int *class_side, char *protocol, size_t protocol_len)
{
    const char *p = text;
    const char *at = strstr(text, "methodsFor:");
    size_t      n = 0;

    *class_side = 0;
    if (protocol && protocol_len)
        protocol[0] = '\0';
    if (!at)
        return 0;
    /*  The protocol this run of methods belongs to, for the Browser.  */
    if (protocol && protocol_len)
        quoted_after(at, "methodsFor:", protocol, protocol_len);
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        ++p;
    while (*p && (isalnum((unsigned char) *p) || *p == '_')
        && n + 1 < name_len)
        name[n++] = *p++;
    name[n] = '\0';
    if (!name[0])
        return 0;
    while (*p == ' ')
        ++p;
    if (strncmp(p, "class", 5) == 0)
        *class_side = 1;
    return 1;
}

/*  ----------  The chunk producer  ----------  */

static int
read_chunks(const char *path, const st_source_sink *sink, void *user,
            char *error, size_t error_len)
{
    st_chunk_reader    *reader;
    st_chunk            chunk;
    char                class_name[256];
    char                protocol[256] = "";
    int                 class_side = 0;
    int                 in_methods = 0;
    int                 have_class = 0;
    int                 stopped = 0;

    reader = CHUNK_open(path, error, error_len);
    if (!reader)
        return 0;

    while (!stopped && CHUNK_next(reader, &chunk)) {
        /*
         *  A chunk with nothing to compile closes the method category.  That
         *  is the empty chunk of the "! !" idiom, and equally the comment
         *  the markbush sources use in its place.
         */
        if (!chunk.has_code) {
            in_methods = 0;
            have_class = 0;
            continue;
        }
        if (chunk.is_reader) {
            if (chunk_methods_for(chunk.text, class_name, sizeof class_name,
                                  &class_side, protocol, sizeof protocol)) {
                in_methods = 1;
                have_class = 1;
            }
            continue;
        }
        if (in_methods) {
            if (have_class && sink->method
             && !sink->method(class_name, class_side, protocol, chunk.text,
                              path, CHUNK_line(reader), user))
                stopped = 1;
            continue;
        }
        if (!chunk_class_side_definition(chunk.text, sink, user, &stopped))
            chunk_class_definition(chunk.text, sink, user, &stopped);
    }
    CHUNK_close(reader);
    return !stopped;
}

/*  ----------  Dispatch  ----------  */

static int
ends_with(const char *path, const char *suffix)
{
    size_t  n = strlen(path);
    size_t  m = strlen(suffix);

    return n >= m && strcmp(path + n - m, suffix) == 0;
}

const char *
SRC_format_of(const char *path)
{
    if (ends_with(path, ".class.st") || ends_with(path, ".extension.st")
     || ends_with(path, ".trait.st") || ends_with(path, "package.st"))
        return "tonel";
    return "chunk";
}

int
SRC_read(const char *path, const st_source_sink *sink, void *user,
         char *error, size_t error_len)
{
    if (error && error_len)
        error[0] = '\0';
    if (strcmp(SRC_format_of(path), "tonel") == 0)
        return TONEL_read(path, sink, user, error, error_len);
    return read_chunks(path, sink, user, error, error_len);
}

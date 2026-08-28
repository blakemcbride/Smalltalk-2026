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
    st_cursor   c;
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
        snprintf(t->error, t->error_len, "%s:%u: %s", t->path, t->c.line,
                 detail);
}

static int  at_end(const tonel *t) { return t->c.pos >= t->c.length; }
static char here(const tonel *t)
{ return t->c.pos < t->c.length ? t->c.text[t->c.pos] : '\0'; }

static void
advance(tonel *t)
{
    if (t->c.pos < t->c.length && t->c.text[t->c.pos] == '\n')
        ++t->c.line;
    ++t->c.pos;
}

static void
skip_separators(tonel *t, char *comment, size_t comment_len)
{
    SRC_skip_separators(&t->c, comment, comment_len);
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
        if (c == '#' && t->c.pos + 1 < t->c.length && t->c.text[t->c.pos + 1] == '(') {
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
                return t->c.pos;
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
    line_at_pattern = t->c.line;
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
    if (strncmp(t->c.text + t->c.pos, "class", 5) == 0
     && !isalnum((unsigned char) t->c.text[t->c.pos + 5])) {
        class_side = 1;
        t->c.pos += 5;
        skip_separators(t, NULL, 0);
    }
    if (strncmp(t->c.text + t->c.pos, ">>", 2) != 0) {
        fail(t, "expected >> after %s", class_name);
        return 0;
    }
    t->c.pos += 2;

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

    body_start = t->c.pos + 1;
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
            char    c = t->c.text[i];

            if (c == '\r' && i + 1 < body_end && t->c.text[i + 1] == '\n')
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
    }  else if (strcmp(type, "weak") == 0) {
        /*
         *  A weak class: indexed, and the collector does not follow those
         *  indexed fields.  The named ones at the front stay strong.
         */
        def->indexable = 1;
        def->weak      = 1;
    }  else if (strcmp(type, "ephemeron") == 0) {
        /*
         *  An ephemeron.  Its first named field is a key held weakly, and
         *  the whole object -- key included -- is strong exactly as long as
         *  that key is reachable some other way.
         *
         *  This was refused for a long time on the ground that a single
         *  marking pass cannot decide it, which was true and was the wrong
         *  conclusion: the answer is a fixed point, and a fixed point is a
         *  loop around the pass rather than a different collector.  The
         *  loop is in om_mt.c.  Nothing indexed is implied; Pharo's
         *  WeakKeyAssociation has three named fields and no indexed part.
         */
        def->ephemeron = 1;
    }  else if (strcmp(type, "immediate") == 0) {
        /*
         *  An immediate has no object header: the value IS the pointer.
         *  There is one tag bit in this memory and SmallInteger has it, so
         *  a NEW immediate class cannot be made.
         *
         *  But the two Pharo declares immediate are already immediate here
         *  by other means -- SmallInteger is the tagged one, and every
         *  Character is a unique entry in CharacterTable, which is what
         *  makes $a == $a true.  Accepting those two and refusing the rest
         *  is the honest reading: it lets Pharo's own declarations load
         *  without pretending a third one could.
         */
        if (!def->name
         || (strcmp(def->name, "SmallInteger") != 0
          && strcmp(def->name, "Character") != 0))
            def->unsupported_shape = "an immediate class";
    }  else if (strcmp(type, "normal") != 0) {
        /*
         *  immediate, ephemeron, compiledMethod.  Each needs object-memory
         *  support this system does not have, and building the class as an
         *  ordinary one would be worse than refusing: it would load, and
         *  then behave differently with nothing to say so.
         *
         *  An ephemeron is not merely a weak object with a different name.
         *  Its key is weak but its value is strong FOR AS LONG AS the key
         *  lives, which a single marking pass cannot decide -- it needs a
         *  fixed point, and that is a different collector rather than a
         *  different flag.
         */
        def->unsupported_shape = type;
    }
}

/*
 *  Whether #classTraits is the mechanical companion of #traits.
 *
 *  Pharo writes the pair together: a class using TFoo gets
 *  #traits : 'TFoo' and #classTraits : 'TFoo classTrait'.  The second says
 *  nothing the first does not, because a trait here carries its class-side
 *  methods with it, so the companion form is accepted and dropped.  Anything
 *  else -- a class trait composed differently from its instance trait -- is
 *  a real statement this system would silently lose, so the class is
 *  refused instead.
 *
 *  The test is the whole of the check: delete every "classTrait" and see
 *  whether what is left is #traits, whitespace aside.
 */
static int
same_words(const char *a, const char *b)
{
    for (;;) {
        while (*a && isspace((unsigned char) *a))
            ++a;
        while (*b && isspace((unsigned char) *b))
            ++b;
        if (!*a || !*b)
            return !*a && !*b;
        if (*a != *b)
            return 0;
        ++a;
        ++b;
    }
}

/*
 *  A composition with its instance-side exclusions taken back out.
 *
 *  `(TCreationWithTest - {#testOfSize})' and `TCreationWithTest classTrait'
 *  ARE the mechanical pair: an exclusion narrows what the instance side
 *  takes and says nothing about the class side, so Pharo writes it on one
 *  and not the other.  Comparing the two literally makes every class that
 *  excludes anything look like it has a class trait of its own, and refuses
 *  it -- which is what happened to BagTest, and through BagTest to
 *  IdentityBagTest, and through those two to the whole test package.
 */
static void
strip_exclusions(const char *in, char *out, size_t out_len)
{
    size_t  n = 0;

    while (*in && n + 1 < out_len) {
        if (*in == '(' || *in == ')') {
            ++in;
            continue;
        }
        if (*in == '-') {
            /*  Skip "- { ... }" whole.  */
            ++in;
            while (*in && *in != '{')
                ++in;
            if (*in == '{') {
                while (*in && *in != '}')
                    ++in;
                if (*in == '}')
                    ++in;
            }
            continue;
        }
        out[n++] = *in++;
    }
    out[n] = '\0';
}

static int
class_traits_are_mechanical(const char *class_traits, const char *traits)
{
    static const char   suffix[] = "classTrait";
    char                stripped[1024];
    char                wanted[1024];
    size_t              n = 0;
    const char         *p = class_traits;

    while (*p && n + 1 < sizeof stripped) {
        if (strncmp(p, suffix, sizeof suffix - 1) == 0) {
            p += sizeof suffix - 1;
            continue;
        }
        stripped[n++] = *p++;
    }
    stripped[n] = '\0';
    strip_exclusions(traits ? traits : "", wanted, sizeof wanted);
    return same_words(stripped, wanted);
}

int
TONEL_read(const char *path, const st_source_sink *sink, void *user,
           char *error, size_t error_len)
{
    tonel       t;
    FILE       *f;
    long        size;
    char       *text;
    /*
     *  The class comment, however long it is.  It used to be `char
     *  comment[4096]', which is a ceiling on how much a class may say about
     *  itself -- silently applied, and several of this system's own class
     *  comments are most of the way to it.
     */
    char       *comment = NULL;
    char        type[64];
    st_ston_pair pairs[ST_STON_MAX_PAIRS];
    unsigned    pair_count = 0;
    int         ok = 1;
    int         is_extension = 0;
    int         is_trait = 0;

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
    t.c.text    = text;
    t.c.length  = (size_t) size;
    t.c.line    = 1;
    t.path      = path;
    t.sink      = sink;
    t.user      = user;
    t.error     = error;
    t.error_len = error_len;

    /*
     *  A UTF-8 byte-order mark, if an editor left one.  Three bytes that
     *  mean nothing and that Windows editors and some Pharo exports put
     *  in front of the first character; the loop below took them for a
     *  type word of no letters and refused the file as "not a Tonel file"
     *  (Bugs3 B63).  Skipped here, before the comment, since a file whose
     *  first character is the comment's opening quote has the mark in
     *  front of that.  TonelReader>>setText:path: does the same.
     */
    if (t.c.length >= 3 && (unsigned char) text[0] == 0xEF
     && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF)
        t.c.pos = 3;

    comment = SRC_take_comment(&t.c);

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
        free(comment);
        free(text);
        snprintf(error, error_len, "%s: not a Tonel file", path);
        return 0;
    }
    skip_separators(&t, NULL, 0);
    if (!SRC_ston_object(&t.c, pairs, &pair_count, ST_STON_MAX_PAIRS,
                         error, error_len)) {
        free(comment);
        free(text);
        return 0;
    }

    if (strcmp(type, "Package") == 0) {
        /*  Nothing to define; a package file only names the package.  */
        free(comment);
        free(text);
        return 1;
    }
    if (strcmp(type, "Extension") == 0) {
        is_extension = 1;
    }  else if (strcmp(type, "Trait") == 0) {
        is_trait = 1;
    }  else if (strcmp(type, "Class") != 0) {
        free(comment);
        free(text);
        snprintf(error, error_len, "%s: unknown Tonel type '%s'", path, type);
        return 0;
    }

    if (!is_extension) {
        st_source_class_def def;
        const st_names     *ivars  = SRC_ston_list(pairs, pair_count, "instVars");
        const st_ston_pair *slots  = SRC_ston_pair_named(pairs, pair_count,
                                                        "slots");
        const st_names     *cvars  = SRC_ston_list(pairs, pair_count, "classVars");
        const st_names     *civars = SRC_ston_list(pairs, pair_count,
                                                  "classInstVars");
        const st_names     *pools  = SRC_ston_list(pairs, pair_count, "pools");
        const char         *super  = SRC_ston_value(pairs, pair_count,
                                                 "superclass");
        const char         *category;
        st_names            empty;
        char                shape[128];

        memset(&empty, 0, sizeof empty);
        memset(&def, 0, sizeof def);
        def.name       = SRC_ston_value(pairs, pair_count, "name");
        def.superclass = super ? super : "nil";
        def.is_trait   = is_trait;
        /*
         *  Tonel v3 splits what v1 called a category into a package and a
         *  tag, and keeps #category for compatibility.  Either answers the
         *  question the Browser asks.
         */
        category = SRC_ston_value(pairs, pair_count, "category");
        if (!category)
            category = SRC_ston_value(pairs, pair_count, "package");
        def.category    = category ? category : "";
        /*
         *  #slots is Tonel v3's spelling of #instVars, and for a plain slot
         *  the two say the same thing: a named field of the instance.  A
         *  slot with a KIND -- "#a => WeakSlot" -- is a metamodel feature
         *  this system does not have, where reading and writing the
         *  variable go through the slot object.  Building it as an ordinary
         *  instance variable would load and then behave differently, so it
         *  is refused by the name of the kind.
         */
        if (slots && slots->is_qualified) {
            snprintf(shape, sizeof shape, "a slot of kind %.63s",
                     slots->qualifier);
            def.unsupported_shape = shape;
        }  else if (slots && !ivars) {
            ivars = &slots->list;
        }
        def.ivars       = ivars  ? ivars  : &empty;
        def.cvars       = cvars  ? cvars  : &empty;
        def.class_ivars = civars ? civars : &empty;
        def.pools       = pools  ? pools  : &empty;
        def.traits      = SRC_ston_value(pairs, pair_count, "traits");
        apply_type(SRC_ston_value(pairs, pair_count, "type"), &def);
        /*
         *  A trait with state would have to add fields to every class that
         *  uses it, which changes instance shape from a direction nothing
         *  else here does.  Refused by name rather than loaded without its
         *  variables, which would compile and then read the wrong field.
         */
        if (is_trait && def.ivars->count)
            def.unsupported_shape = "a trait with instance variables";
        {
            const char *class_traits = SRC_ston_value(pairs, pair_count,
                                                      "classTraits");

            if (class_traits && class_traits[0]
             && !class_traits_are_mechanical(class_traits, def.traits))
                def.unsupported_shape = "a #classTraits of its own";
        }

        if (!def.name) {
            free(comment);
            free(text);
            snprintf(error, error_len, "%s: a Tonel class has no #name", path);
            return 0;
        }
        if (sink->class_def && !sink->class_def(&def, user))
            ok = 0;
        if (ok && comment && comment[0] && sink->comment)
            ok = sink->comment(def.name, 0, comment, user);
    }

    /*  Methods, each optionally preceded by its own metadata.  */
    while (ok) {
        char        category[256] = "";
        st_ston_pair method_meta[ST_STON_MAX_PAIRS];
        unsigned    meta_count = 0;

        skip_separators(&t, NULL, 0);
        if (at_end(&t))
            break;
        memset(method_meta, 0, sizeof method_meta);
        if (here(&t) == '{') {
            const char *named;

            if (!SRC_ston_object(&t.c, method_meta, &meta_count,
                                 ST_STON_MAX_PAIRS, error, error_len)) {
                ok = 0;
                break;
            }
            named = SRC_ston_value(method_meta, meta_count, "category");
            if (named)
                snprintf(category, sizeof category, "%s", named);
            skip_separators(&t, NULL, 0);
            if (at_end(&t)) {
                            SRC_ston_free(method_meta, meta_count);
                break;
            }
        }
        ok = read_method(&t, category);
        SRC_ston_free(method_meta, meta_count);
    }

    SRC_ston_free(pairs, pair_count);
    free(comment);
    free(text);
    return ok;
}

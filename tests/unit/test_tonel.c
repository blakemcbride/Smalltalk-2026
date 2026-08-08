/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Reading source files, in both formats.
 *
 *  These drive SRC_read with a recording sink rather than through a
 *  bootstrap, so they need no object memory and run under either build.
 *  What they are for is the property Phase C is gated on: the same class
 *  written as a 1983 chunk file and as a Pharo Tonel file has to arrive as
 *  the same events, because that is what makes the two formats produce the
 *  same image.
 *
 *  The bracket cases matter more than they look.  A method body ends at the
 *  ']' balancing its '[', and a scanner that miscounts one inside a string,
 *  a comment, a character literal or a literal array does not report an
 *  error -- it silently swallows the next method.  So each of those is a
 *  test rather than a comment.
 */

#include "st_test.h"

#include "source.h"
#include "profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  ----------  A sink that records  ----------  */

#define MAX_EVENTS  64

typedef struct {
    char        class_name[128];
    int         class_side;
    char        category[128];
    char        source[2048];
    unsigned    line;
} rec_method;

typedef struct {
    char        name[128];
    char        superclass[128];
    char        category[128];
    char        ivars[512];
    char        class_ivars[512];
    char        cvars[512];
    int         indexable, bytes, words, weak, is_trait;
    char        unsupported[64];
    char        traits[128];
} rec_class;

typedef struct {
    rec_class   classes[MAX_EVENTS];
    unsigned    class_count;
    rec_method  methods[MAX_EVENTS];
    unsigned    method_count;
    unsigned    comments;
    unsigned    diagnostics;
    char        last_diagnostic[256];
} recorder;

static void
join(const st_names *l, char *out, size_t out_len)
{
    unsigned    i;

    out[0] = '\0';
    for (i = 0; l && i < l->count; ++i) {
        if (i)
            strncat(out, " ", out_len - strlen(out) - 1);
        strncat(out, l->items[i], out_len - strlen(out) - 1);
    }
}

static int
rec_class_def(const st_source_class_def *def, void *user)
{
    recorder   *r = (recorder *) user;
    rec_class  *c;

    if (r->class_count >= MAX_EVENTS)
        return 1;
    c = &r->classes[r->class_count++];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", def->name ? def->name : "");
    snprintf(c->superclass, sizeof c->superclass, "%s",
             def->superclass ? def->superclass : "");
    snprintf(c->category, sizeof c->category, "%s",
             def->category ? def->category : "");
    join(def->ivars, c->ivars, sizeof c->ivars);
    join(def->class_ivars, c->class_ivars, sizeof c->class_ivars);
    join(def->cvars, c->cvars, sizeof c->cvars);
    c->indexable = def->indexable;
    c->bytes     = def->bytes;
    c->words     = def->words;
    c->weak      = def->weak;
    snprintf(c->unsupported, sizeof c->unsupported, "%s",
             def->unsupported_shape ? def->unsupported_shape : "");
    snprintf(c->traits, sizeof c->traits, "%s",
             def->traits ? def->traits : "");
    c->is_trait = def->is_trait;
    return 1;
}

static int
rec_class_side_def(const char *name, const st_names *ivars, void *user)
{
    recorder   *r = (recorder *) user;
    unsigned    i;

    /*  The chunk format states these separately; Tonel folds them in.  */
    for (i = 0; i < r->class_count; ++i) {
        if (strcmp(r->classes[i].name, name) == 0) {
            join(ivars, r->classes[i].class_ivars,
                 sizeof r->classes[i].class_ivars);
            return 1;
        }
    }
    return 1;
}

static int
rec_comment(const char *class_name, int class_side, const char *text,
            void *user)
{
    recorder   *r = (recorder *) user;

    (void) class_name;
    (void) class_side;
    (void) text;
    ++r->comments;
    return 1;
}

static int
rec_method_event(const char *class_name, int class_side, const char *category,
                 const char *source, const char *file, unsigned line,
                 void *user)
{
    recorder   *r = (recorder *) user;
    rec_method *m;

    (void) file;
    if (r->method_count >= MAX_EVENTS)
        return 1;
    m = &r->methods[r->method_count++];
    memset(m, 0, sizeof *m);
    snprintf(m->class_name, sizeof m->class_name, "%s", class_name);
    m->class_side = class_side;
    snprintf(m->category, sizeof m->category, "%s", category ? category : "");
    snprintf(m->source, sizeof m->source, "%s", source);
    m->line = line;
    return 1;
}

static void
rec_diagnostic(const char *file, unsigned line, const char *message,
               void *user)
{
    recorder   *r = (recorder *) user;

    (void) file;
    (void) line;
    ++r->diagnostics;
    snprintf(r->last_diagnostic, sizeof r->last_diagnostic, "%s", message);
}

static const st_source_sink recording_sink = {
    rec_class_def, rec_class_side_def, rec_comment, rec_method_event,
    rec_diagnostic
};

/*  ----------  Reading a string as a file  ----------  */

static int
read_text(const char *name, const char *text, recorder *out, char *error,
          size_t error_len)
{
    char    path[512];
    FILE   *f;
    int     ok;

    snprintf(path, sizeof path, "/tmp/st2026-test-%s", name);
    f = fopen(path, "wb");
    if (!f) {
        printf("  cannot write %s\n", path);
        return 0;
    }
    fwrite(text, 1, strlen(text), f);
    fclose(f);

    memset(out, 0, sizeof *out);
    ok = SRC_read(path, &recording_sink, out, error, error_len);
    remove(path);
    return ok;
}

/*  Collapse a method's source so the two formats can be compared.  */
static void
squeeze(const char *in, char *out, size_t out_len)
{
    size_t  n = 0;
    int     was_space = 1;

    for (; *in && n + 1 < out_len; ++in) {
        int is_space = (*in == ' ' || *in == '\t' || *in == '\r'
                     || *in == '\n');

        if (is_space) {
            if (!was_space)
                out[n++] = ' ';
        }  else  {
            out[n++] = *in;
        }
        was_space = is_space;
    }
    while (n > 0 && out[n - 1] == ' ')
        --n;
    out[n] = '\0';
}

/*  ----------  The tests  ----------  */

static const char *const tonel_twin =
"\"A class comment.\"\n"
"Class {\n"
"\t#name : 'Twin',\n"
"\t#superclass : 'Object',\n"
"\t#instVars : [ 'left', 'right' ],\n"
"\t#classVars : [ 'Tally' ],\n"
"\t#classInstVars : [ 'made' ],\n"
"\t#category : 'Probe-Core'\n"
"}\n"
"\n"
"{ #category : 'accessing' }\n"
"Twin >> left [\n"
"\t^left\n"
"]\n"
"\n"
"{ #category : 'accessing' }\n"
"Twin >> left: a right: b [\n"
"\tleft := a.\n"
"\tright := b\n"
"]\n"
"\n"
"{ #category : 'instance creation' }\n"
"Twin class >> withLeft: a right: b [\n"
"\t^self new left: a right: b; yourself\n"
"]\n";

static const char *const chunk_twin =
"Object subclass: #Twin\n"
"  instanceVariableNames: 'left right'\n"
"  classVariableNames: 'Tally'\n"
"  poolDictionaries: ''\n"
"  category: 'Probe-Core'!\n"
"\n"
"Twin class\n"
"  instanceVariableNames: 'made'!\n"
"\n"
"!Twin methodsFor: 'accessing'!\n"
"left\n"
"\t^left!\n"
"left: a right: b\n"
"\tleft := a.\n"
"\tright := b! !\n"
"\n"
"!Twin class methodsFor: 'instance creation'!\n"
"withLeft: a right: b\n"
"\t^self new left: a right: b; yourself! !\n";

static void
test_tonel_reads_a_class(void)
{
    recorder    r;
    char        error[512] = "";

    CHECK(read_text("twin.class.st", tonel_twin, &r, error, sizeof error));
    if (error[0])
        printf("  error: %s\n", error);

    CHECK_EQ_INT((int) r.class_count, 1);
    if (r.class_count != 1)
        return;
    CHECK_EQ_STR(r.classes[0].name, "Twin");
    CHECK_EQ_STR(r.classes[0].superclass, "Object");
    CHECK_EQ_STR(r.classes[0].category, "Probe-Core");
    CHECK_EQ_STR(r.classes[0].ivars, "left right");
    CHECK_EQ_STR(r.classes[0].cvars, "Tally");
    CHECK_EQ_STR(r.classes[0].class_ivars, "made");
    CHECK_EQ_INT(r.classes[0].indexable, 0);
    CHECK_EQ_INT((int) r.comments, 1);

    CHECK_EQ_INT((int) r.method_count, 3);
    if (r.method_count != 3)
        return;
    CHECK_EQ_STR(r.methods[0].class_name, "Twin");
    CHECK_EQ_INT(r.methods[0].class_side, 0);
    CHECK_EQ_STR(r.methods[0].category, "accessing");
    CHECK_EQ_INT(r.methods[2].class_side, 1);
    CHECK_EQ_STR(r.methods[2].category, "instance creation");
}

/*
 *  The property the phase is gated on.  Both formats describe one class;
 *  both must arrive as the same events, down to the method source once
 *  layout is normalised away.
 */
static void
test_the_two_formats_agree(void)
{
    recorder    tonel;
    recorder    chunk;
    char        error[512] = "";
    unsigned    i;

    CHECK(read_text("twin.class.st", tonel_twin, &tonel, error, sizeof error));
    CHECK(read_text("twin.stClass", chunk_twin, &chunk, error, sizeof error));

    CHECK_EQ_INT((int) tonel.class_count, (int) chunk.class_count);
    CHECK_EQ_INT((int) tonel.method_count, (int) chunk.method_count);
    if (tonel.class_count != chunk.class_count
     || tonel.method_count != chunk.method_count)
        return;

    CHECK_EQ_STR(tonel.classes[0].name,        chunk.classes[0].name);
    CHECK_EQ_STR(tonel.classes[0].superclass,  chunk.classes[0].superclass);
    CHECK_EQ_STR(tonel.classes[0].category,    chunk.classes[0].category);
    CHECK_EQ_STR(tonel.classes[0].ivars,       chunk.classes[0].ivars);
    CHECK_EQ_STR(tonel.classes[0].cvars,       chunk.classes[0].cvars);
    CHECK_EQ_STR(tonel.classes[0].class_ivars, chunk.classes[0].class_ivars);

    for (i = 0; i < tonel.method_count; ++i) {
        char    a[2048];
        char    b[2048];

        CHECK_EQ_STR(tonel.methods[i].class_name, chunk.methods[i].class_name);
        CHECK_EQ_INT(tonel.methods[i].class_side, chunk.methods[i].class_side);
        CHECK_EQ_STR(tonel.methods[i].category,   chunk.methods[i].category);
        squeeze(tonel.methods[i].source, a, sizeof a);
        squeeze(chunk.methods[i].source, b, sizeof b);
        CHECK_EQ_STR(a, b);
    }
}

/*
 *  Brackets the body scanner must not count.  A mistake here is silent: the
 *  method simply eats the one after it, so the count is the assertion.
 */
static void
test_brackets_that_are_not_brackets(void)
{
    static const char *const source =
    "Class { #name : 'B', #superclass : 'Object', #category : 'P' }\n"
    "B >> inAString [ ^'a ] string [ here' ]\n"
    "B >> inAComment [ \"a ] comment [ here\" ^1 ]\n"
    "B >> asACharacter [ ^$] ]\n"
    "B >> openAsACharacter [ ^$[ ]\n"
    "B >> inALiteralArray [ ^#($[ $] 'x ] y' #z) ]\n"
    "B >> aByteArray [ ^#[1 2 3] ]\n"
    "B >> nested [ ^[[1] value] value ]\n"
    "B >> doubledQuoteInAString [ ^'it''s ] fine' ]\n"
    "B >> last [ ^42 ]\n";
    recorder    r;
    char        error[512] = "";

    CHECK(read_text("brackets.class.st", source, &r, error, sizeof error));
    if (error[0])
        printf("  error: %s\n", error);
    /*  Nine methods, and the last one proves nothing was swallowed.  */
    CHECK_EQ_INT((int) r.method_count, 9);
    if (r.method_count == 9)
        CHECK_EQ_STR(r.methods[8].class_name, "B");
}

/*
 *  An extension defines no class and names one defined elsewhere, which is
 *  what a package needs and what the chunk format cannot say.
 */
static void
test_an_extension_defines_no_class(void)
{
    static const char *const source =
    "Extension { #name : 'Object' }\n"
    "\n"
    "{ #category : '*Probe-Core' }\n"
    "Object >> probeMe [ ^self ]\n"
    "\n"
    "{ #category : '*Probe-Core' }\n"
    "Object class >> probeClass [ ^self ]\n";
    recorder    r;
    char        error[512] = "";

    CHECK(read_text("ext.extension.st", source, &r, error, sizeof error));
    CHECK_EQ_INT((int) r.class_count, 0);
    CHECK_EQ_INT((int) r.method_count, 2);
    if (r.method_count == 2) {
        CHECK_EQ_STR(r.methods[0].class_name, "Object");
        CHECK_EQ_INT(r.methods[1].class_side, 1);
        /*  The leading * is kept: it is what says the method is on loan.  */
        CHECK_EQ_STR(r.methods[0].category, "*Probe-Core");
    }
}

/*
 *  Shapes this object memory cannot build are named rather than mis-built.
 *  Loading an immediate class as an ordinary one would succeed and then
 *  behave differently, which is worse than refusing.
 */
static void
test_shapes_we_cannot_build_are_reported(void)
{
    recorder    r;
    char        error[512] = "";

    CHECK(read_text("imm.class.st",
        "Class { #name : 'I', #superclass : 'Object', #type : 'immediate' }\n",
        &r, error, sizeof error));
    CHECK_EQ_INT((int) r.class_count, 1);
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported, "an immediate class");

    /*
     *  Weak is built now rather than refused: indexed, with the collector
     *  not following the indexed part.
     */
    CHECK(read_text("weak.class.st",
        "Class { #name : 'W', #superclass : 'Object', #type : 'weak' }\n",
        &r, error, sizeof error));
    if (r.class_count) {
        CHECK_EQ_STR(r.classes[0].unsupported, "");
        CHECK_EQ_INT(r.classes[0].indexable, 1);
        CHECK_EQ_INT(r.classes[0].weak, 1);
    }

    /*
     *  An ephemeron still is not.  It is not a weak object with another
     *  name: its key is weak while its value stays strong for as long as
     *  the key lives, and deciding that needs the marker to run to a fixed
     *  point rather than once -- a different collector, not a different
     *  flag.
     */
    CHECK(read_text("eph.class.st",
        "Class { #name : 'E', #superclass : 'Object', #type : 'ephemeron' }\n",
        &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported, "ephemeron");

    /*  The shapes we CAN build map onto the Blue Book subclass forms.  */
    CHECK(read_text("var.class.st",
        "Class { #name : 'V', #superclass : 'Object', #type : 'variable' }\n",
        &r, error, sizeof error));
    if (r.class_count) {
        CHECK_EQ_INT(r.classes[0].indexable, 1);
        CHECK_EQ_INT(r.classes[0].bytes, 0);
        CHECK_EQ_STR(r.classes[0].unsupported, "");
    }
    CHECK(read_text("bytes.class.st",
        "Class { #name : 'Y', #superclass : 'Object', #type : 'bytes' }\n",
        &r, error, sizeof error));
    if (r.class_count) {
        CHECK_EQ_INT(r.classes[0].indexable, 1);
        CHECK_EQ_INT(r.classes[0].bytes, 1);
    }

    /*
     *  #slots is Tonel v3's spelling of #instVars, and for a PLAIN slot the
     *  two say the same thing.
     */
    CHECK(read_text("slots.class.st",
        "Class { #name : 'S', #superclass : 'Object', "
        "#slots : [ 'a', 'b' ] }\n", &r, error, sizeof error));
    if (r.class_count) {
        CHECK_EQ_STR(r.classes[0].ivars, "a b");
        CHECK_EQ_STR(r.classes[0].unsupported, "");
    }

    /*
     *  A slot with a KIND is a metamodel feature this system does not have,
     *  where reading and writing the variable go through the slot object.
     *  It is refused by the name of the kind -- and it is worth reading the
     *  "=>" at all, because a header the reader cannot parse says nothing
     *  useful about why it was rejected.
     */
    CHECK(read_text("kinded.class.st",
        "Class { #name : 'K', #superclass : 'Object', "
        "#slots : [ 'a', #b => WeakSlot ] }\n", &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported, "a slot of kind WeakSlot");

    /*
     *  An immediate has no object header: the value IS the pointer.  There
     *  is one tag bit here and SmallInteger has it, so a new immediate
     *  class cannot be made -- but the two Pharo declares immediate are
     *  already immediate here by other means, so its own declarations load.
     */
    CHECK(read_text("imm-small.class.st",
        "Class { #name : 'SmallInteger', #superclass : 'Integer', "
        "#type : 'immediate' }\n", &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported, "");
    CHECK(read_text("imm-char.class.st",
        "Class { #name : 'Character', #superclass : 'Magnitude', "
        "#type : 'immediate' }\n", &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported, "");
    CHECK(read_text("imm-other.class.st",
        "Class { #name : 'Mine', #superclass : 'Object', "
        "#type : 'immediate' }\n", &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported, "an immediate class");

    /*
     *  A trait arrives as a definition like any other, flagged -- it has no
     *  superclass and defines no instances, and its methods follow it as
     *  ordinary method events naming it.  What the sink does with that is
     *  the sink's business; the reader's job is to say so.
     */
    CHECK(read_text("t.trait.st",
        "Trait { #name : 'TFoo', #category : 'P' }\n"
        "TFoo >> hello [ ^1 ]\n"
        "TFoo class >> greeting [ ^2 ]\n", &r, error, sizeof error));
    CHECK_EQ_INT((int) r.class_count, 1);
    if (r.class_count) {
        CHECK_EQ_INT(r.classes[0].is_trait, 1);
        CHECK_EQ_STR(r.classes[0].name, "TFoo");
        CHECK_EQ_STR(r.classes[0].unsupported, "");
    }
    CHECK_EQ_INT((int) r.method_count, 2);
    if (r.method_count == 2) {
        CHECK_EQ_STR(r.methods[0].class_name, "TFoo");
        CHECK_EQ_INT(r.methods[0].class_side, 0);
        CHECK_EQ_INT(r.methods[1].class_side, 1);
    }

    /*
     *  A trait with state would have to add a field to every class that
     *  takes it, so it is refused rather than loaded without its variable.
     */
    CHECK(read_text("tstate.trait.st",
        "Trait { #name : 'TState', #instVars : [ 'count' ] }\n",
        &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported,
                     "a trait with instance variables");

    /*
     *  #classTraits is written mechanically beside #traits and says nothing
     *  the other does not, because a trait here carries its class-side
     *  methods with it.  The companion form is accepted and dropped...
     */
    CHECK(read_text("uses.class.st",
        "Class { #name : 'U', #superclass : 'Object', "
        "#traits : 'TA + TB', #classTraits : 'TA classTrait + TB classTrait' }\n",
        &r, error, sizeof error));
    if (r.class_count) {
        CHECK_EQ_STR(r.classes[0].traits, "TA + TB");
        CHECK_EQ_STR(r.classes[0].unsupported, "");
    }
    /*  ...and anything else is a statement that would be silently lost.  */
    CHECK(read_text("own.class.st",
        "Class { #name : 'V', #superclass : 'Object', "
        "#traits : 'TA', #classTraits : 'TB classTrait' }\n",
        &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].unsupported, "a #classTraits of its own");
}

/*
 *  Tonel v1 spells names as symbols and v3 as strings, and v3 splits the
 *  category into a package and a tag.  Both have to read.
 */
static void
test_both_tonel_spellings(void)
{
    recorder    r;
    char        error[512] = "";

    CHECK(read_text("v1.class.st",
        "Class { #name : #Old, #superclass : #Object, "
        "#instVars : [ #a, #b ], #category : #'Some-Where' }\n",
        &r, error, sizeof error));
    CHECK_EQ_INT((int) r.class_count, 1);
    if (r.class_count) {
        CHECK_EQ_STR(r.classes[0].name, "Old");
        CHECK_EQ_STR(r.classes[0].superclass, "Object");
        CHECK_EQ_STR(r.classes[0].ivars, "a b");
        CHECK_EQ_STR(r.classes[0].category, "Some-Where");
    }

    /*  No #category, only #package: the package answers for it.  */
    CHECK(read_text("v3.class.st",
        "Class { #name : 'New', #superclass : 'Object', "
        "#package : 'Pack-Age', #tag : 'Tag' }\n",
        &r, error, sizeof error));
    if (r.class_count)
        CHECK_EQ_STR(r.classes[0].category, "Pack-Age");
}

static void
test_a_package_file_defines_nothing(void)
{
    recorder    r;
    char        error[512] = "";

    CHECK(read_text("package.st", "Package { #name : 'Probe-Core' }\n",
                    &r, error, sizeof error));
    CHECK_EQ_INT((int) r.class_count, 0);
    CHECK_EQ_INT((int) r.method_count, 0);
}

static void
test_the_format_is_chosen_by_suffix(void)
{
    CHECK_EQ_STR(SRC_format_of("a/b/Foo.class.st"), "tonel");
    CHECK_EQ_STR(SRC_format_of("a/b/Foo.extension.st"), "tonel");
    CHECK_EQ_STR(SRC_format_of("a/b/TFoo.trait.st"), "tonel");
    CHECK_EQ_STR(SRC_format_of("a/b/package.st"), "tonel");
    CHECK_EQ_STR(SRC_format_of("a/b/Foo.stClass"), "chunk");
    CHECK_EQ_STR(SRC_format_of("kernel/Kernel.st"), "chunk");
}

/*
 *  Profiles: which files make an image.
 *
 *  Run from the top of the tree, so the profiles and the library are where
 *  the repository puts them; skipped rather than failed otherwise.
 */
static void
test_profiles(void)
{
    st_names    files;
    char        error[512] = "";
    int        *dialects = NULL;
    unsigned    bluebook_count;
    unsigned    i;
    int         saw_probe = 0;
    int         saw_sources = 0;

    if (!PROFILE_expand("profiles/bluebook.profile", &files, NULL, error,
                        sizeof error)) {
        printf("  skipped profiles: %s\n", error);
        return;
    }
    /*  The 1983 manifest, entry for entry.  */
    CHECK(files.count > 200);
    bluebook_count = files.count;
    for (i = 0; i < files.count; ++i) {
        if (strncmp(files.items[i], "sources/", 8) == 0
         || strstr(files.items[i], "/sources/"))
            saw_sources = 1;
    }
    CHECK(saw_sources);
    SRC_names_free(&files);

    /*  st2026 requires bluebook and adds the lib package on top.  */
    CHECK(PROFILE_expand("profiles/st2026.profile", &files, &dialects, error,
                         sizeof error));
    CHECK(files.count > bluebook_count);
    for (i = 0; i < files.count; ++i) {
        if (strstr(files.items[i], "lib/Probe/"))
            saw_probe = 1;
    }
    CHECK(saw_probe);
    /*
     *  A required profile's files come first, which is what "requires"
     *  has to mean if a package is to extend what it sits on.
     */
    CHECK(strstr(files.items[0], "sources/") != NULL);
    /*
     *  And the dialect follows the profile that named the file: the 1983
     *  library required from bluebook.profile stays Blue Book, and lib/,
     *  which st2026.profile names itself, is closures.  One image, two
     *  languages, which is what on:do: needs -- it takes blocks that have
     *  to be real closures.
     */
    if (dialects) {
        for (i = 0; i < files.count; ++i) {
            if (strstr(files.items[i], "sources/"))
                CHECK_EQ_INT(dialects[i], ST_DIALECT_BLUE_BOOK);
            if (strstr(files.items[i], "lib/"))
                CHECK_EQ_INT(dialects[i], ST_DIALECT_CLOSURES);
        }
    }
    free(dialects);
    /*  And a directory is taken in sorted order, so a build repeats.  */
    for (i = 1; i < files.count; ++i) {
        if (strstr(files.items[i - 1], "lib/Probe/")
         && strstr(files.items[i], "lib/Probe/"))
            CHECK(strcmp(files.items[i - 1], files.items[i]) < 0);
    }
    SRC_names_free(&files);
}

int
main(void)
{
    ST_TEST_BEGIN("source formats");

    test_the_format_is_chosen_by_suffix();
    test_tonel_reads_a_class();
    test_the_two_formats_agree();
    test_brackets_that_are_not_brackets();
    test_an_extension_defines_no_class();
    test_shapes_we_cannot_build_are_reported();
    test_both_tonel_spellings();
    test_a_package_file_defines_nothing();
    test_profiles();

    return ST_TEST_END();
}

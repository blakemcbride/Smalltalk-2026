/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  What this VM does with a primitive number, and the report built on it.
 *
 *  The port's central question -- will a body of Smalltalk source run here --
 *  is unbounded when asked as a question and finite when asked as a list:
 *  source names primitives by number, the numbers are enumerable, and what
 *  is left after the ones this VM answers is the work.  "st80 -primitives"
 *  produces that list; this checks it says true things.
 *
 *  Two of the checks below are worth more than the others.  ST_PRIM_TAG
 *  pins 198 and 199 as things that MUST keep failing: they are labels read
 *  by a walk up the sender chain, and a well-meaning future implementation
 *  of either would break ensure: and on:do: in a way no test of the
 *  exception system would obviously point at.  And ST_PRIM_ACCEPTED marks
 *  the five that succeed and do nothing -- which is not the same as
 *  implemented, because the method's Smalltalk fallback never runs.
 */

#include "st_test.h"

#include "prim.h"
#include "survey.h"
#include "compiler.h"

#include <stdio.h>
#include <string.h>

/*  ----------  The table  ----------  */

static void
test_the_four_answers(void)
{
    const char *name;

    /*  Implemented, and it can say what it is.  */
    name = NULL;
    CHECK_EQ_INT(ST_primitive_status_of(1, &name), ST_PRIM_PRESENT);
    CHECK(name != NULL && strstr(name, "SmallInteger") != NULL);

    name = NULL;
    CHECK_EQ_INT(ST_primitive_status_of(60, &name), ST_PRIM_PRESENT);
    /*  This system's own: the database and the network, one number each.  */
    CHECK_EQ_INT(ST_primitive_status_of(129, NULL), ST_PRIM_PRESENT);
    CHECK_EQ_INT(ST_primitive_status_of(208, NULL), ST_PRIM_PRESENT);
    CHECK(name != NULL);

    /*
     *  Succeeds and does nothing.  Distinguished from implemented because
     *  the image's fallback code never runs: a method relying on the effect
     *  fails silently rather than loudly.
     */
    CHECK_EQ_INT(ST_primitive_status_of(89, NULL), ST_PRIM_ACCEPTED);
    CHECK_EQ_INT(ST_primitive_status_of(91, NULL), ST_PRIM_ACCEPTED);
    CHECK_EQ_INT(ST_primitive_status_of(116, NULL), ST_PRIM_ACCEPTED);

    /*
     *  Must fail.  198 and 199 are marks a sender-chain walk reads --
     *  ContextPart>>findNextUnwindUpTo: and findNextHandlerContext look for
     *  exactly these numbers.  If either ever starts succeeding, ensure:
     *  and on:do: stop working and nothing here would say why.
     */
    CHECK_EQ_INT(ST_primitive_status_of(198, NULL), ST_PRIM_TAG);
    CHECK_EQ_INT(ST_primitive_status_of(199, NULL), ST_PRIM_TAG);

    /*  Absent, and honest about it: no name to offer.  */
    name = (const char *) "not cleared";
    CHECK_EQ_INT(ST_primitive_status_of(21, &name), ST_PRIM_ABSENT);
    CHECK(name == NULL);
    CHECK_EQ_INT(ST_primitive_status_of(0, NULL), ST_PRIM_ABSENT);
    CHECK_EQ_INT(ST_primitive_status_of(134, NULL), ST_PRIM_ABSENT);

    /*
     *  254 stood here as the absent one until it became the host's line
     *  ending, which is what a file out is written in -- lib/Files-Fixes
     *  asks for it by number and gets a line feed or a carriage return and
     *  line feed depending on the machine.  A silent absence would put the
     *  Alto's carriage returns back in every filed-out class, so the number
     *  is checked present and named rather than left to the floor count.
     */
    name = NULL;
    CHECK_EQ_INT(ST_primitive_status_of(254, &name), ST_PRIM_PRESENT);
    CHECK(name != NULL);

    /*
     *  223 is String>>hash over every byte, this system's own number.
     *  Silently absent it would fail nothing: the method's Smalltalk body
     *  computes the same function, so every test would pass and every
     *  Dictionary lookup would hash its key with a loop in bytecodes.  A
     *  slowdown with no failure is the absence a floor count is blind to,
     *  so the number is checked present and named.
     */
    name = NULL;
    CHECK_EQ_INT(ST_primitive_status_of(223, &name), ST_PRIM_PRESENT);
    CHECK(name != NULL && strstr(name, "hash") != NULL);

    /*
     *  188 runs a CompiledMethod that is installed nowhere, and 167 yields
     *  without forking a helper.  Both have Smalltalk fallbacks that work
     *  on one worker and race on many -- the compiler's #DoIt slot, the
     *  helper's shared block context -- so, as with 223, a silent absence
     *  would pass every single-threaded test and fail only where it costs
     *  the most to find.
     */
    name = NULL;
    CHECK_EQ_INT(ST_primitive_status_of(188, &name), ST_PRIM_PRESENT);
    CHECK(name != NULL && strstr(name, "executeMethod") != NULL);
    name = NULL;
    CHECK_EQ_INT(ST_primitive_status_of(167, &name), ST_PRIM_PRESENT);
    CHECK(name != NULL && strstr(name, "yield") != NULL);

    /*
     *  Every number the table claims is one the compiler will accept, so a
     *  typo'd entry cannot sit there describing a primitive no source could
     *  ever ask for.
     */
    {
        unsigned    i;
        unsigned    claimed = 0;

        for (i = 0; i <= 300; ++i) {
            if (ST_primitive_status_of(i, NULL) == ST_PRIM_ABSENT)
                continue;
            ++claimed;
            CHECK(i >= 1 && i <= 255);
        }
        /*  A floor, not an exact count: adding primitives is the point.  */
        CHECK(claimed >= 90);
    }
}

/*  ----------  The report  ----------  */

static int
survey_text(st_survey *s, const char *name, const char *text)
{
    char    path[512];
    FILE   *f;

    snprintf(path, sizeof path, "/tmp/st2026-prim-%s", name);
    f = fopen(path, "wb");
    if (!f) {
        printf("  cannot write %s\n", path);
        return 0;
    }
    fwrite(text, 1, strlen(text), f);
    fclose(f);

    SURVEY_init(s);
    SURVEY_file(s, path);
    remove(path);
    return 1;
}

static const st_survey_primitive *
row(const st_survey *s, unsigned number, const char *name)
{
    unsigned    i;

    for (i = 0; i < s->primitive_count; ++i) {
        if (s->primitives[i].number == number
         && strcmp(s->primitives[i].name, name) == 0)
            return &s->primitives[i];
    }
    return NULL;
}

static void
test_the_report_counts_what_source_asks_for(void)
{
    st_survey                   s;
    const st_survey_primitive  *r;

    if (!survey_text(&s, "numbered.class.st",
        "Class { #name : 'P', #superclass : 'Object' }\n"
        "P >> one [ <primitive: 60> ^nil ]\n"
        "P >> two [ <primitive: 60> ^nil ]\n"
        "P >> three [ <primitive: 21> ^nil ]\n"
        "P >> none [ ^7 ]\n"))
        return;

    CHECK_EQ_INT((int) s.methods, 4);
    CHECK_EQ_INT((int) s.failed, 0);
    /*  A method with no primitive contributes no row.  */
    CHECK_EQ_INT((int) s.primitive_count, 2);

    r = row(&s, 60, "");
    CHECK(r != NULL);
    if (r) {
        CHECK_EQ_INT((int) r->methods, 2);
        /*  The example is the FIRST method that asked, by its selector --
            not its first source line, which in the 1983 files carries the
            whole method comment.  */
        CHECK_EQ_STR(r->example, "P>>one");
    }
    r = row(&s, 21, "");
    CHECK(r != NULL);
    if (r)
        CHECK_EQ_STR(r->example, "P>>three");
}

static void
test_a_named_primitive_is_not_just_117(void)
{
    st_survey                   s;
    const st_survey_primitive  *r;

    /*
     *  Primitive 117 is a doorway, not a primitive.  Pharo's kernel reaches
     *  a plugin through it hundreds of times, and a report that folded all
     *  of those into one row saying "117" would answer nothing at all.
     */
    if (!survey_text(&s, "named.class.st",
        "Class { #name : 'N', #superclass : 'Object' }\n"
        "N >> a [ <primitive: 'primFoo' module: 'FooPlugin'> ^nil ]\n"
        "N >> b [ <primitive: 'primBar' module: 'FooPlugin'> ^nil ]\n"
        "N >> c [ <primitive: 'primFoo' module: 'FooPlugin'> ^nil ]\n"))
        return;

    CHECK_EQ_INT((int) s.failed, 0);
    CHECK_EQ_INT((int) s.primitive_count, 2);

    r = row(&s, 117, "'primFoo' module: 'FooPlugin'");
    CHECK(r != NULL);
    if (r) {
        CHECK_EQ_INT((int) r->methods, 2);
        CHECK_EQ_STR(r->example, "N>>a");
    }
    r = row(&s, 117, "'primBar' module: 'FooPlugin'");
    CHECK(r != NULL);
    if (r)
        CHECK_EQ_INT((int) r->methods, 1);
}

static void
test_class_side_methods_are_labelled(void)
{
    st_survey                   s;
    const st_survey_primitive  *r;

    if (!survey_text(&s, "side.class.st",
        "Class { #name : 'S', #superclass : 'Object' }\n"
        "S class >> make [ <primitive: 70> ^nil ]\n"))
        return;

    r = row(&s, 70, "");
    CHECK(r != NULL);
    if (r)
        CHECK_EQ_STR(r->example, "S class>>make");
}

/*
 *  The 1983 library, which is the one corpus whose answer is known: it uses
 *  no 198 and no 199, because it has no exception system -- that fact is
 *  what made repurposing those two numbers safe in the first place.
 */
static void
test_the_1983_library(void)
{
    st_survey   s;
    FILE       *manifest = fopen("sources/MANIFEST", "r");
    char        line[512];
    unsigned    read = 0;

    if (!manifest) {
        printf("  skipped: run from the top of the tree\n");
        return;
    }
    SURVEY_init(&s);
    while (fgets(line, sizeof line, manifest)) {
        size_t  n = strlen(line);

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n) {
            SURVEY_file(&s, line);
            ++read;
        }
    }
    fclose(manifest);

    CHECK(read > 200);
    CHECK_EQ_INT((int) s.unreadable, 0);
    CHECK(s.primitive_count > 100);
    CHECK(row(&s, 198, "") == NULL);
    CHECK(row(&s, 199, "") == NULL);
    /*  And it does ask for the ones the trace oracle depends on.  */
    CHECK(row(&s, 1, "") != NULL);
    CHECK(row(&s, 60, "") != NULL);
    CHECK(row(&s, 96, "") != NULL);   /*  BitBlt copyBits  */
    /*  Nothing overflowed the table, so the totals mean what they say.  */
    CHECK_EQ_INT((int) s.primitives_overflowed, 0);
}

int
main(void)
{
    ST_TEST_BEGIN("primitives");

    test_the_four_answers();
    test_the_report_counts_what_source_asks_for();
    test_a_named_primitive_is_not_just_117();
    test_class_side_methods_are_labelled();
    test_the_1983_library();

    return ST_TEST_END();
}

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The compiler against the whole 1983 class library.
 *
 *  Every method of all 226 classes in sources/ must compile.  That is a
 *  sharper gate than it sounds: the library is 4000 methods of code written
 *  by people who had the real compiler in front of them, so it uses the
 *  grammar's corners rather than its middle -- and it found two.  The bar
 *  after a block's arguments turns out to be optional when the block has no
 *  body, which the Blue Book grammar does not say and Xerox's own sources
 *  rely on; and a method category is closed with a comment rather than an
 *  empty chunk, which the chunk reader had to learn to treat alike.
 *
 *  Only grammar is checked here.  Literals are fabricated and every
 *  identifier resolves, so nothing depends on an image existing.  Whether
 *  these methods RUN is Phase 8's question; whether they parse is this one's,
 *  and the two are worth failing separately.
 */

#include "st_test.h"
#include "survey.h"

#include <stdio.h>
#include <string.h>

#define MANIFEST    "sources/MANIFEST"

int
main(void)
{
    st_survey   survey;
    FILE       *manifest;
    char        line[512];

    ST_TEST_BEGIN("1983 class library");

    manifest = fopen(MANIFEST, "r");
    if (!manifest) {
        printf("skipped: %s not found (run from the top of the tree)\n",
               MANIFEST);
        return ST_TEST_END();
    }

    SURVEY_init(&survey);
    while (fgets(line, sizeof line, manifest)) {
        size_t  n = strlen(line);

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n)
            SURVEY_file(&survey, line);
    }
    fclose(manifest);

    printf("  ");
    SURVEY_report(&survey, stdout);

    /*
     *  226 vendored classes and 4517 methods, plus kernel/Bootstrap.st --
     *  our own additions, filed in last and listed last in the manifest.
     *
     *  The method count is not a guess: the upstream tree also stores each
     *  method as its own .st file, and there are exactly 4517 of those, so
     *  this is an independent check that the chunk reader is finding every
     *  method and not quietly skipping any.  Undercounting is the failure
     *  mode that hides -- an early-terminated method category simply
     *  compiles fewer methods and still passes.
     */
    CHECK_EQ_INT(survey.files, 227);
    CHECK_EQ_INT(survey.unreadable, 0);
    CHECK_EQ_INT(survey.methods, 4520);
    CHECK_EQ_INT(survey.failed, 0);

    return ST_TEST_END();
}

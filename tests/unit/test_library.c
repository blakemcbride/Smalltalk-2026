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

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MANIFEST    "sources/MANIFEST"

/*
 *  How many underscores in the file touch a word character on either side,
 *  outside comments, strings and character literals; and where the first
 *  one is.
 *
 *  In sources/ an underscore is always 1983's assignment arrow, and the
 *  closure dialect -- which every recompile goes through -- reads one that
 *  touches a letter as part of an identifier: `runs_ runs' as the name
 *  runs_, which cannot be recompiled, and `ascii _maxAscii' as a unary
 *  send of #_maxAscii, which can, and then means something else.  The
 *  fifty-seven of the first kind were respaced before the system shipped;
 *  the one of the second kind was found by a whole-image sweep of every
 *  method's bytecodes against its recompilation (Bugs6 COMP-8), and the
 *  compiler refuses such a send now.  This keeps the count at zero, so the
 *  next one is caught when it is written rather than when it is accepted.
 */
static long
glued_underscores(const char *path, char *where, size_t where_size)
{
    FILE   *f = fopen(path, "rb");
    long    glued = 0;
    int     in_comment = 0;
    int     in_string = 0;
    int     prev = '\n';
    int     c;
    unsigned line = 1;

    if (!f)
        return 0;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n')
            ++line;
        if (in_comment) {
            if (c == '"')
                in_comment = 0;
        }  else if (in_string) {
            if (c == '\'')
                in_string = 0;
        }  else if (c == '"') {
            in_comment = 1;
        }  else if (c == '\'') {
            in_string = 1;
        }  else if (c == '$') {
            c = fgetc(f);           /*  the character itself, whatever it is  */
            if (c == '\n')
                ++line;
        }  else if (c == '_') {
            int next = fgetc(f);
            int word_before = isalnum(prev) || prev == '_';
            int word_after  = next != EOF && (isalnum(next) || next == '_');

            if (word_before || word_after) {
                if (glued == 0)
                    snprintf(where, where_size, "%s:%u", path, line);
                ++glued;
            }
            if (next != EOF)
                ungetc(next, f);
        }
        prev = c;
    }
    fclose(f);
    return glued;
}

int
main(void)
{
    st_survey   survey;
    FILE       *manifest;
    char        line[512];
    long        glued = 0;
    char        where[600] = "";

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
        if (n) {
            SURVEY_file(&survey, line);
            glued += glued_underscores(line, where, sizeof where);
        }
    }
    fclose(manifest);

    printf("  ");
    SURVEY_report(&survey, stdout);
    SURVEY_free(&survey);       /*  the name tables; the counts stay  */

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
    CHECK_EQ_INT(survey.methods, 4521);
    CHECK_EQ_INT(survey.failed, 0);

    /*  No assignment arrow touches a name: see glued_underscores.  */
    if (glued)
        printf("  first glued underscore at %s\n", where);
    CHECK_EQ_INT(glued, 0);

    return ST_TEST_END();
}

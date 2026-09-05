/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  One process's fault stays one process's fault.
 *
 *  Bugs3 found nine ordinary lines that each stopped or hung the whole
 *  image under -serve: a perform: with the wrong number of arguments (B5),
 *  a perform:withArguments: with twelve (B1), a superclass cycle (B6), a
 *  receiver with no doesNotUnderstand: anywhere above it (B7), a method
 *  whose bytes had been rewritten (B11).  Each cleared the interpreter's
 *  running flag, or spun in C, and one worker leaving ends the pool.
 *
 *  None of that can be seen from inside test_image, which runs one green
 *  process on one thread with no pool to lose.  So this drives the real
 *  binary the way the audit did: an image whose startup evaluates each
 *  line of its argument and prints `line ==> answer' on stderr, served on
 *  two workers, given the faulting line and then `3 + 4'.  The check is
 *  the shape of the finding: the fault is reported as an error, and the
 *  expression after it still prints 7, because the pool is still there.
 *
 *  It also holds the two command-line findings of B58 -- a -startup that
 *  does not compile must not write an image, and an unhandled error in
 *  -eval must be an error exit -- and B19's promise that eight workers
 *  printing at once print whole lines.
 *
 *  The image is built once, into the test directory, from the binary the
 *  build just made.  Under OM=bb there is no image format to build, and
 *  the test skips as test_image does.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define IMAGE       "build/mt/tests/bugs3-serve.im"
#define BATCH       "build/mt/tests/bugs3-serve-batch.st"
#define STARTUP     "build/mt/tests/bugs3-serve-startup.st"
#define RACE        "build/mt/tests/bugs3-serve-race.st"
#define BADIMAGE    "build/mt/tests/bugs3-serve-bad.im"

/*
 *  The harness from the Bugs3 appendix, verbatim: each line of the first
 *  argument is evaluated and its answer, or the error it raised, is
 *  printed on stderr.
 */
static const char *startup_text =
    "[:strm | [strm atEnd] whileFalse: [[:line | line isEmpty ifTrue: [] "
    "ifFalse: [\n"
    "    (line , ' ==> ', ([(Compiler evaluate: line) printString]\n"
    "        on: Error do: [:e | 'ERR ', e class name, ': ', "
    "(e messageText ifNil: ['<nil>'])])) displayNl]]\n"
    "  value: (strm upTo: Character lf)]]\n"
    "    value: (ReadStream on: (Smalltalk arguments isEmpty ifTrue: [''] "
    "ifFalse: [Smalltalk arguments first]))\n";

static const char *st80;

static const char *
find_binary(void)
{
    if (access("build/mt/st80", X_OK) == 0)
        return "build/mt/st80";
    if (access("./st80", X_OK) == 0)
        return "./st80";
    return NULL;
}

static int
write_file(const char *path, const char *text)
{
    FILE   *f = fopen(path, "w");

    if (!f)
        return -1;
    fputs(text, f);
    return fclose(f);
}

/*
 *  Run a shell command, collecting everything it writes to either stream,
 *  and answer its exit status (or -1).  The output is capped; every
 *  answer this test looks for is near the end.
 */
static int
run(const char *command, char *out, size_t len)
{
    FILE   *p;
    size_t  got = 0;
    int     status;

    out[0] = '\0';
    p = popen(command, "r");
    if (!p)
        return -1;
    for (;;) {
        size_t  n = fread(out + got, 1, len - 1 - got, p);

        if (n == 0)
            break;
        got += n;
        if (got >= len - 1) {
            char    sink[4096];

            while (fread(sink, 1, sizeof sink, p) > 0)
                ;
            break;
        }
    }
    out[got] = '\0';
    status = pclose(p);
    if (status == -1)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

/*
 *  Serve the image on two workers with the batch as its argument.  The
 *  batch goes through a file rather than the command line, because the
 *  expressions are full of quotes.  Sixty seconds and then SIGKILL: a
 *  hang is one of the faults being checked for.
 */
static int
serve(const char *batch, unsigned workers, char *out, size_t len)
{
    char    command[1024];

    if (write_file(BATCH, batch) != 0)
        return -1;
    snprintf(command, sizeof command,
             "timeout -k 2 60 %s -serve %s -workers %u \"$(cat %s)\" 2>&1",
             st80, IMAGE, workers, BATCH);
    return run(command, out, len);
}

static void
expect(const char *out, const char *wanted, const char *what)
{
    ++st_test_checks;
    if (!strstr(out, wanted)) {
        ++st_test_failures;
        printf("  FAIL %s: expected to see \"%s\"\n", what, wanted);
        printf("       output ends:\n%s\n",
               strlen(out) > 1500 ? out + strlen(out) - 1500 : out);
    }
}

static void
expect_absent(const char *out, const char *unwanted, const char *what)
{
    ++st_test_checks;
    if (strstr(out, unwanted)) {
        ++st_test_failures;
        printf("  FAIL %s: did not expect to see \"%s\"\n", what, unwanted);
    }
}

/*
 *  The other expression in the batch still prints: the pool survived.
 */
static void
check_survives(const char *batch, const char *error_text, const char *what)
{
    static char out[65536];
    int         status = serve(batch, 2, out, sizeof out);

    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL %s: could not run the server\n", what);
        return;
    }
    expect(out, error_text, what);
    expect(out, "3 + 4 ==> 7", what);
    expect_absent(out, "Segmentation", what);
}

int
main(void)
{
    static char out[65536];
    char        command[2048];
    int         status;

    ST_TEST_BEGIN("serve-faults");

    st80 = find_binary();
    if (!st80) {
        printf("skipped: no st80 binary to drive\n");
        return ST_TEST_END();
    }
    if (write_file(STARTUP, startup_text) != 0) {
        printf("skipped: cannot write %s\n", STARTUP);
        return ST_TEST_END();
    }
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-startup \"$(cat %s)\" -o %s 2>&1", st80, STARTUP, IMAGE);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0) {
        ++st_test_failures;
        printf("  FAIL cannot build the probe image (exit %d):\n%s\n",
               status, out);
        return ST_TEST_END();
    }

    /*  B5: the wrong number of arguments through perform:.  */
    check_survives("3 perform: #+\n3 + 4\n",
                   "ERR Error: perform: + with 0 arguments; it takes 1",
                   "B5 perform: #+");
    check_survives("3 perform: #at:put: with: 1\n3 + 4\n",
                   "ERR Error: perform: at:put: with 1 arguments; it takes 2",
                   "B5 perform:with:");

    /*  B1: an Array that does not fit the frame, twelve and eighteen.  */
    check_survives("nil perform: #a1:a2:a3:a4:a5:a6:a7:a8:a9:a10:a11:a12: "
                   "withArguments: (Array new: 12)\n3 + 4\n",
                   "ERR Error: perform:withArguments: cannot spread 12 "
                   "arguments", "B1 twelve");
    check_survives("nil perform: #a1:a2:a3:a4:a5:a6:a7:a8:a9:a10:a11:a12:"
                   "a13:a14:a15:a16:a17:a18: withArguments: (Array new: 18)"
                   "\n3 + 4\n",
                   "ERR Error: perform:withArguments: cannot spread 18 "
                   "arguments", "B1 eighteen");

    /*  B6: a superclass cycle, refused; and one made by force, bounded.  */
    check_survives("Object subclass: #CA instanceVariableNames: '' "
                   "classVariableNames: '' poolDictionaries: '' "
                   "category: 'x'. Object subclass: #CB "
                   "instanceVariableNames: '' classVariableNames: '' "
                   "poolDictionaries: '' category: 'x'. CA superclass: CB. "
                   "CB superclass: CA. CA new zork\n"
                   "[:ca :cb | ca instVarAt: 1 put: cb. cb instVarAt: 1 "
                   "put: ca. ca new zork] value: (Smalltalk at: #CA) "
                   "value: (Smalltalk at: #CB)\n3 + 4\n",
                   "ERR MessageNotUnderstood: Message not understood: zork",
                   "B6 cycle");

    /*  B7: no doesNotUnderstand: anywhere above the receiver.  */
    check_survives("Behavior new new printString\n3 + 4\n",
                   "ERR MessageNotUnderstood: Message not understood: "
                   "printString", "B7 Behavior new new");
    check_survives("Object removeSelector: #doesNotUnderstand:. 3 zork\n"
                   "3 + 4\n",
                   "ERR MessageNotUnderstood: Message not understood: zork",
                   "B7 without Object>>doesNotUnderstand:");

    /*
     *  B7 with nothing left to send at all: neither doesNotUnderstand:
     *  nor cannotInterpret: anywhere in the image.  The interpreter then
     *  ends the PROCESS, and the check is that it is only the process: a
     *  heartbeat forked beside the faulting one keeps printing, the main
     *  line finishes, and the next expression answers.  The report names
     *  the fault and says the process is ended.
     */
    check_survives("[[true] whileTrue: [(Delay forMilliseconds: 50) wait."
                   " 'hb' displayNl]] fork."
                   " Object removeSelector: #doesNotUnderstand:."
                   " Object removeSelector: #cannotInterpret:."
                   " [3 zork] fork. (Delay forMilliseconds: 400) wait."
                   " 'main done'\n3 + 4\n",
                   "nothing in the image understands doesNotUnderstand: or "
                   "cannotInterpret: either; the process is ended",
                   "B7 no handler anywhere");
    {
        static char out[65536];

        serve("[[true] whileTrue: [(Delay forMilliseconds: 50) wait."
              " 'hb' displayNl]] fork."
              " Object removeSelector: #doesNotUnderstand:."
              " Object removeSelector: #cannotInterpret:."
              " [3 zork] fork. (Delay forMilliseconds: 400) wait."
              " 'main done'\n", 2, out, sizeof out);
        expect(out, "==> 'main done'", "B7 no handler: the main line");
        expect(out, "hb\nhb\n", "B7 no handler: the heartbeat");
    }

    /*  B11: rewritten bytecodes, and a context with a rewritten ip.  */
    check_survives("Object subclass: #CM instanceVariableNames: '' "
                   "classVariableNames: '' poolDictionaries: '' "
                   "category: 'x'. CM compile: 'foo ^3'. [:m | m initialPC "
                   "to: m size do: [:i | m at: i put: 255]] value: "
                   "(CM compiledMethodAt: #foo). CM new foo\n3 + 4\n",
                   "ERR CorruptMethod:", "B11 literal index");
    check_survives("thisContext sender sender instVarAt: 2 put: -1. 3\n"
                   "3 + 4\n",
                   "ERR CorruptMethod:", "B11 instruction pointer");

    /*  B15: the loop that used to reach the depth ceiling answers.  */
    check_survives("1 to: 200000 do: [:i | 1 + 1.0]\n3 + 4\n",
                   "1 to: 200000 do: [:i | 1 + 1.0] ==> 1", "B15");

    /*
     *  B19: eight workers printing 300 lines each, and every line whole.
     *  The program is the audit's, run from a file through Compiler
     *  evaluate:.
     */
    if (write_file(RACE,
            "| done |\n"
            "done := Semaphore new.\n"
            "1 to: 8 do: [:i | Processor forkParallel: [300 timesRepeat: "
            "[('LINE', i printString, ' ', (String new: 60 withAll: "
            "(Character value: 64 + i))) displayNl]. done signal]].\n"
            "8 timesRepeat: [done wait].\n"
            "^'finished'\n") == 0) {
        static char big[262144];
        char        batch[512];
        unsigned    whole = 0;
        const char *scan;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", RACE);
        status = serve(batch, 8, big, sizeof big);
        for (scan = big; (scan = strstr(scan, "LINE")) != NULL; ++scan) {
            unsigned    k;

            if (scan[4] < '1' || scan[4] > '8' || scan[5] != ' ')
                continue;
            for (k = 0; k < 60; ++k)
                if (scan[6 + k] != 'A' + (scan[4] - '1'))
                    break;
            if (k == 60 && scan[66] == '\n')
                ++whole;
        }
        ++st_test_checks;
        if (status < 0 || whole != 2400) {
            ++st_test_failures;
            printf("  FAIL B19: %u of 2400 lines came out whole "
                   "(exit %d)\n", whole, status);
        }
    }

    /*
     *  Bugs4 MEM-1: a fork-and-terminate storm on thirty-two workers.
     *
     *  Ordinary concurrent code -- fork a process, stop it, do it again --
     *  stopped the whole image in about four runs in five.  A process was
     *  taken from the MIDDLE of a ready list, which is the path used only
     *  while some other worker is detaching, and take_first_runnable
     *  unlinked it before it said whose hands it was in; the detacher
     *  looking in that gap answered "it was nowhere", Process>>terminate
     *  wrote nil over the suspendedContext of a process a third worker had
     *  already nominated, and that worker's switch was handed a nil to run.
     *
     *  Neither worker count either side of thirty-two shows it: one and
     *  eight never enter the middle path often enough, and fork with no
     *  terminate never names anything, so the walk is never taken.  Run
     *  three times, because eighty percent per run is not a gate.
     */
    {
        const char *storm =
            "| done | done := Semaphore new. "
            "1 to: 32 do: [:i | [1 to: 8000 do: [:j | | p | "
            "p := [[true] whileTrue: [Processor yield]] newProcess. "
            "p resume. p terminate]. done signal] fork]. "
            "1 to: 32 do: [:i | done wait]. 'storm ok'\n3 + 4\n";
        int         attempt;

        for (attempt = 0; attempt < 3; ++attempt) {
            status = serve(storm, 32, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL MEM-1: could not run the server\n");
                break;
            }
            expect(out, "'storm ok'", "MEM-1 fork/terminate storm");
            expect(out, "3 + 4 ==> 7", "MEM-1 the pool survived");
            expect_absent(out, "is not a context",
                          "MEM-1 nothing ran a nil context");
            expect_absent(out, "suspended context is not a context",
                          "MEM-1 nothing was dropped");
        }
    }

    /*  B58: a startup that does not compile writes no image, exit 1.  */
    unlink(BADIMAGE);
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-startup '3 +' -o %s 2>&1", st80, BADIMAGE);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status == 0 || access(BADIMAGE, F_OK) == 0) {
        ++st_test_failures;
        printf("  FAIL B58 -startup that does not compile: exit %d, "
               "image %s\n", status,
               access(BADIMAGE, F_OK) == 0 ? "written" : "not written");
        unlink(BADIMAGE);
    }
    expect(out, "cannot compile the startup", "B58 bad startup");

    /*  B58: -eval exits 1 after an unhandled error, 0 after a handled one.  */
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-eval '3 zork' 2>&1", st80);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 1) {
        ++st_test_failures;
        printf("  FAIL B58 -eval '3 zork' exited %d, want 1\n", status);
    }
    expect(out, "went unhandled", "B58 -eval unhandled");
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-eval '[3 zork] on: Error do: [:e | 5]' 2>&1", st80);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0) {
        ++st_test_failures;
        printf("  FAIL B58 -eval with a handled error exited %d, want 0\n",
               status);
    }
    expect(out, "\n5\n", "B58 -eval handled");

    unlink(IMAGE);
    unlink(BATCH);
    unlink(STARTUP);
    unlink(RACE);
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: the bootstrap targets the 64-bit object memory\n");
    return 0;
}

#endif

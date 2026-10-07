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

#define IMAGE       fixture("bugs3-serve.im")
#define BATCH       fixture("bugs3-serve-batch.st")
#define STARTUP     fixture("bugs3-serve-startup.st")
#define RACE        fixture("bugs3-serve-race.st")
#define BADIMAGE    fixture("bugs3-serve-bad.im")
#define SHARED      fixture("bugs5-serve-shared.st")
#define FORWARD     fixture("bugs5-serve-forward.st")
#define TERMLOCK    fixture("bugs5-serve-terminate.st")
#define RESUMED     fixture("bugs5-serve-resumed.st")
#define MONITOR     fixture("bugs5-serve-monitor.st")
#define WORDS       fixture("bugs5-serve-words.st")
#define SOURCES     fixture("bugs5-serve-sources.st")

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

static const char *st2026;

/*
 *  A fixture's path in this build's scratch directory (see
 *  st_test_dir), made once per name and kept.
 */
static const char *
fixture(const char *name)
{
    static struct { const char *name; char *path; } made[32];
    size_t  i;
    size_t  n;

    for (i = 0; i < sizeof made / sizeof made[0] && made[i].name; ++i)
        if (strcmp(made[i].name, name) == 0)
            return made[i].path;
    if (i == sizeof made / sizeof made[0])
        return name;
    n = strlen(st_test_dir()) + strlen(name) + 2;
    made[i].path = (char *) malloc(n);
    if (!made[i].path)
        return name;
    snprintf(made[i].path, n, "%s/%s", st_test_dir(), name);
    made[i].name = name;
    return made[i].path;
}

static const char *
find_binary(void)
{
    if (access(st_test_binary(), X_OK) == 0)
        return st_test_binary();
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
             st2026, IMAGE, workers, BATCH);
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

    st2026 = find_binary();
    if (!st2026) {
        /*  Under make the binary is a prerequisite, so its absence fails.  */
        if (getenv("ST2026_BIN")) {
            ++st_test_checks;
            ++st_test_failures;
            printf("  FAIL no st2026 binary at %s\n", st_test_binary());
        } else
            printf("skipped: no st2026 binary to drive\n");
        return ST_TEST_END();
    }
    if (write_file(STARTUP, startup_text) != 0) {
        /*  A failure, not a skip: "ok: 0 checks" passed for nothing.  */
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL cannot write %s\n", STARTUP);
        return ST_TEST_END();
    }
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-startup \"$(cat %s)\" -o %s 2>&1", st2026, STARTUP, IMAGE);
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

    /*
     *  Bugs5 OM-1: eight workers storing into one shared slot.
     *
     *  OM_store_pointer read the old value, stored the new one and released
     *  the old one as three steps.  Two workers that read the same old
     *  value both released it, so each of the four strings stored round
     *  robin into one Array slot lost a count whenever the race was won,
     *  reached zero while `keep' still held it, and was freed; the churn
     *  after the loop then handed its table entry to something else.  Two
     *  runs in three printed a context where a string had been.  The check
     *  is that all four are still twenty-character strings.  Three runs,
     *  as for MEM-1, because two in three per run is not a gate.
     */
    if (write_file(SHARED,
            "| keep slot done bad |\n"
            "keep := (1 to: 4) collect: [:i | String new: 20 withAll: "
            "(Character value: 96 + i)].\n"
            "slot := Array new: 1. done := Semaphore new.\n"
            "1 to: 8 do: [:w | Processor forkParallel: [1 to: 300000 do: "
            "[:i | slot at: 1 put: (keep at: i \\\\ 4 + 1)]. done signal]].\n"
            "8 timesRepeat: [done wait].\n"
            "1 to: 20000 do: [:i | Array with: i with: i printString].\n"
            "bad := keep reject: [:s | s isString and: [s size = 20]].\n"
            "^bad isEmpty ifTrue: ['shared slot ok'] ifFalse: "
            "[bad printString]\n") == 0) {
        char        batch[512];
        int         attempt;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", SHARED);
        for (attempt = 0; attempt < 3; ++attempt) {
            status = serve(batch, 8, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL OM-1: could not run the server\n");
                break;
            }
            expect(out, "==> 'shared slot ok'", "OM-1 shared slot");
            expect(out, "3 + 4 ==> 7", "OM-1 the pool survived");
        }
    }

    /*
     *  Bugs5 OM-3: eight workers forwarding at once.
     *
     *  OM_forward_identity handed its pair to the safepoint through two
     *  statics, so concurrent becomeForward:s overwrote each other's -- one
     *  safepoint did another's forward, or one worker's `from' with
     *  another's `to', and the clear afterwards turned the loser into a
     *  sweep that found nothing.  Both answered success.  The audit's probe
     *  lost 127 of 240 forwards; the check is that the holder of every `a'
     *  now holds its own `b'.
     */
    if (write_file(FORWARD,
            "| lost done mtx |\n"
            "lost := 0. done := Semaphore new. "
            "mtx := Semaphore forMutualExclusion.\n"
            "1 to: 8 do: [:k | Processor forkParallel: [1 to: 30 do: "
            "[:i | | a b holder |\n"
            "  a := Array with: k with: i. b := Array with: #to with: k "
            "with: i.\n"
            "  holder := Array with: a. a becomeForward: b.\n"
            "  (holder at: 1) == b ifFalse: [mtx critical: "
            "[lost := lost + 1]]].\n"
            "  done signal]].\n"
            "8 timesRepeat: [done wait].\n"
            "^'lost forwards: ', lost printString\n") == 0) {
        char        batch[512];

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", FORWARD);
        status = serve(batch, 8, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-3: could not run the server\n");
        } else {
            expect(out, "==> 'lost forwards: 0'", "OM-3 concurrent forwards");
            expect(out, "3 + 4 ==> 7", "OM-3 the pool survived");
        }
    }

    /*
     *  Bugs5 OM-5: terminate at the wrong instant leaves a Mutex locked.
     *
     *  critical: was `self acquire. ^aBlock ensure: [self release]', so a
     *  process terminated after its wait returned but before ensure: was
     *  entered kept the lock with nothing to give it back; and terminate
     *  skipped an unwind block already running, because runUnwindBlock
     *  disarms the frame first.  Forty processes looping on one Mutex,
     *  one terminated every 5 ms, then one more critical: that must run
     *  -- the old code locked it two runs in three on eight workers and
     *  every run on one.  The loops yield because one worker does not
     *  time-slice equal priorities, and a loop that never blocks would
     *  starve the process doing the terminating.  The second line is the
     *  unwind block: terminated while waiting inside it, the process must
     *  still finish it when the gate opens, and the outer ensure: after.
     *  It waits until the process is in the block: a yield does not get it
     *  there on eight workers, and terminated before it, the block runs on
     *  the terminator -- which then waits on the gate it was to open.
     */
    if (write_file(TERMLOCK,
            "| lock procs x |\n"
            "lock := Mutex new. x := 0.\n"
            "procs := (1 to: 40) collect: [:i | [[true] whileTrue: "
            "[lock critical: [x := x + 1]. Processor yield]] fork].\n"
            "1 to: 40 do: [:i | (Delay forMilliseconds: 5) wait. "
            "(procs at: i) terminate].\n"
            "[lock critical: [x := -1]] fork.\n"
            "(Delay forMilliseconds: 1000) wait.\n"
            "^x = -1 ifTrue: ['terminate lock ok'] ifFalse: "
            "['LOCKED ', lock printString]\n") == 0) {
        char        batch[1024];
        unsigned    workers[] = { 8, 1 };
        int         run_index;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n"
                 "| gate log p | gate := Semaphore new. "
                 "log := OrderedCollection new. "
                 "p := [[[log add: 1] ensure: [log add: 2. gate wait. "
                 "log add: 3]] ensure: [log add: 4]. log add: 5] fork. "
                 "[log size < 2] whileTrue: [Processor yield]. "
                 "(Delay forMilliseconds: 20) wait. "
                 "p terminate. gate signal. "
                 "(Delay forMilliseconds: 100) wait. log asArray\n", TERMLOCK);
        for (run_index = 0; run_index < 4; ++run_index) {
            status = serve(batch, workers[run_index & 1], out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL OM-5: could not run the server\n");
                break;
            }
            expect(out, "==> 'terminate lock ok'", "OM-5 terminate in critical:");
            expect(out, "==> (1 2 3 4 )", "OM-5 terminate in an unwind block");
        }
    }

    /*
     *  signalException: and a handler that resumes.
     *
     *  The frame it spliced in came from newProcess, which wraps the block
     *  in one that ends the process, so resuming the exception ended the
     *  process: `done' stayed nil.  And a return into a context stopped
     *  between two bytecodes pushes one value too many, so a resumption
     *  in the middle of `s + (1 + 2)' would have added the wrong thing.
     *  The sum is exact only if every interruption left the loop as it
     *  found it.  At most one signal can miss -- sent as the process
     *  finishes -- so the count is checked against that.
     */
    if (write_file(RESUMED,
            "| p done count i |\n"
            "count := 0. done := nil.\n"
            "p := [[| s | s := 0. [(s := s + (1 + 2)) < 300000] whileTrue: "
            "[Processor yield]. done := s]\n"
            "  on: Warning do: [:e | count := count + 1. e resume: 99]] "
            "fork.\n"
            "i := 0.\n"
            "[i < 20 and: [done isNil]] whileTrue: [(Delay forMilliseconds: "
            "2) wait. p signalException: Warning new. i := i + 1].\n"
            "[done isNil and: [p suspendedContext notNil]] whileTrue: "
            "[(Delay forMilliseconds: 10) wait].\n"
            "^(done = 300000 and: [count > 0 and: [count >= (i - 1)]]) "
            "ifTrue: ['resumed ok'] ifFalse: [{done. count. i}]\n") == 0) {
        char        batch[512];
        unsigned    workers[] = { 8, 1 };
        int         run_index;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", RESUMED);
        for (run_index = 0; run_index < 2; ++run_index) {
            status = serve(batch, workers[run_index], out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL resumed signal: could not run the server\n");
                break;
            }
            expect(out, "==> 'resumed ok'", "signalException: resumed");
        }
    }

    /*
     *  Bugs5 OM-6: terminating a waiter released its Monitor twice.
     *
     *  waitForChange gives the mutex up and waits; terminated there, the
     *  ensure: of the critical: around it released the mutex again, and
     *  from then on the monitor admitted two processes, then more.  Ten of
     *  twenty consumers blocked in SharedQueue>>next are terminated; the
     *  other ten must each get one of ten items, and afterwards the
     *  queue's own monitor must admit one process at a time.  The old
     *  code let sixty in at once.
     */
    if (write_file(MONITOR,
            "| q procs got inside most m mtx |\n"
            "q := SharedQueue new. got := 0. inside := 0. most := 0. "
            "mtx := Semaphore forMutualExclusion.\n"
            "procs := (1 to: 20) collect: [:i | [q next. mtx critical: "
            "[got := got + 1]] fork].\n"
            "(Delay forMilliseconds: 50) wait.\n"
            "1 to: 20 by: 2 do: [:i | (procs at: i) terminate].\n"
            "1 to: 10 do: [:i | q nextPut: i].\n"
            "(Delay forMilliseconds: 300) wait.\n"
            "m := q instVarAt: 1.\n"
            "(1 to: 8) do: [:k | [1 to: 2000 do: [:j | m critical: "
            "[inside := inside + 1. most := most max: inside. "
            "inside := inside - 1]]] fork].\n"
            "(Delay forMilliseconds: 300) wait.\n"
            "^(got = 10 and: [q size = 0 and: [most = 1]]) "
            "ifTrue: ['monitor ok'] ifFalse: [{got. q size. most}]\n") == 0) {
        char        batch[512];
        unsigned    workers[] = { 8, 1 };
        int         run_index;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", MONITOR);
        for (run_index = 0; run_index < 2; ++run_index) {
            status = serve(batch, workers[run_index], out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL OM-6: could not run the server\n");
                break;
            }
            expect(out, "==> 'monitor ok'", "OM-6 terminated waiter");
        }
    }

    /*
     *  Bugs5 OM-4: a word object's length is in words.  shallowCopy (148),
     *  the first-n copy (168) and class migration all asked
     *  OM_fetch_byte_length of one, got the word count, and copied that
     *  many BYTES -- into a byte object, for the two primitives, which
     *  basicAt:put: then wrote past the end of: two thousand copies of a
     *  thousand-word object, each written in full, ended in `malloc():
     *  corrupted top size'.  Each copy must come back whole, and the
     *  loop must leave a heap that still answers the next line.
     */
    if (write_file(WORDS,
            "| base cls w c k ok |\n"
            "base := Object subclass: #Bugs5WordBase instanceVariableNames: "
            "'' classVariableNames: '' poolDictionaries: '' "
            "category: 'Bugs5'.\n"
            "cls := base variableWordSubclass: #Bugs5Words "
            "instanceVariableNames: '' classVariableNames: '' "
            "poolDictionaries: '' category: 'Bugs5'.\n"
            "cls compile: 'keep: n <primitive: 168> ^nil' "
            "classified: 'probe'.\n"
            "w := cls new: 1000.\n"
            "1 to: 1000 do: [:i | w basicAt: i put: i].\n"
            "ok := [:x :n | x basicSize = n and: [(1 to: n) inject: true "
            "into: [:a :i | a and: [(x basicAt: i) = i]]]].\n"
            "c := w shallowCopy. k := w keep: 700.\n"
            "1 to: 2000 do: [:j | | d | d := w shallowCopy. "
            "1 to: 1000 do: [:i | d basicAt: i put: 16r4141]].\n"
            "base addInstVarName: 'zz'.\n"
            "^((ok value: c value: 1000) and: [(ok value: k value: 700) "
            "and: [ok value: w value: 1000]]) "
            "ifTrue: ['words whole'] ifFalse: ['halved']\n") == 0) {
        char        batch[512];

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", WORDS);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-4: could not run the server\n");
        } else {
            expect(out, "==> 'words whole'", "OM-4 word object copies");
            expect(out, "3 + 4 ==> 7", "OM-4 the heap survived");
        }
    }

    /*
     *  Bugs5 KERN-2: a critical: inside a handler that leaves by return:
     *  released nothing -- the handler's ensure: blocks never ran -- so
     *  the next critical: waited for ever.  The 1983 Semaphore>>critical:
     *  had no ensure: at all and is checked beside Mutex.
     */
    {
        static const char batch[] =
            "| m flag | flag := false. m := Mutex new. [Error signal: 'a'] "
            "on: Error do: [:e | m critical: [e return: 1]]. [m critical: "
            "[flag := true]] fork. (Delay forMilliseconds: 300) wait. "
            "flag ifTrue: ['mutex released'] ifFalse: ['mutex held']\n"
            "| m flag | flag := false. m := Semaphore forMutualExclusion. "
            "[Error signal: 'a'] on: Error do: [:e | m critical: "
            "[e return: 1]]. [m critical: [flag := true]] fork. "
            "(Delay forMilliseconds: 300) wait. (flag and: "
            "[(m instVarAt: 3) = 1]) ifTrue: ['semaphore released'] "
            "ifFalse: ['semaphore held']\n";

        status = serve(batch, 4, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL KERN-2: could not run the server\n");
        } else {
            expect(out, "==> 'mutex released'", "KERN-2 Mutex in a handler");
            expect(out, "==> 'semaphore released'",
                   "KERN-2 Semaphore in a handler");
        }
    }

    /*
     *  Bugs5 OM-8: an image survives a second out-of-memory.
     *
     *  The first releases the 64K reserve and the table grows into it;
     *  re-armed, the ceiling dropped back but the allocator tested only
     *  the table's size, so the next runaway ate the reserve before anyone
     *  noticed and releasing it then gave nothing to signal with.  Two
     *  runaways in one image, each caught, the second with the reserve
     *  re-armed.  Four million objects, because the table starts at that
     *  size and a lower ceiling stops an image of either build before its
     *  first OutOfMemory; about a minute, so not under a sanitizer.
     */
#if !(defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__) \
   || (defined(__has_feature) && (__has_feature(thread_sanitizer) \
                               || __has_feature(address_sanitizer))))
    if (write_file(BATCH,
            "| a r1 r2 | r1 := [a := OrderedCollection new. [a add: (Array "
            "new: 1)] repeat] on: OutOfMemory do: [:e | e return: a size]. "
            "a := nil. Smalltalk garbageCollect. r2 := [a := OrderedCollection "
            "new. [a add: (Array new: 1)] repeat] on: OutOfMemory do: [:e | e "
            "return: a size]. a := nil. (r1 > 100000 and: [r2 > 100000]) "
            "ifTrue: ['survived twice'] ifFalse: [{r1. r2}]\n") == 0) {
        snprintf(command, sizeof command,
                 "ST_MAX_OBJECTS=4194304 timeout -k 2 180 %s -serve %s "
                 "-workers 4 \"$(cat %s)\" 2>&1", st2026, IMAGE, BATCH);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-8: could not run the server\n");
        } else {
            expect(out, "==> 'survived twice'", "OM-8 second out-of-memory");
        }
    }
#endif

    /*
     *  Bugs5 FILES-2: eight workers compiling at once each get their own
     *  source back.  Every compile moves the one changes-file stream to its
     *  end, writes, and makes it read-only; every sourceCodeAt: moves it
     *  somewhere else and reads.  Unlocked, a run of eight times a hundred
     *  compiles garbled three hundred sources and raised `no writing
     *  allowed'.  LibraryLocks class>>holdingSources: now serializes them.
     */
    if (write_file(SOURCES,
            "| done bad errs n mtx classes |\n"
            "done := Semaphore new. bad := 0. errs := 0. n := 8. "
            "mtx := Semaphore forMutualExclusion.\n"
            "classes := (1 to: n) collect: [:k | Object subclass: ('ZZPar', "
            "k printString) asSymbol instanceVariableNames: '' "
            "classVariableNames: '' poolDictionaries: '' category: 'ZZPar'].\n"
            "1 to: n do: [:k | | cls | cls := classes at: k.\n"
            "  Processor forkParallel: [1 to: 100 do: [:i | | src sel |\n"
            "    sel := ('m', i printString) asSymbol.\n"
            "    src := 'm', i printString, ' ^', (k * 1000 + i) printString.\n"
            "    [cls compile: src classified: 'x'.\n"
            "     (cls sourceCodeAt: sel) = src ifFalse: [mtx critical: "
            "[bad := bad + 1]]]\n"
            "      on: Error do: [:e | mtx critical: [errs := errs + 1]]].\n"
            "    done signal]].\n"
            "n timesRepeat: [done wait].\n"
            "^'bad=', bad printString, ' errs=', errs printString\n") == 0) {
        char        batch[512];
        int         attempt;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", SOURCES);
        for (attempt = 0; attempt < 2; ++attempt) {
            status = serve(batch, 8, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL FILES-2: could not run the server\n");
                break;
            }
            expect(out, "==> 'bad=0 errs=0'", "FILES-2 parallel compiles");
        }
    }

    /*  B58: a startup that does not compile writes no image, exit 1.  */
    unlink(BADIMAGE);
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-startup '3 +' -o %s 2>&1", st2026, BADIMAGE);
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
             "-eval '3 zork' 2>&1", st2026);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 1) {
        ++st_test_failures;
        printf("  FAIL B58 -eval '3 zork' exited %d, want 1\n", status);
    }
    expect(out, "went unhandled", "B58 -eval unhandled");
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-eval '[3 zork] on: Error do: [:e | 5]' 2>&1", st2026);
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
    unlink(SHARED);
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

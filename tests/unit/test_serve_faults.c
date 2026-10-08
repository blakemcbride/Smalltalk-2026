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
 *  How long one server run may take before it is killed as a hang.
 *  Sixty seconds on a plain build; under a sanitizer the interpreter runs
 *  tens of times slower, and since these runs drive this build's own
 *  binary (Bugs5 DOCS-4) -- not the plain one they used to borrow -- the
 *  thirty-two-worker storms met the plain build's limit and were killed
 *  half way, which reported a hang that was only a slow binary.
 */
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__) \
 || (defined(__has_feature) && (__has_feature(thread_sanitizer) \
                             || __has_feature(address_sanitizer)))
#define SERVE_SECONDS   "1200"
#else
#define SERVE_SECONDS   "60"
#endif

/*
 *  A fixture's path in this build's scratch directory (see
 *  st_test_dir), made once per name and kept.
 *
 *  With this process's id in it (Bugs5 DOCS-6).  Two runs from one tree --
 *  two terminals, or make test beside a hand run -- shared every one of
 *  these, and each server read whichever batch file the other run had
 *  written last.  fixtures_remove takes them all away at the end, the
 *  image's .changes with them, so that a name per run is not a pile per
 *  run.
 */
/*
 *  Sixty-four, not thirty-two: the Bugs6 file findings brought the count
 *  of names past the old table, and a name the table has no room for
 *  comes back BARE -- so a snapshot was written under the tree's root and
 *  the run that was to resume it looked in the test directory and could
 *  not open it.  Names derived from one fixture (`.im', `.im.changes')
 *  are made with snprintf by their users rather than registered here.
 */
static struct { const char *name; char *path; } fixtures_made[64];

static const char *
fixture(const char *name)
{
    size_t  i;
    size_t  n;

    for (i = 0; i < sizeof fixtures_made / sizeof fixtures_made[0]
                && fixtures_made[i].name; ++i)
        if (strcmp(fixtures_made[i].name, name) == 0)
            return fixtures_made[i].path;
    if (i == sizeof fixtures_made / sizeof fixtures_made[0])
        return name;
    n = strlen(st_test_dir()) + strlen(name) + 32;
    fixtures_made[i].path = (char *) malloc(n);
    if (!fixtures_made[i].path)
        return name;
    snprintf(fixtures_made[i].path, n, "%s/%ld-%s", st_test_dir(),
             st_test_pid(), name);
    fixtures_made[i].name = name;
    return fixtures_made[i].path;
}

static void
fixtures_remove(void)
{
    char    changes[1024];
    size_t  i;

    for (i = 0; i < sizeof fixtures_made / sizeof fixtures_made[0]
                && fixtures_made[i].name; ++i) {
        unlink(fixtures_made[i].path);
        snprintf(changes, sizeof changes, "%s.changes", fixtures_made[i].path);
        unlink(changes);
    }
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
 *  expressions are full of quotes.  SERVE_SECONDS and then SIGKILL: a
 *  hang is one of the faults being checked for.
 */
static int
serve(const char *batch, unsigned workers, char *out, size_t len)
{
    char    command[1024];

    if (write_file(BATCH, batch) != 0)
        return -1;
    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -serve %s -workers %u"
             " \"$(cat %s)\" 2>&1",
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

/*
 *  How many collections a GC log reports: one "  gc #" line each.
 */
static unsigned
om_collections_logged(const char *out)
{
    unsigned    n = 0;
    const char *p = out;

    while ((p = strstr(p, "  gc #")) != NULL) {
        ++n;
        p += 6;
    }
    return n;
}

/*
 *  Bugs5 low findings, om.  Each of these needs a pool, a class of its own
 *  or a second run of a saved image, which is why they are here and not in
 *  test_image.
 */
static void
bugs5_low_om(void)
{
    static char out[65536];
    char        command[2048];
    int         status;

    /*
     *  OM-8, found alongside: a ceiling below the table's starting size is
     *  honoured.  ST_MAX_OBJECTS=1048576 used to be silently replaced by
     *  the 64M default, so a runaway never met an OutOfMemory -- it was
     *  still allocating after a hundred seconds and 2.3 GB.  And once the
     *  ceiling was honoured, the second runaway in such an image found the
     *  reserve never re-armed, because the re-arm tested the table's
     *  high-water mark, which the booted image alone holds at 255,177.
     *  And under it lay two faults the high ceiling had hidden: the holes a
     *  loaded image comes with were never put on the free chain, and a
     *  collection that could not lower the table's limit dropped every
     *  index sitting in a worker's magazine -- so a table three quarters
     *  free collected for ever without reaching its OutOfMemory.  Half a
     *  million: two runaways caught, about four seconds.  It was a
     *  quarter of a million, the lowest ceiling accepted, until the
     *  Bugs6 fixes grew the library past it -- the bootstrap's table
     *  high-water mark, which a loaded image needs whole, passed 262,144
     *  and the image could not be loaded under the ceiling at all.
     */
    if (write_file(BATCH,
            "| a r1 r2 | r1 := [a := OrderedCollection new. [a add: (Array "
            "new: 1)] repeat] on: OutOfMemory do: [:e | e return: a size]. "
            "a := nil. Smalltalk garbageCollect. r2 := [a := OrderedCollection "
            "new. [a add: (Array new: 1)] repeat] on: OutOfMemory do: [:e | e "
            "return: a size]. a := nil. (r1 between: 100000 and: 524288) & "
            "(r2 between: 100000 and: 524288) ifTrue: ['survived twice'] "
            "ifFalse: [{r1. r2}]\n") == 0) {
        snprintf(command, sizeof command,
                 "ST_MAX_OBJECTS=524288 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s "
                 "-serve %s -workers 4 \"$(cat %s)\" 2>&1",
                 st2026, IMAGE, BATCH);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-8-alongside: could not run the server\n");
        } else {
            expect(out, "==> 'survived twice'",
                   "OM-8-alongside ceiling below the starting size");
        }
    }

    /*
     *  COMP-8: a sharedPools slot that is not a collection is no pools, and
     *  the walk up the superclass chain goes on past it.  It used to ask
     *  about the same class for ever, in C, with the Symbol lock held.
     *  (The walk is bounded against a superclass cycle as well, but a
     *  compile: into a cycled class never reaches it: Behavior's own
     *  instance-variable walk in Smalltalk exceeds the depth limit first.)
     */
    check_survives("Object subclass: #ZZComp8 instanceVariableNames: '' "
                   "classVariableNames: '' poolDictionaries: '' category: 'x'."
                   " (Smalltalk at: #ZZComp8) instVarAt: 9 put: 3."
                   " (Smalltalk at: #ZZComp8) compile: 'h ^Transcript'."
                   " (Smalltalk at: #ZZComp8) new h == Transcript"
                   " ifTrue: ['pools walked'] ifFalse: ['wrong']\n"
                   "3 + 4\n",
                   "==> 'pools walked'", "COMP-8 sharedPools of 3");

    /*
     *  OM-12: one Delay timing process, not two.  The bootstrap runs the
     *  class initializer twice and the first pass's process, not yet run,
     *  read the second pass's TimingSemaphore when it did.
     */
    check_survives("Smalltalk garbageCollect. ((Delay classPool at: "
                   "#TimingSemaphore) size = 1 and: [(Process allInstances "
                   "select: [:p | p priority = Processor timingPriority]) "
                   "size = 1]) ifTrue: ['one timing process'] ifFalse: "
                   "[{(Delay classPool at: #TimingSemaphore) size}]\n"
                   "3 + 4\n",
                   "==> 'one timing process'", "OM-12 timing processes");

    /*
     *  OM-10: idle workers do not stall the reclamation epoch.  One worker
     *  allocating, seven idle: the busy one's dropped Arrays used to wait
     *  for the stop-the-world collector -- six collections where one
     *  worker alone needed one -- because the epoch advanced only when
     *  every worker had published it and an idle one never does.
     */
    if (write_file(BATCH, "| x | 1 to: 3000000 do: [:i | x := Array with: i "
                          "with: (Array new: 3)]. 'allocated'\n") == 0) {
        snprintf(command, sizeof command,
                 "ST_GC_LOG=1 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -serve %s "
                 "-workers 8 \"$(cat %s)\" 2>&1", st2026, IMAGE, BATCH);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-10: could not run the server\n");
        } else {
            expect(out, "==> 'allocated'", "OM-10 allocation loop");
            ++st_test_checks;
            if (om_collections_logged(out) > 2) {
                ++st_test_failures;
                printf("  FAIL OM-10: %u collections on eight workers, "
                       "want at most 2\n", om_collections_logged(out));
            }
        }
    }

    /*
     *  OM-9: an ephemeron the snapshot's own collection fired is mourned
     *  in the image that comes back.  The writer queued it and cleared its
     *  ephemeron bit, the queue is not in the file, and the reloaded image
     *  held a plain object no one would ever tell.  The saved image resumes
     *  the batch after the snapshot line, so the last line runs there.
     */
    {
        const char *saved = fixture("bugs5-om9");
        char        batch[2048];

        unlink(fixture("bugs5-om9.im"));
        snprintf(batch, sizeof batch,
                 "Object ephemeronSubclass: #Bugs5OmEph instanceVariableNames:"
                 " 'key' classVariableNames: '' poolDictionaries: '' "
                 "category: 'Bugs5'\n"
                 "(Smalltalk at: #Bugs5OmEph) compile: 'fill key := Object new'"
                 " classified: 'x' notifying: nil\n"
                 "(Smalltalk at: #Bugs5OmEph) compile: 'mourn Smalltalk at: "
                 "#Bugs5OmMourned put: (Smalltalk at: #Bugs5OmMourned) + 1' "
                 "classified: 'x' notifying: nil\n"
                 "Smalltalk at: #Bugs5OmMourned put: 0. Smalltalk at: "
                 "#Bugs5OmKeeper put: (Smalltalk at: #Bugs5OmEph) new. "
                 "(Smalltalk at: #Bugs5OmKeeper) fill. 0\n"
                 "Smalltalk snapshotAs: '%s' thenQuit: true\n"
                 "(Delay forMilliseconds: 100) wait. Smalltalk garbageCollect."
                 " (Delay forMilliseconds: 100) wait. 'mourned ', "
                 "(Smalltalk at: #Bugs5OmMourned) printString\n", saved);
        (void) serve(batch, 2, out, sizeof out);
        snprintf(command, sizeof command,
                 ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -serve %s -workers 2 2>&1",
                 st2026, fixture("bugs5-om9.im"));
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-9: could not run the saved image\n");
        } else {
            expect(out, "==> 'mourned 1'", "OM-9 mourned after a reload");
        }

        /*
         *  OM-14: an image saved while timers fire comes back with every
         *  process on the list it says it is on.  The write used to happen
         *  after the collection's safepoint, with frozen workers still
         *  draining timer signals and moving processes between lists.  A
         *  race, so this cannot fail every time; it is the shape of the
         *  check that would see it.  One worker when reloaded, so the
         *  processes at background priority stay still while it looks.
         */
        unlink(fixture("bugs5-om9.im"));
        snprintf(batch, sizeof batch,
                 "Smalltalk at: #B5OmStop put: false. 1 to: 32 do: [:k | "
                 "[[Smalltalk at: #B5OmStop] whileFalse: [(Delay "
                 "forMilliseconds: 1) wait]] forkAt: Processor "
                 "userBackgroundPriority]. (Delay forMilliseconds: 50) wait. 0\n"
                 "Smalltalk snapshotAs: '%s' thenQuit: true\n"
                 "| bad | bad := 0. Process allInstances do: [:p | | l | l := "
                 "p instVarAt: 4. (l notNil and: [(l isKindOf: LinkedList) "
                 "and: [(l includes: p) not]]) ifTrue: [bad := bad + 1]]. "
                 "Smalltalk at: #B5OmStop put: true. 'bad ', bad printString\n",
                 saved);
        (void) serve(batch, 4, out, sizeof out);
        snprintf(command, sizeof command,
                 ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -serve %s -workers 1 2>&1",
                 st2026, fixture("bugs5-om9.im"));
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-14: could not run the saved image\n");
        } else {
            expect(out, "==> 'bad 0'", "OM-14 lists after a snapshot");
        }
        unlink(fixture("bugs5-om9.im"));
        unlink(fixture("bugs5-om9.im.changes"));
    }
}

static void
bugs5_low_sched(void)
{
}

static void
bugs5_low_kern(void)
{
}

static void
bugs5_low_comp(void)
{
    static char out[65536];

    /*
     *  COMP-7.  The Decompiler failed on 1,646 of the image's methods --
     *  the C compiler's jump layout and the closure bytecodes, neither of
     *  which 1983's knew.  Every method decompiles now; the count of those
     *  that do not is the number that must stay at nought.  A few seconds
     *  here and more than twenty minutes under TSAN, so not under a
     *  sanitizer; the method below covers the same code in little.
     */
#if !(defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__) \
   || (defined(__has_feature) && (__has_feature(thread_sanitizer) \
                               || __has_feature(address_sanitizer))))
    serve("[:bad | Smalltalk allBehaviorsDo: [:c | c selectors do: [:s |"
          " [(c decompile: s) decompileString] on: Error do: [:e |"
          " bad add: s]]]. bad size] value: OrderedCollection new\n",
          1, out, sizeof out);
    expect(out, "bad size] value: OrderedCollection new ==> 0",
           "COMP-7 every method in the image should decompile");
#endif

    /*
     *  And what it answers is the method.  A closure with a local nobody
     *  uses, a variable two blocks share and assign (a vector, copied into
     *  the inner block), loops with and without a body, and:, or:, a
     *  cascade, brace arrays and a conditional for its value -- decompiled,
     *  compiled again under another name, and run.
     */
    serve("Object compile: 'zzComp7: n | a b | a := 0. b := [:x | | u w |"
          " w := x. a := a + w. [:y | a := a + y + n] value: 1. a]."
          " #(1 2) do: [:e | b value: e]. [a > 100] whileFalse: [a := a * 2]."
          " [a := a + 1. a > 1000] whileFalse. ^{a. n even and: [n > 1]."
          " n odd or: [false]. (OrderedCollection new add: 3; add: 4;"
          " yourself) asArray. {}. [:p | ] value: 1."
          " n > 2 ifTrue: [#big] ifFalse: [#small]}'\n"
          "Object compile: ((Object decompile: #zzComp7:) decompileString"
          " copyReplaceAll: 'zzComp7:' with: 'zzComp7b:')\n"
          "3 zzComp7b: 3\n"
          "(3 zzComp7: 3) = (3 zzComp7b: 3)\n",
          1, out, sizeof out);
    expect(out, "3 zzComp7b: 3 ==> (1001 false true (3 4 ) () nil big )",
           "COMP-7 a decompiled closure method should compile and run");
    expect(out, "(3 zzComp7: 3) = (3 zzComp7b: 3) ==> true",
           "COMP-7 the decompiled method should answer what the original"
           " does");
}

static void
bugs5_low_net(void)
{
    static char out[65536];
    const char *login = fixture("bugs5-net-login.st");
    char        batch[512];
    int         status;

    /*
     *  Bugs5 NET-13: every log line is one line.  Bugs4 NET-6 escaped the
     *  two fields RestDispatcher logs, and the rest of RestLog -- the demo
     *  upload line with a client's file name in it -- and HttpServer>>log:
     *  wrote CRs and ANSI escapes to standard error as they came.  The
     *  batch builds the characters rather than holding them, so that the
     *  only place a raw one could appear is the log line itself.
     */
    status = serve("RestLog info: 'zqA', (String with: (Character value: 13)),"
                   " 'B', (String with: (Character value: 27)), '[2J'\n"
                   "(HttpServer new name: 'zqhs') log: 'C', (String with:"
                   " (Character value: 10)), 'D'\n"
                   "3 + 4\n", 2, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL NET-13: could not run the server\n");
    } else {
        expect(out, "info: zqA\\x0dB\\x1b[2J", "NET-13 RestLog escapes");
        expect(out, "zqhs: C\\x0aD", "NET-13 HttpServer>>log: escapes");
        expect_absent(out, "zqA\rB", "NET-13 RestLog wrote a raw CR");
        expect_absent(out, "\033[2J", "NET-13 RestLog wrote a raw ESC");
    }

    /*
     *  Bugs5 NET-15, the second half: the demo's Login answered nil at
     *  once for a user name with no row, and only a name that had one paid
     *  for PBKDF2, so the time an `Invalid login' took said which names
     *  exist.  Login is loaded from demo/backend over a database of one
     *  method that finds nobody; the miss must cost at least half of one
     *  PBKDF2 at PasswordHash's iterations -- the best of three of each,
     *  so a busy machine slows both rather than one.
     */
    if (write_file(login,
            "| cls req miss hash |\n"
            "cls := Object subclass: #ZZNetDb instanceVariableNames: '' "
            "classVariableNames: '' poolDictionaries: '' category: 'ZZNet'.\n"
            "cls compile: 'fetchOne: q with: a ^nil' classified: 'x'.\n"
            "cls compile: 'db ^self' classified: 'x'.\n"
            "TonelReader loadFile: 'demo/backend/Login.class.st'.\n"
            "req := cls new.\n"
            "miss := (1 to: 3) inject: 1000000 into: [:m :i | m min: "
            "(Time millisecondsToRun: [(Smalltalk at: #Login) login: 'nobody' "
            "password: 'pw' outjson: nil request: req])].\n"
            "hash := (1 to: 3) inject: 1000000 into: [:m :i | m min: "
            "(Time millisecondsToRun: [Crypto pbkdf2: 'pw' salt: 'abcdefgh' "
            "iterations: PasswordHash iterations])].\n"
            "^'login-miss ', (miss * 2 >= hash) printString, ' ', "
            "miss printString, '/', hash printString\n") != 0) {
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL NET-15: could not write the login script\n");
        return;
    }
    snprintf(batch, sizeof batch,
             "Compiler evaluate: (FileStream oldFileNamed: '%s') "
             "contentsOfEntireFile\n", login);
    status = serve(batch, 1, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL NET-15: could not run the server\n");
    } else {
        expect(out, "==> 'login-miss true", "NET-15 an unknown user costs a verify");
    }
    unlink(login);
}

/*
 *  Whether there is anything at all at a fixture path.
 */
static int
files_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void
bugs5_low_files(void)
{
    static char out[65536];
    char        batch[2048];
    char        command[2048];

    /*
     *  FILES-10.  The file layer's errno was one static for every worker.
     *  One process fails to open a missing file, waits a moment, and asks
     *  lastError, while six others open a directory as fast as they can:
     *  it answered their EISDIR, 21, a hundred times in a hundred.
     */
    serve("[:done :stop | | bad | 1 to: 6 do: [:k | [[stop first]"
          " whileFalse: [Disk fileClass new doPrimCommand: 4 name: '/'"
          " page: nil]. done signal] fork]. (Delay forMilliseconds: 50)"
          " wait. bad := (1 to: 100) inject: 0 into: [:n :i | | f |"
          " f := Disk fileClass new. f doPrimCommand: 4 name:"
          " '/nonexistent-bugs5-files10/x' page: nil. 1 to: 2000 do:"
          " [:j | j even]. n + (f lastError = 2 ifTrue: [0] ifFalse: [1])]."
          " stop at: 1 put: true. 6 timesRepeat: [done wait]. bad]"
          " value: Semaphore new value: (Array with: false)\n",
          8, out, sizeof out);
    expect(out, "] value: Semaphore new value: (Array with: false) ==> 0\n",
           "FILES-10 each worker's own errno");

    /*
     *  SNAPSHOT-zero-byte.  snapshotAs: opened the image file before
     *  primitive 97 wrote anything -- and 97 writes a .tmp and renames
     *  it -- so a snapshot that failed left a new name behind as an
     *  empty .im.  The .tmp is made a directory so that 97 fails.
     */
    snprintf(command, sizeof command, "rm -rf %s %s.tmp && mkdir %s.tmp",
             fixture("bugs5-files-snap.im"), fixture("bugs5-files-snap.im"),
             fixture("bugs5-files-snap.im"));
    (void) system(command);
    snprintf(batch, sizeof batch,
             "[Smalltalk snapshotAs: '%s'] on: Error do: [:e | 'failed']\n"
             "(Delay forMilliseconds: 5) wait. 3 + 4\n",
             fixture("bugs5-files-snap"));
    serve(batch, 2, out, sizeof out);
    expect(out, "==> 'failed'", "SNAPSHOT-zero-byte the snapshot fails");
    expect(out, "3 + 4 ==> 7", "SNAPSHOT-zero-byte Delay still works");
    ++st_test_checks;
    if (files_exists(fixture("bugs5-files-snap.im"))) {
        ++st_test_failures;
        printf("  FAIL SNAPSHOT-zero-byte: a failed snapshot left %s\n",
               fixture("bugs5-files-snap.im"));
    }
    snprintf(command, sizeof command, "rm -rf %s.tmp",
             fixture("bugs5-files-snap.im"));
    (void) system(command);

    /*
     *  FILES-9.  After condenseChanges the one-writer lock was held on
     *  the changes file it had removed, and a second -serve of the image
     *  was admitted.  The first server condenses and then stays up; the
     *  second must still be refused.  Started once the first has said
     *  'condensed', not after a fixed sleep -- under TSAN the condense
     *  takes longer than any sleep worth writing -- and the first is then
     *  stopped by its pid rather than left to run out its wait.
     */
    snprintf(command, sizeof command,
             "rm -f %s %s.changes && cp %s %s",
             fixture("bugs5-files-condense.im"),
             fixture("bugs5-files-condense.im"), IMAGE,
             fixture("bugs5-files-condense.im"));
    (void) system(command);
    if (write_file(BATCH, "Smalltalk condenseChanges. 'condensed'\n"
                          "(Delay forSeconds: 600) wait. 'done'\n") == 0) {
        snprintf(command, sizeof command,
                 ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -serve %s -workers 2"
                 " \"$(cat %s)\" >%s.first 2>&1 & first=$!;"
                 " n=0; until grep -q \"==> 'condensed'\" %s.first"
                 " || [ $n -ge " SERVE_SECONDS "0 ]; do sleep 0.1; n=$((n+1));"
                 " done;"
                 " " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -serve %s -workers 1"
                 " '3 + 4' 2>&1; kill $first; wait $first; cat %s.first",
                 st2026, fixture("bugs5-files-condense.im"), BATCH,
                 fixture("bugs5-files-condense.im"),
                 fixture("bugs5-files-condense.im"),
                 st2026, fixture("bugs5-files-condense.im"),
                 fixture("bugs5-files-condense.im"));
        run(command, out, sizeof out);
        expect(out, "==> 'condensed'", "FILES-9 the condense ran");
        expect(out, "is already open by another st2026",
               "FILES-9 a second server is refused after a condense");
        expect_absent(out, "3 + 4 ==> 7",
                      "FILES-9 the second server did not run");
    } else {
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL FILES-9: cannot write %s\n", BATCH);
    }

    /*
     *  FILES-12, TonelWriter.  The pattern on the `Class >> pattern ['
     *  line was copied from the source, so a method compiled from one
     *  line wrote `+x' and `at:i put:v', which Pharo's reader splits
     *  wrongly, and a comment in the pattern was taken for a word.
     */
    serve("Object subclass: #B5FilesZork instanceVariableNames: ''"
          " classVariableNames: '' poolDictionaries: '' category: 'B5'."
          " (Smalltalk at: #B5FilesZork) compile: '+x ^x' classified: 'a'."
          " (Smalltalk at: #B5FilesZork) compile: 'at:i put:v ^i + v'"
          " classified: 'a'. (Smalltalk at: #B5FilesZork) compile:"
          " 'foo: \"c[\" a ^a' classified: 'a'. (TonelWriter sourceFor:"
          " (Smalltalk at: #B5FilesZork)) displayNl. 1\n", 2, out,
          sizeof out);
    expect(out, "B5FilesZork >> + x [", "FILES-12 a binary pattern");
    expect(out, "B5FilesZork >> at: i put: v [", "FILES-12 a keyword pattern");
    expect(out, "B5FilesZork >> foo: a [", "FILES-12 a comment in a pattern");
    expect(out, "\"c[\" ^a", "FILES-12 the comment is kept in the body");
}

#include <time.h>
#include <sys/stat.h>

/*
 *  Run a command and answer how many seconds it took, its output in out.
 */
static long
docs_timed_run(const char *command, char *out, size_t len, int *status)
{
    time_t  began = time(NULL);

    *status = run(command, out, len);
    return (long) (time(NULL) - began);
}

/*
 *  make, run from inside `make unit-test', without the parent's job server
 *  or level: only -n is wanted from it, and it is to read the Makefile, not
 *  join the build that is running this.  Nor the sanitizer switches a
 *  `make ASAN=1 unit-test' leaves in the environment: under them the rule
 *  for ./st2026 is the sanitizer one, not the one being checked.
 */
#define DOCS_MAKE   "env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL -u ASAN -u TSAN " \
                    "make --no-print-directory "

static void
bugs5_low_docs(void)
{
    static char out[65536];
    char        command[2048];
    char        path[1024];
    int         status;
    long        took;
    FILE       *f;

    /*
     *  DOCS-10: the message for a bare -inject asks for the script, not for
     *  the file name inject_reject warns about.
     */
    snprintf(command, sizeof command, "%s -inject 2>&1", st2026);
    run(command, out, sizeof out);
    expect(out, "-inject needs the script itself", "DOCS-10 -inject message");
    expect_absent(out, "script file", "DOCS-10 -inject message");

    /*
     *  DOCS-8: tools/timeout.sh's own watchdog -- the path a Mac without
     *  timeout(1) takes, forced here on a machine that has one.  It stops
     *  a hang and says 124, the GNU answer; it passes a command's output
     *  and status through; it does not hold the pipe open for the rest of
     *  the limit after the command is done, which a popen reader would sit
     *  out in full; and a whole serve check runs through it.
     */
    took = docs_timed_run("ST_TIMEOUT_WATCHDOG=1 " ST_TEST_TIMEOUT
                          " -k 1 1 sleep 60; echo status=$?",
                          out, sizeof out, &status);
    expect(out, "status=124", "DOCS-8 watchdog stops a hang");
    ++st_test_checks;
    if (took > 20) {
        ++st_test_failures;
        printf("  FAIL DOCS-8 watchdog took %ld seconds to stop sleep 60\n",
               took);
    }
    took = docs_timed_run("ST_TIMEOUT_WATCHDOG=1 " ST_TEST_TIMEOUT
                          " 60 sh -c 'echo through; exit 3'; echo status=$?",
                          out, sizeof out, &status);
    expect(out, "through\nstatus=3", "DOCS-8 watchdog passes status");
    ++st_test_checks;
    if (took > 20) {
        ++st_test_failures;
        printf("  FAIL DOCS-8 watchdog held the pipe %ld seconds\n", took);
    }
    if (setenv("ST_TIMEOUT_WATCHDOG", "1", 1) == 0) {
        status = serve("3 + 4\n", 2, out, sizeof out);
        unsetenv("ST_TIMEOUT_WATCHDOG");
        expect(out, "3 + 4 ==> 7", "DOCS-8 serve under the watchdog");
    }

    /*
     *  DOCS-7: a profile run that stalls is stopped at the wall-clock
     *  limit and failed by name, with the end of its output.  The binary
     *  is a stand-in that prints a line and then waits for ever, burning
     *  no bytecodes -- the stall the budget could not see.
     */
    snprintf(path, sizeof path, "%s", fixture("docs-stall.sh"));
    if (write_file(path, "#!/bin/sh\necho stalled in the middle of a test\n"
                         "exec sleep 600\n") == 0
     && chmod(path, 0755) == 0
     && write_file(fixture("docs-stall.expected"), "st2026 1 1\n") == 0) {
        snprintf(command, sizeof command,
                 ST_TEST_TIMEOUT " -k 2 120 env ST_PROFILE_SECONDS=2 "
                 "sh tests/run_profiles.sh %s %s 2>&1; echo status=$?",
                 path, fixture("docs-stall.expected"));
        took = docs_timed_run(command, out, sizeof out, &status);
        expect(out, "FAIL st2026: did not finish in 2 seconds",
               "DOCS-7 stalled profile stopped");
        expect(out, "stalled in the middle of a test",
               "DOCS-7 stalled profile's output shown");
        expect(out, "status=1", "DOCS-7 stalled profile fails");
        ++st_test_checks;
        if (took > 60) {
            ++st_test_failures;
            printf("  FAIL DOCS-7 a 2-second limit took %ld seconds\n", took);
        }
    } else {
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL DOCS-7: cannot write the stand-in binary\n");
    }

    /*
     *  DOCS-6: this run's fixtures carry its pid, so another run's are
     *  other files.
     */
    snprintf(path, sizeof path, "/%ld-", st_test_pid());
    expect(IMAGE, path, "DOCS-6 serve fixture named per run");
    expect(st_test_path("x"), path, "DOCS-6 st_test_path named per run");

    /*
     *  DOCS-5: under make, this program was built with a dependency file
     *  that names the header every suite includes, so editing it rebuilds
     *  them.
     */
    if (getenv("ST_TEST_DIR")) {
        snprintf(path, sizeof path, "%s/test_serve_faults.d", st_test_dir());
        out[0] = '\0';
        if ((f = fopen(path, "r")) != NULL) {
            size_t  n = fread(out, 1, sizeof out - 1, f);

            out[n] = '\0';
            fclose(f);
        }
        expect(out, "st_test.h", "DOCS-5 test binaries track headers");
    }

    /*
     *  DOCS-11 and DOCS-12, from the Makefile itself: clean takes what the
     *  builds and suites leave, and a HEADLESS build does not overwrite
     *  ./st2026 where there is a windowed build to keep.
     */
    run(DOCS_MAKE "-n clean 2>&1", out, sizeof out);
    expect(out, "demo.im", "DOCS-11 clean removes demo.im");
    expect(out, "st2026-parallel-file-test.txt",
           "DOCS-11 clean removes the suites' fixtures");
    expect(out, "demo/DB.sqlite", "DOCS-11 clean removes the demo database");
    run(DOCS_MAKE "-n HEADLESS=1 st2026 2>&1 | tail -5", out, sizeof out);
    expect(out, "if [ -x build/mt/st2026 ]",
           "DOCS-12 HEADLESS keeps a windowed ./st2026");
    expect(out, "./st2026 untouched", "DOCS-12 HEADLESS says so");
}

/*
 *  Bugs6, the two critical findings.
 */
static void
bugs6_critical(void)
{
    static char out[65536];
    const char *mutual  = fixture("bugs6-serve-mutual.st");
    const char *classes = fixture("bugs6-serve-classes.st");
    char        batch[512];
    int         status;

    /*
     *  SCHED-1: two processes terminating each other at the same moment.
     *
     *  primDetach: waits for the worker holding its target to park it at
     *  its next bytecode, and a worker inside primDetach: never reaches
     *  one: a and b, each terminating the other, spun both their workers
     *  for ever, and on two workers nothing else in the image ran again.
     *  Two hundred rounds of the audit's pair; every round one of the two
     *  must survive to set the flag.  The old binary failed within sixty
     *  rounds on eight workers and hung outright on two, which here is
     *  the sixty-second kill.
     */
    if (write_file(mutual,
            "| run |\n"
            "run := [:rounds | | ok |\n"
            "  ok := true.\n"
            "  1 to: rounds do: [:i | | go a b finished tries |\n"
            "    ok ifTrue: [\n"
            "      finished := false. go := Semaphore new.\n"
            "      a := [go wait. b terminate. finished := true] newProcess.\n"
            "      b := [go wait. a terminate. finished := true] newProcess.\n"
            "      a resume. b resume.\n"
            "      (Delay forMilliseconds: 1) wait.\n"
            "      go signal. go signal.\n"
            "      tries := 0.\n"
            "      [finished or: [tries >= 1000]] whileFalse: "
            "[(Delay forMilliseconds: 1) wait. tries := tries + 1].\n"
            "      finished ifFalse: [ok := false]]].\n"
            "  ok].\n"
            "^(run value: 200) ifTrue: ['mutual ok'] ifFalse: ['mutual STUCK']\n")
        == 0) {
        unsigned    workers[] = { 2, 8 };
        int         run_index;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", mutual);
        for (run_index = 0; run_index < 2; ++run_index) {
            status = serve(batch, workers[run_index], out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL SCHED-1: could not run the server\n");
                break;
            }
            expect(out, "==> 'mutual ok'", "SCHED-1 mutual terminate");
        }
    }

    /*
     *  KERNB-1: eight workers defining fifty classes each under one
     *  superclass and one category.  The organizer's three arrays were
     *  replaced one at a time with no lock and the superclass's Set of
     *  subclasses grew under whoever was adding to it: every run lost
     *  tens of subclasses and two hundred organizer entries, printed
     *  SubscriptOutOfBounds from the organizer, and now and then looped
     *  for ever on a nil read out of a half-replaced array.  Now every
     *  class is a subclass, is in its category, is in the change set, and
     *  nothing raised.  A handler round each definition so that the old
     *  binary's endless loop reports as a count rather than a hang.
     */
    if (write_file(classes,
            "| sup done lostSub lostOrg lostGlob lostChg errs |\n"
            "Object subclass: #Bugs6KbSup instanceVariableNames: '' "
            "classVariableNames: '' poolDictionaries: '' "
            "category: 'Bugs6-KbProbe'.\n"
            "sup := Smalltalk at: #Bugs6KbSup. done := Semaphore new. "
            "errs := 0.\n"
            "1 to: 8 do: [:w | [1 to: 50 do: [:i | [sup subclass: "
            "('Bugs6KbSub', w printString, 'x', i printString) asSymbol "
            "instanceVariableNames: '' classVariableNames: '' "
            "poolDictionaries: '' category: 'Bugs6-KbProbe'] "
            "on: Error do: [:e | errs := errs + 1. e return: nil]]. "
            "done signal] fork].\n"
            "1 to: 8 do: [:i | done wait].\n"
            "lostSub := 400 - sup subclasses size. lostGlob := 0. "
            "lostOrg := 0.\n"
            "1 to: 8 do: [:w | 1 to: 50 do: [:i | | n | "
            "n := ('Bugs6KbSub', w printString, 'x', i printString) asSymbol.\n"
            "  (Smalltalk includesKey: n) ifFalse: [lostGlob := lostGlob + 1].\n"
            "  ((SystemOrganization listAtCategoryNamed: #'Bugs6-KbProbe') "
            "includes: n) ifFalse: [lostOrg := lostOrg + 1]]].\n"
            "lostChg := 400 - (Smalltalk changes changedClasses select: "
            "[:c | c name beginsWith: 'Bugs6KbSub']) size.\n"
            "^'classes lost ', lostSub printString, ' ', lostGlob printString, "
            "' ', lostOrg printString, ' ', lostChg printString, "
            "' errors ', errs printString\n") == 0) {
        int     run_index;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", classes);
        for (run_index = 0; run_index < 2; ++run_index) {
            status = serve(batch, 8, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL KERNB-1: could not run the server\n");
                break;
            }
            expect(out, "==> 'classes lost 0 0 0 0 errors 0'",
                   "KERNB-1 eight workers defining classes");
            expect_absent(out, "SubscriptOutOfBounds",
                          "KERNB-1 the organizer stays whole");
            expect_absent(out, "Segmentation", "KERNB-1 no crash");
        }
    }
}

/*
 *  Bugs6, the object-memory findings among the high ones.
 */
static void
bugs6_high_om(void)
{
    static char out[65536];
    const char *oops = fixture("bugs6-serve-oops.st");
    const char *ctx  = fixture("bugs6-serve-ctx.st");
    char        batch[1024];
    char        command[2048];
    int         status;

    /*
     *  OM-1: doesNotUnderstand: built its Message round a freed Array.
     *
     *  The Array of arguments was allocated first and held in a C local
     *  while the Message was allocated; a collection inside that second
     *  allocation swept the Array, and the handler's `e message
     *  arguments' read a freed entry.  ST_GC_AT_CLASS=32 forces a
     *  collection at every Message allocation, which is the audit's
     *  oracle: the old binary dies with a segmentation fault on the
     *  first unhandled send, and the fixed one answers the arguments.
     */
    snprintf(command, sizeof command,
             "ST_GC_AT_CLASS=32 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
             " %s -serve %s -workers 1 \"[:x | ([nil zork: x] on: "
             "MessageNotUnderstood do: [:e | e message arguments]) first == x]"
             " value: (Array with: 1 with: 2 with: 3)\" 2>&1",
             st2026, IMAGE);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL OM-1: could not run the server\n");
    } else {
        expect(out, "value: (Array with: 1 with: 2 with: 3) ==> true",
               "OM-1 the Message's arguments survive a collection");
        expect_absent(out, "Segmentation", "OM-1 no crash");
    }

    /*
     *  OM-2: Smalltalk oopsLeft walked the free chain with no lock while
     *  seven workers took entries off it.  The walk read a header being
     *  relinked and never ended -- inside a primitive, so it never polled,
     *  and the next collection parked every other worker behind it for
     *  ever; or it dereferenced NULL.  The old binary died within a
     *  second of this; the walk is under the table lock now.
     */
    if (write_file(oops,
            "| fin |\n"
            "fin := Semaphore new.\n"
            "1 to: 7 do: [:w | [1 to: 300000 do: [:i | Array new: 3]. "
            "fin signal] fork].\n"
            "[1 to: 20000 do: [:i | Smalltalk oopsLeft]. fin signal] fork.\n"
            "1 to: 8 do: [:w | fin wait].\n"
            "^'oopsLeft survived'\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", oops);
        status = serve(batch, 8, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-2: could not run the server\n");
        } else {
            expect(out, "==> 'oopsLeft survived'",
                   "OM-2 oopsLeft beside allocating workers");
            expect_absent(out, "Segmentation", "OM-2 no crash");
        }
    }

    /*
     *  OM-3: a context below the active one, the home of a block, the
     *  suspendedContext of a waiting process, and the waiting process
     *  itself were all accepted by become: and becomeForward:, and each
     *  one stopped the image -- the sender chain or a Semaphore list was
     *  left naming an Object.  Every one is refused now, with the error
     *  the active context has always got; and an ordinary pair of Arrays
     *  still swaps and forwards.
     */
    if (write_file(ctx,
            "| r p |\n"
            "r := OrderedCollection new.\n"
            "p := [(Delay forSeconds: 30) wait] fork. "
            "(Delay forMilliseconds: 20) wait.\n"
            "r add: ([p suspendedContext becomeForward: Object new. #bad] "
            "on: Error do: [:e | #refused]).\n"
            "r add: ([p become: Object new. #bad] on: Error do: [:e | #refused]).\n"
            "r add: ([p becomeForward: Object new. #bad] "
            "on: Error do: [:e | #refused]).\n"
            "r add: ([thisContext sender becomeForward: Object new. #bad] "
            "on: Error do: [:e | #refused]).\n"
            "r add: ([thisContext home becomeForward: Object new. #bad] "
            "on: Error do: [:e | #refused]).\n"
            "r add: ([thisContext becomeForward: Object new. #bad] "
            "on: Error do: [:e | #refused]).\n"
            "r add: ([(Array new: 2) become: (Array new: 3). #swapped] "
            "on: Error do: [:e | #refused]).\n"
            "r add: ([(Array new: 2) becomeForward: (Array new: 3). #forwarded] "
            "on: Error do: [:e | #refused]).\n"
            "^r asArray\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", ctx);
        status = serve(batch, 1, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-3: could not run the server\n");
        } else {
            expect(out, "==> (refused refused refused refused refused refused "
                        "swapped forwarded )",
                   "OM-3 contexts and waiting processes refuse become:");
            expect(out, "3 + 4 ==> 7", "OM-3 the image survived");
        }
    }
}

/*
 *  Bugs6, the scheduler findings among the high ones: three ways a
 *  terminate or a signalException: lost a lock or tore a queue.  Every
 *  line is deterministic on one worker, where `Processor yield' runs a
 *  forked process up to its next wait or yield.
 */
static void
bugs6_high_sched(void)
{
    static char out[65536];
    const char *probe = fixture("bugs6-serve-sched.st");
    char        batch[512];
    int         status;

    /*
     *  SCHED-2: Delay>>wait kept 1983's `AccessProtect wait ... ensure:
     *  [AccessProtect signal]', so a process terminated after the wait
     *  returned and before the ensure: was entered kept the lock, and
     *  every Delay in the image waited for ever.  The lock is taken,
     *  handed to a process that is then terminated on its ready list,
     *  and must come back: the next Delay has to return.
     *
     *  SCHED-6: signalException: into a process waiting inside an
     *  ensure: block threw the rest of the block away.  The block has
     *  to finish (ensureEnd) before the handler runs (handled).
     *
     *  SCHED-7: terminate of a process parked inside a critical: section
     *  unwound it from the middle.  The section has to finish (1 2)
     *  before the process ends, and the Mutex be free after; then eight
     *  processes looping on short Delays, terminated and replaced at
     *  random for two seconds, must leave Delay working -- the old
     *  binary's timing process died on a nil at the head of the queue
     *  every run.
     */
    if (write_file(probe,
            "| ap p gate log m oc procs killer rnd stop n |\n"
            "ap := Delay classPool at: #AccessProtect. ap wait.\n"
            "p := [(Delay forMilliseconds: 5) wait] fork. Processor yield. "
            "ap signal.\n"
            "p terminate.\n"
            "(Delay forMilliseconds: 1) wait.\n"
            "log := OrderedCollection new. gate := Semaphore new.\n"
            "p := [[[log add: #body] ensure: [log add: #ensureStart. "
            "gate wait. log add: #ensureEnd]] on: Error do: [:e | "
            "log add: #handled]] fork.\n"
            "Processor yield. p signalException: (Error new messageText: "
            "'poke'). gate signal.\n"
            "(Delay forMilliseconds: 20) wait.\n"
            "m := Mutex new. oc := OrderedCollection new.\n"
            "p := [m critical: [oc add: 1. Processor yield. oc add: 2]] fork.\n"
            "Processor yield. p terminate.\n"
            "(Delay forMilliseconds: 20) wait.\n"
            "rnd := Random new. rnd seed: 7. stop := false. n := 0.\n"
            "procs := (1 to: 8) collect: [:i | [[stop] whileFalse: "
            "[(Delay forMilliseconds: 1 + (rnd next * 2) truncated) wait]] fork].\n"
            "killer := [[stop] whileFalse: [ | k | k := (rnd next * 8) "
            "truncated + 1. (procs at: k) terminate. procs at: k put: "
            "[[stop] whileFalse: [(Delay forMilliseconds: 1 + (rnd next * 2) "
            "truncated) wait]] fork. n := n + 1. Processor yield]] fork.\n"
            "(Delay forSeconds: 2) wait. stop := true. "
            "(Delay forMilliseconds: 50) wait.\n"
            "procs do: [:q | q terminate]. killer terminate.\n"
            "(Delay forMilliseconds: 5) wait.\n"
            "^{(ap instVarAt: 3). log asArray. oc asArray. "
            "p suspendedContext isNil. m isHeld. n > 100}\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", probe);
        status = serve(batch, 1, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL SCHED-2/6/7: could not run the server\n");
        } else {
            expect(out, "==> (1 (body ensureStart ensureEnd handled ) (1 2 ) "
                        "true false true )",
                   "SCHED-2/6/7 the lock comes back, the block finishes, "
                   "the section finishes, Delay survives the storm");
            expect(out, "3 + 4 ==> 7", "SCHED-2/6/7 the image survived");
        }
    }
}

/*
 *  Bugs6, the high findings fixed by area: one function per worker.
 */
static void
bugs6_high_comp(void)
{
    static char out[65536];
    const char *lits = fixture("bugs6-comp2.st");
    char        command[2048];
    int         status;

    /*
     *  COMP-2: the literals a compile builds lived in C arrays until the
     *  method was made, and the guard Array the primitive pushed held the
     *  first 256 of them and silently no more.  ST_GC_AT_CLASS=14 forces a
     *  collection at every String allocation, so a 300-string literal
     *  array is collected under 300 times while it is being built: the
     *  old binary answered 299 strings that were not the ones in the
     *  source, and the fixed compiler -- which roots the compile in flight
     *  -- answers every one.  One worker, so that nothing but the compile's
     *  own allocations is in play.
     */
    if (write_file(lits,
                   "[:n | | src a bad | src := WriteStream on: (String new: 4000)."
                   " src nextPutAll: 'bugs6Lits ^#('."
                   " 1 to: n do: [:i | src nextPutAll: '''s'; print: i; nextPutAll: ''' ']."
                   " src nextPutAll: ')'. Object compile: src contents."
                   " a := Object new bugs6Lits. bad := 0."
                   " 1 to: n do: [:i | ((a at: i) class == String and: [(a at: i) = ('s', i printString)])"
                   " ifFalse: [bad := bad + 1]]. 'bad=', bad printString] value: 300\n") != 0) {
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL COMP-2: cannot write the probe\n");
    } else {
        snprintf(command, sizeof command,
                 "ST_GC_AT_CLASS=14 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
                 " %s -serve %s -workers 1 \"$(cat %s)\" 2>&1",
                 st2026, IMAGE, lits);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL COMP-2: could not run the server\n");
        } else {
            expect(out, "==> 'bad=0'",
                   "COMP-2 a literal array's strings survive collections mid-compile");
            expect_absent(out, "Segmentation", "COMP-2 no crash");
        }
    }

    /*
     *  COMP-12: re-evaluating ContextPart's own definition rebuilt the
     *  class under the running contexts -- `asked to run an object that is
     *  not a context', 1383 errors, then nothing -- because the bootstrap's
     *  format word and 1983's recomputed one differ for the same shape.
     *  In its own process, since the old binary does not come back from
     *  it.  An unchanged definition answers the same class; a definition
     *  that would reshape ContextPart is refused before anything is
     *  touched; and the image goes on.
     */
    status = serve("[:old | Compiler evaluate: ContextPart definition logged: false."
                   " old == ContextPart] value: ContextPart\n"
                   "[Object subclass: #ContextPart instanceVariableNames: 'sender pc stackp bugs6'"
                   " classVariableNames: '' poolDictionaries: '' category: 'Kernel-Methods'."
                   " 'not refused'] on: Error do: [:e | e return: e messageText]\n"
                   "ContextPart instVarNames size\n"
                   "3 + 4\n", 2, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL COMP-12: could not run the server\n");
    } else {
        expect(out, "value: ContextPart ==> true",
               "COMP-12 an unchanged definition keeps the class");
        expect(out, "==> 'ContextPart cannot be reshaped: the interpreter holds "
                    "ContextPart instances in its registers; define a subclass instead'",
               "COMP-12 a reshape of ContextPart is refused");
        expect(out, "3 + 4 ==> 7", "COMP-12 the image goes on");
        expect_absent(out, "recompiling ContextPart", "COMP-12 nothing recompiled");
        expect_absent(out, "not a context", "COMP-12 no wreck");
    }
}

static void
bugs6_high_netgui(void)
{
    static char out[65536];
    const char *multipart = fixture("bugs6-serve-multipart.st");
    const char *parts     = fixture("bugs6-serve-parts.st");
    char        batch[1024];
    int         status;

    /*
     *  NET-1: a multipart POST, unauthenticated, against a RestServer that
     *  requires authentication.  indexOfSubstring:startingAt: compared the
     *  whole boundary at every position of the body, and parseParts ran
     *  it before the login check because the session token is a field of
     *  the form: 300 KB with a 2,000-character boundary left the worker
     *  scanning past the sixty-second mark with no reply.  Now the
     *  boundary is capped at RFC 2046's seventy characters (400 at once),
     *  the search is primitive 220, and a body of more than ten thousand
     *  parts is 400 where the cap is passed.  The client is the image's
     *  own Socket, so the run needs nothing outside the binary; the old
     *  binary answered the first request with nothing in twenty seconds.
     *  Two files, because one doIt may name 63 literals and the whole
     *  of this names more.
     */
    if (write_file(multipart,
            "| crlf server port post reply |\n"
            "crlf := String with: (Character value: 13) with: (Character value: 10).\n"
            "server := RestServer new port: 0; bindAddress: '127.0.0.1';\n"
            "  backendDirectory: 'tests/rest-backend'; requireAuthentication: true;\n"
            "  maxRequestBytes: 1000000; name: 'bugs6'; yourself.\n"
            "server start. port := server port.\n"
            "post := [:bnd :text | | sock buf n |\n"
            "  sock := Socket connectTo: '127.0.0.1' port: port.\n"
            "  sock send: 'POST /rest HTTP/1.1', crlf, 'Host: x', crlf,\n"
            "    'Content-Type: multipart/form-data; boundary=', bnd, crlf,\n"
            "    'Content-Length: ', text size printString, crlf,\n"
            "    'Connection: close', crlf, crlf, text.\n"
            "  buf := String new: 12.\n"
            "  n := sock receiveInto: buf timeout: 20000.\n"
            "  sock close.\n"
            "  n isNil ifTrue: ['no reply'] ifFalse: [buf copyFrom: 1 to: n]].\n"
            "reply := (post value: (String new: 2000 withAll: $A)\n"
            "    value: (String new: 300000 withAll: $B)), ' / ',\n"
            "  (post value: (String new: 70 withAll: $A)\n"
            "    value: (String new: 300000 withAll: $B)).\n"
            "server stop.\n"
            "^reply\n") == 0
     && write_file(parts,
            "| crlf body |\n"
            "crlf := String with: (Character value: 13) with: (Character value: 10).\n"
            "body := WriteStream on: (String new: 600000).\n"
            "10001 timesRepeat: [body nextPutAll: '--b'; nextPutAll: crlf;\n"
            "  nextPutAll: 'Content-Disposition: form-data; name=\"f\"';\n"
            "  nextPutAll: crlf; nextPutAll: crlf; nextPutAll: '1'; nextPutAll: crlf].\n"
            "body nextPutAll: '--b--'; nextPutAll: crlf. body := body contents.\n"
            "^'parts cap ', ([(HttpRequest fromString: 'POST /rest HTTP/1.1', crlf,\n"
            "  'Content-Type: multipart/form-data; boundary=b', crlf,\n"
            "  'Content-Length: ', body size printString, crlf, crlf, body)\n"
            "    parts size printString] on: HttpError do: [:e | e status printString])\n")
        == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n"
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", multipart, parts);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL NET-1: could not run the server\n");
        }  else  {
            expect(out, "==> 'HTTP/1.1 400 / HTTP/1.1 200'",
                   "NET-1 an unauthenticated multipart POST is answered at once");
            expect(out, "==> 'parts cap 400'",
                   "NET-1 a body of more than ten thousand parts is refused");
        }
    }

    /*
     *  GUI-1: a blit at the accepted coordinate limit, 2^30 on both axes,
     *  overflowed the clip sums and looped 2^26 words for 2^30 rows -- for
     *  ever, inside a primitive, where SIGTERM only sets a flag the loop
     *  never read.  On the old binary this is the sixty-second kill and
     *  the line after it never prints.
     */
    status = serve("Display fill: (1073741824@1073741824 extent: "
                   "1073741824@1073741824) mask: Form black. 'blit returned'\n"
                   "3 + 4\n", 2, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL GUI-1: could not run the server\n");
    }  else  {
        expect(out, "==> 'blit returned'", "GUI-1 the blit at 2^30 returns");
        expect(out, "3 + 4 ==> 7", "GUI-1 the pool is still there");
    }
}

/*
 *  Bugs6, the file and database findings among the high ones.  Two of them
 *  need two lives of one image -- a handle or a descriptor saved by one
 *  process and presented to the next -- and the third needs eight workers
 *  appending at once, which is why they are here and not in test_image.
 */
static void
bugs6_high_files(void)
{
    static char out[65536];
    const char *db_probe  = fixture("bugs6-serve-files1.st");
    const char *db_a      = fixture("bugs6-files1-a.db");
    const char *db_b      = fixture("bugs6-files1-b.db");
    const char *db_snap   = fixture("bugs6-files1-snap");
    const char *fd_probe  = fixture("bugs6-serve-files2.st");
    const char *fd_data   = fixture("bugs6-files2-data.txt");
    const char *fd_other  = fixture("bugs6-files2-other.txt");
    const char *fd_snap   = fixture("bugs6-files2-snap");
    const char *log_probe = fixture("bugs6-serve-files4.st");
    const char *log       = fixture("bugs6-files4.log");
    char        image[1024];
    char        changes[1024];
    char        batch[512];
    char        command[2048];
    char        text[4096];
    int         status;

    /*
     *  FILES-1.  A DbConnection saved open came back holding handle 0,
     *  and the first connect of the new life took slot 0: the stale
     *  object's fetchAll: read the new connection's rows, its isOpen
     *  answered true, and its close closed the new connection.  Now a
     *  handle carries the serial of its claim: the stale one answers "no
     *  such database connection" for ever and the new one is untouched.
     *  Through SQLite, and skipped where there is no driver, as
     *  test_odbc_parallel skips.
     */
    if (write_file(db_probe, "") == 0) {
        snprintf(text, sizeof text,
            "| ca cb r |\n"
            "ca := [DbConnection open: 'DRIVER=SQLITE3;Database=%s;'] "
            "on: Error do: [:e | nil].\n"
            "ca isNil ifTrue: [^'no sqlite driver'].\n"
            "ca execute: 'create table t (v varchar(20))'. "
            "ca execute: 'insert into t values (?)' with: #('from-A'). "
            "ca commit.\n"
            "Smalltalk at: #Bugs6FilesCA put: ca.\n"
            "(Smalltalk snapshotAs: '%s' thenQuit: true) ifTrue: [^'saved'].\n"
            "ca := Smalltalk at: #Bugs6FilesCA.\n"
            "cb := DbConnection open: 'DRIVER=SQLITE3;Database=%s;'.\n"
            "cb execute: 'create table t (v varchar(20))'. "
            "cb execute: 'insert into t values (?)' with: #('from-B'). "
            "cb commit.\n"
            "r := [((ca fetchAll: 'select v from t') collect: [:e | e at: 'v']) "
            "printString] on: Error do: [:e | e messageText].\n"
            "ca close.\n"
            "^'stale ', ca isOpen printString, ' ', r, ' / live ', "
            "cb isOpen printString, ' ', ((cb fetchAll: 'select v from t') "
            "collect: [:e | e at: 'v']) asArray printString\n",
            db_a, db_snap, db_b);
        snprintf(image, sizeof image, "%s.im", db_snap);
        snprintf(changes, sizeof changes, "%s.im.changes", db_snap);
        unlink(db_a);
        unlink(db_b);
        unlink(image);
        if (write_file(db_probe, text) == 0) {
            snprintf(batch, sizeof batch,
                     "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                     "contentsOfEntireFile\n", db_probe);
            status = serve(batch, 2, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL FILES-1: could not run the server\n");
            } else if (strstr(out, "==> 'no sqlite driver'")) {
                printf("  skipped FILES-1: no SQLITE3 driver\n");
            } else {
                snprintf(command, sizeof command,
                         ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
                         " %s -serve %s -workers 2 2>&1",
                         st2026, image);
                status = run(command, out, sizeof out);
                if (status < 0) {
                    ++st_test_failures;
                    printf("  FAIL FILES-1: could not run the saved image\n");
                } else
                    /*  A String answer is printed with its quotes doubled.  */
                    expect(out, "==> 'stale false no such database connection"
                                " / live true (''from-B'' )'",
                           "FILES-1 a connection from the previous life");
            }
        }
        unlink(db_a);
        unlink(db_b);
        unlink(image);
        unlink(changes);
    }

    /*
     *  FILES-2.  A File saved with descriptor N was believed as soon as
     *  the new life had opened N files: the old File read and wrote
     *  whichever file held N now.  The descriptor is now marked with the
     *  File that opened it, so the stale one is refused and the File is
     *  reopened by name.  Before: read 'OTHER-FILE', and the write went
     *  to the other file.
     */
    snprintf(text, sizeof text,
        "| f streams page r |\n"
        "(FileStream fileNamed: '%s') nextPutAll: 'DATA-OF-F'; close.\n"
        "(FileStream fileNamed: '%s') nextPutAll: 'OTHER-FILE'; close.\n"
        "f := Disk findKey: '%s'. f size. Smalltalk at: #Bugs6FilesF put: f.\n"
        "(Smalltalk snapshotAs: '%s' thenQuit: true) ifTrue: [^'saved'].\n"
        "f := Smalltalk at: #Bugs6FilesF.\n"
        "streams := (1 to: 16) collect: [:i | FileStream oldFileNamed: '%s'].\n"
        "page := f readPageNumber: 1.\n"
        "r := (page page copyFrom: 1 to: page size) asString.\n"
        "page page replaceFrom: 1 to: 9 with: 'WRITTEN-F' asByteArray "
        "startingAt: 1. page size: 9. f write: page.\n"
        "^'read ', r, ' data ', (FileStream oldFileNamed: '%s') "
        "contentsOfEntireFile, ' other ', (FileStream oldFileNamed: '%s') "
        "contentsOfEntireFile\n",
        fd_data, fd_other, fd_data, fd_snap, fd_other, fd_data, fd_other);
    snprintf(image, sizeof image, "%s.im", fd_snap);
    snprintf(changes, sizeof changes, "%s.im.changes", fd_snap);
    unlink(image);
    if (write_file(fd_probe, text) == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", fd_probe);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL FILES-2: could not run the server\n");
        } else {
            snprintf(command, sizeof command,
                     ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
                     " %s -serve %s -workers 2 2>&1",
                     st2026, image);
            status = run(command, out, sizeof out);
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL FILES-2: could not run the saved image\n");
            } else
                expect(out, "==> 'read DATA-OF-F data WRITTEN-F other OTHER-FILE'",
                       "FILES-2 a descriptor from the previous life");
        }
    }
    unlink(fd_data);
    unlink(fd_other);
    unlink(image);
    unlink(changes);

    /*
     *  FILES-4.  Eight workers, each fifty times opening its own stream
     *  on one log file, setToEnd, one line, close: 400 lines must be
     *  there afterwards.  The old binary kept 80 to 106 of them -- each
     *  stream's setToEnd was the end of its own cached page, each write
     *  put the whole page back, and each close truncated at its own
     *  position.
     */
    snprintf(text, sizeof text,
        "| sem n |\n"
        "(FileStream fileNamed: '%s') nextPutAll: ''; close.\n"
        "sem := Semaphore new. n := 8.\n"
        "1 to: n do: [:w | Processor forkParallel: [1 to: 50 do: [:i | | f | "
        "f := FileStream fileNamed: '%s'. f setToEnd; nextPutAll: 'w', "
        "w printString, '-', i printString, (String with: Character cr); "
        "close]. sem signal]].\n"
        "n timesRepeat: [sem wait].\n"
        "^'lines ', ((FileStream oldFileNamed: '%s') contentsOfEntireFile "
        "occurrencesOf: Character cr) printString\n",
        log, log, log);
    if (write_file(log_probe, text) == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", log_probe);
        status = serve(batch, 8, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL FILES-4: could not run the server\n");
        } else
            expect(out, "==> 'lines 400'",
                   "FILES-4 eight workers appending to one log");
    }
    unlink(log);
}

/*
 *  Bugs6, the kernel and concurrency findings among the high ones.
 */
static void
bugs6_high_kern(void)
{
    static char out[65536];
    const char *promise = fixture("bugs6-serve-promise.st");
    const char *waiter  = fixture("bugs6-serve-waiter.st");
    const char *storm   = fixture("bugs6-serve-monitor.st");
    char        batch[512];
    int         status;

    /*
     *  KERNB-2: a broken Promise signalled ONE stored Error in every
     *  waiter; eight woken together wrote their handlers into the same
     *  object and answered each other's contexts.  Each must get its
     *  own number back.
     */
    if (write_file(promise,
            "| p results done bad |\n"
            "results := Array new: 8. done := Semaphore new. p := Promise new.\n"
            "1 to: 8 do: [:i | [results at: i put: ([p value] on: Error "
            "do: [:e | e return: i]). done signal] fork].\n"
            "(Delay forMilliseconds: 200) wait.\n"
            "p signalError: (Error new messageText: 'boom').\n"
            "1 to: 8 do: [:i | done wait].\n"
            "bad := 0. 1 to: 8 do: [:i | (results at: i) = i ifFalse: "
            "[bad := bad + 1]].\n"
            "^'promise bad ', bad printString\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", promise);
        status = serve(batch, 8, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL KERNB-2: could not run the server\n");
        } else {
            expect(out, "==> 'promise bad 0'", "KERNB-2 one exception per asker");
            expect_absent(out, "a primitive has failed", "KERNB-2 no stray return:");
        }
    }

    /*
     *  KERNB-3: a Warning poked into a process waiting for a Mutex was
     *  resumed by its own defaultAction straight past the wait, into the
     *  section beside the holder.  The waiter must enter only once the
     *  holder lets go, and the semaphore must count one afterwards.
     */
    if (write_file(waiter,
            "| m holder waiter inside go rel log |\n"
            "m := Mutex new. go := Semaphore new. rel := Semaphore new. "
            "inside := 0. log := OrderedCollection new.\n"
            "holder := [m critical: [go signal. rel wait]] fork. go wait.\n"
            "waiter := [m critical: [inside := inside + 1. "
            "log add: #waiterInside]] fork.\n"
            "(Delay forMilliseconds: 100) wait.\n"
            "waiter signalException: (Warning new messageText: 'poke').\n"
            "(Delay forMilliseconds: 100) wait.\n"
            "log add: (m owner == holder); add: inside.\n"
            "rel signal. (Delay forMilliseconds: 100) wait.\n"
            "log add: inside; add: ((m instVarAt: 1) instVarAt: 3).\n"
            "^log asArray\n") == 0) {
        unsigned    workers[] = { 8, 1 };
        int         run_index;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", waiter);
        for (run_index = 0; run_index < 2; ++run_index) {
            status = serve(batch, workers[run_index], out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL KERNB-3: could not run the server\n");
                break;
            }
            expect(out, "==> (true 0 waiterInside 1 1 )",
                   "KERNB-3 a resumed poke goes back into its wait");
        }
    }

    /*
     *  FIXES-2: a storm of terminates on Monitor waiters left the
     *  Monitor's Mutex owned by nobody with its semaphore at zero, or a
     *  nil in its waiters, and on eight workers the image reported every
     *  process blocked.  A thousand kills of consumers blocked in or
     *  around waitForChange; afterwards critical: must still answer, no
     *  consumer may be left waiting, and the Mutex must be free.
     */
    if (write_file(storm,
            "| m consumers producer killer stop kills fin okCritical live rnd alive |\n"
            "m := Monitor new. stop := false. kills := 0. fin := Semaphore new. "
            "rnd := Random new. consumers := OrderedCollection new.\n"
            "alive := [:i | [[stop] whileFalse: [m critical: [m waitForChange]]] "
            "forkAt: Processor userSchedulingPriority].\n"
            "1 to: 20 do: [:i | consumers add: (alive value: i)].\n"
            "producer := [[stop] whileFalse: [m critical: [m signal]. "
            "Processor yield]] fork.\n"
            "killer := [1 to: 1000 do: [:k | | idx | (Delay forMilliseconds: 1) wait. "
            "idx := (rnd next * consumers size) truncated + 1.\n"
            "    (consumers at: idx) terminate. kills := kills + 1. "
            "consumers at: idx put: (alive value: idx)]. fin signal] fork.\n"
            "fin wait.\n"
            "okCritical := false. [m critical: [okCritical := true]. fin signal] fork. "
            "[(Delay forSeconds: 3) wait. fin signal] fork. fin wait.\n"
            "stop := true. 1 to: 60 do: [:i | m critical: [m signalAll]. "
            "(Delay forMilliseconds: 5) wait].\n"
            "live := consumers count: [:p | p suspendedContext notNil].\n"
            "^{kills. okCritical. live. m hasWaiters. (m instVarAt: 1) owner}\n")
        == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", storm);
        status = serve(batch, 8, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL FIXES-2: could not run the server\n");
        } else {
            expect(out, "==> (1000 true 0 false nil )",
                   "FIXES-2 a terminate storm leaves the Monitor whole");
            expect_absent(out, "sent to a UndefinedObject",
                          "FIXES-2 no nil among the waiters");
            expect_absent(out, "every process is blocked", "FIXES-2 no deadlock");
        }
    }
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
     *  found it, and it is the check; the count only has to show that a
     *  handler resumed.  Not every signal reaches it: one sent while the
     *  handler is still running from the last finds it disabled, as a
     *  running handler is, and takes Warning's default action -- under
     *  ThreadSanitizer three of twenty did.
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
            "^(done = 300000 and: [count > 0]) "
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
     *  re-armed.  Four million objects, the table's starting size; the
     *  smaller ceilings that used to stop before a first OutOfMemory are
     *  checked in bugs5_low_om.  About a minute, so not under a sanitizer.
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
                 "ST_MAX_OBJECTS=4194304 " ST_TEST_TIMEOUT " -k 2 180 %s "
                 "-serve %s -workers 4 \"$(cat %s)\" 2>&1", st2026, IMAGE, BATCH);
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

    bugs5_low_om();
    bugs5_low_sched();
    bugs5_low_kern();
    bugs5_low_comp();
    bugs5_low_net();
    bugs5_low_files();
    bugs5_low_docs();
    bugs6_critical();
    bugs6_high_om();
    bugs6_high_sched();
    bugs6_high_comp();
    bugs6_high_netgui();
    bugs6_high_files();
    bugs6_high_kern();

    unlink(IMAGE);
    unlink(BATCH);
    unlink(STARTUP);
    unlink(RACE);
    unlink(SHARED);
    fixtures_remove();
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

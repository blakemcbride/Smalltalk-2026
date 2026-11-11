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
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "st_socket.h"              /*  NET_MAX_SOCKETS  */

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
 *  half way, which reported a hang that was only a slow binary.  Forty
 *  minutes under a sanitizer: the OutOfMemory-ceiling checks allocate a
 *  million objects four times over, which the thread sanitizer runs in
 *  twenty-five.
 */
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__) \
 || (defined(__has_feature) && (__has_feature(thread_sanitizer) \
                             || __has_feature(address_sanitizer)))
#define SERVE_SECONDS   "2400"
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
static struct { const char *name; char *path; } fixtures_made[160];

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
     *  free collected for ever without reaching its OutOfMemory.  A
     *  million: two runaways caught, a few seconds.  The ceiling has to
     *  clear the bootstrap's table high-water mark, which a loaded image
     *  needs whole, and that mark moves in steps: the bootstrap collects
     *  each time its fresh allocations double, so the mark sits just
     *  under a power of two until the library grows past it and then
     *  jumps by the next phase's allocations.  A quarter of a million,
     *  the lowest ceiling accepted, was passed by the Bugs6 critical
     *  fixes; half a million, by the three methods of KERNB-4 (the mark
     *  went from 524,044 to 566,316 and the image could not be loaded
     *  under the ceiling at all).
     */
    if (write_file(BATCH,
            "| a r1 r2 | r1 := [a := OrderedCollection new. [a add: (Array "
            "new: 1)] repeat] on: OutOfMemory do: [:e | e return: a size]. "
            "a := nil. Smalltalk garbageCollect. r2 := [a := OrderedCollection "
            "new. [a add: (Array new: 1)] repeat] on: OutOfMemory do: [:e | e "
            "return: a size]. a := nil. (r1 between: 100000 and: 1048576) & "
            "(r2 between: 100000 and: 1048576) ifTrue: ['survived twice'] "
            "ifFalse: [{r1. r2}]\n") == 0) {
        snprintf(command, sizeof command,
                 "ST_MAX_OBJECTS=1048576 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s "
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
    expect(out, "ls build/mt/st2026 build/mt-*/st2026",
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

/*
 *  Bugs6, three medium findings in the object memory.
 */
static void
bugs6_medium_om(void)
{
    static char out[65536];
    const char *rss   = fixture("bugs6-serve-rss.st");
    const char *big   = fixture("bugs6-serve-five.bin");
    const char *oom   = fixture("bugs6-serve-oom3.st");
    const char *weak  = fixture("bugs6-serve-weak.st");
    const char *wimg  = fixture("bugs6-serve-weak.im");
    char        batch[1024];
    char        command[2048];
    int         status;

    /*
     *  OM-4: under workers a dead object's body was kept on the magazine
     *  whatever its size, and a small object reusing the entry pinned it:
     *  a hundred five-megabyte Strings dropped and collected left the
     *  process at its high-water mark for good.  Bodies over a page go
     *  back to the allocator when their object dies; the resident size
     *  after the drop must be within 100 MB of the size before.
     */
    /*
     *  Not under a sanitizer: ASAN keeps freed memory in a quarantine and
     *  TSAN in its own allocator, so the resident size does not come back
     *  whatever the collector gave up.
     */
#if !(defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__) \
   || (defined(__has_feature) && (__has_feature(thread_sanitizer) \
                               || __has_feature(address_sanitizer))))
    snprintf(command, sizeof command,
             "dd if=/dev/urandom of=%s bs=1M count=5 2>/dev/null", big);
    (void) run(command, out, sizeof out);
    snprintf(batch, sizeof batch,
             "| rss keep before after |\n"
             "rss := [ | s line v | s := FileStream oldFileNamed: '/proc/self/status'. "
             "v := nil. [s atEnd] whileFalse: [line := s nextLine. "
             "(line beginsWith: 'VmRSS') ifTrue: [v := (line copyFrom: 7 to: line size - 3) "
             "asInteger]]. s close. v].\n"
             "before := rss value. keep := OrderedCollection new.\n"
             "1 to: 100 do: [:i | keep add: (FileStream oldFileNamed: '%s') "
             "contentsOfEntireFile].\n"
             "keep := nil. Smalltalk garbageCollect. Smalltalk garbageCollect.\n"
             "1 to: 300000 do: [:i | Array new: 3]. Smalltalk garbageCollect.\n"
             "after := rss value.\n"
             "^after - before < 102400 ifTrue: ['memory came back'] "
             "ifFalse: [{before. after}]\n", big);
    if (write_file(rss, batch) == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", rss);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL OM-4: could not run the server\n");
        } else
            expect(out, "==> 'memory came back'", "OM-4 dead bodies are given back");
    }
#endif

    /*
     *  OM-5: the emergency reserve was lifted once for the whole image, so
     *  the second process to reach the ceiling while the first's handler
     *  unwound found nothing to release and stopped the image, handler or
     *  no handler.  Four runaways at once, each handling OutOfMemory, must
     *  all be caught.  A million, as the runaway check above.
     */
    if (write_file(oom,
            "| fin rs |\n"
            "fin := Semaphore new. rs := Array new: 4.\n"
            "1 to: 4 do: [:w | [ | a | rs at: w put: ([a := OrderedCollection new. "
            "[a add: (Array new: 1)] repeat] on: OutOfMemory do: [:e | e return: #caught]). "
            "a := nil. fin signal] fork].\n"
            "4 timesRepeat: [fin wait].\n"
            "^rs\n") == 0) {
        snprintf(command, sizeof command,
                 "ST_MAX_OBJECTS=1048576 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s "
                 "-serve %s -workers 8 \"$(cat %s)\" 2>&1",
                 st2026, IMAGE, oom);
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", oom);
        if (write_file(BATCH, batch) == 0) {
            snprintf(command, sizeof command,
                     "ST_MAX_OBJECTS=1048576 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s "
                     "-serve %s -workers 8 \"$(cat %s)\" 2>&1",
                     st2026, IMAGE, BATCH);
            status = run(command, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL OM-5: could not run the server\n");
            } else {
                expect(out, "==> (caught caught caught caught )",
                       "OM-5 four handled runaways");
                expect_absent(out, "out of memory activating",
                              "OM-5 the image did not stop");
            }
        }
    }

    /*
     *  OM-6: the Finalizer's mourn raced the owner's at:put: on one
     *  WeakKeyDictionary, so six per cent of dead keys were never removed
     *  and the queue once handed the Finalizer an Array.  Needs the weak
     *  classes, which are the pharo-weak profile's, so that image is
     *  built here; twenty thousand keys die while being added on eight
     *  workers, and once the size settles it must be zero.
     */
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/pharo-weak.profile "
             "-startup \"$(cat %s)\" -o %s 2>&1", st2026, STARTUP, wimg);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0) {
        ++st_test_failures;
        printf("  FAIL OM-6: cannot build the weak image (exit %d)\n", status);
    } else if (write_file(weak,
            "| d last same fin result |\n"
            "d := WeakKeyDictionary new. fin := Semaphore new. result := nil.\n"
            "[1 to: 20000 do: [:i | d at: Object new put: i. "
            "i \\\\ 2000 = 0 ifTrue: [Smalltalk garbageCollect]]. fin signal] fork.\n"
            "fin wait. last := -1. same := 0.\n"
            "1 to: 120 do: [:i | | s | result isNil ifTrue: [Smalltalk garbageCollect. "
            "(Delay forMilliseconds: 250) wait. s := d size.\n"
            "    s = last ifTrue: [same := same + 1] ifFalse: [same := 0]. last := s.\n"
            "    same >= 8 ifTrue: [result := {s. d keys size. "
            "((d instVarAt: 2) select: [:x | x notNil]) size}]]].\n"
            "^result\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", weak);
        if (write_file(BATCH, batch) == 0) {
            snprintf(command, sizeof command,
                     ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s "
                     "-serve %s -workers 8 \"$(cat %s)\" 2>&1",
                     st2026, wimg, BATCH);
            status = run(command, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL OM-6: could not run the weak server\n");
            } else {
                expect(out, "==> (0 0 0 )", "OM-6 every dead key is mourned");
                expect_absent(out, "mourning", "OM-6 only associations are queued");
            }
        }
    }
}

/*
 *  Bugs6, the medium finding in the compiler's one shared dictionary.
 */
static void
bugs6_medium_kern(void)
{
    static char out[65536];
    const char *undeclared = fixture("bugs6-serve-undeclared.st");
    char        batch[1024];
    int         status;

    /*
     *  KERNB-6: eight workers compiling a hundred unknown names each.
     *  Undeclared is a plain Dictionary, and the compiler stored into it
     *  with no lock: two workers found the same empty slot, or one grew
     *  the table under the other, and every run kept 600-700 of the 800
     *  -- and a method compiled against a lost binding held an
     *  Association no declaration could ever adopt.  The store is under
     *  the globals lock now, and so are the class definitions that take
     *  bindings out of Undeclared; eight workers defining classes with a
     *  class variable each run alongside, and every name is where it
     *  should be.  Short names, since each declaration says so on the
     *  Transcript and all of it has to fit the buffer.
     */
    if (write_file(undeclared,
            "| done lost |\n"
            "done := Semaphore new. lost := 0.\n"
            "1 to: 8 do: [:w | [1 to: 100 do: [:i | Compiler evaluate: "
            "'KbU', w printString, 'x', i printString, ' isNil']. done signal] fork].\n"
            "1 to: 8 do: [:w | [1 to: 50 do: [:i | [Object subclass: "
            "('KbC', w printString, 'x', i printString) asSymbol "
            "instanceVariableNames: '' classVariableNames: "
            "'KbV', w printString, 'x', i printString "
            "poolDictionaries: '' category: 'Bugs6-KbProbe'] "
            "on: Error do: [:e | lost := lost + 1000. e return: nil]]. done signal] fork].\n"
            "1 to: 16 do: [:i | done wait].\n"
            "1 to: 8 do: [:w | 1 to: 100 do: [:i | (Undeclared includesKey: "
            "('KbU', w printString, 'x', i printString) asSymbol) "
            "ifFalse: [lost := lost + 1]]].\n"
            "1 to: 8 do: [:w | 1 to: 50 do: [:i | (Smalltalk includesKey: "
            "('KbC', w printString, 'x', i printString) asSymbol) "
            "ifFalse: [lost := lost + 1]]].\n"
            "^'undeclared lost ', lost printString\n") == 0) {
        int     run_index;

        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", undeclared);
        for (run_index = 0; run_index < 2; ++run_index) {
            status = serve(batch, 8, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL KERNB-6: could not run the server\n");
                break;
            }
            expect(out, "==> 'undeclared lost 0'",
                   "KERNB-6 eight workers compiling unknown names");
            expect_absent(out, "every process is blocked",
                          "KERNB-6 the two locks are taken in one order");
        }
    }
}

/*
 *  Bugs6, the file findings among the medium ones that need a snapshot.
 */
static void
bugs6_medium_files(void)
{
    static char out[65536];
    const char *held_probe = fixture("bugs6-serve-files3.st");
    const char *held       = fixture("bugs6-files3-held.txt");
    const char *held_snap  = fixture("bugs6-files3-snap");
    const char *src_probe  = fixture("bugs6-serve-files5.st");
    const char *src_snap   = fixture("bugs6-files5-snap");
    const char *src_copy   = fixture("bugs6-files5-copy");
    char        image[1024];
    char        changes[1024];
    char        copy[1024];
    char        batch[512];
    char        command[4096];
    char        text[4096];
    int         status;

    /*
     *  FILES-3.  A stream written and not flushed when a snapshot is
     *  taken keeps its bytes: the snapshot releases every open stream,
     *  and release writes out what it holds first.  Before: the file was
     *  empty, in the process that went on as much as in the image saved.
     */
    snprintf(text, sizeof text,
        "| f |\n"
        "f := FileStream fileNamed: '%s'.\n"
        "f nextPutAll: 'hello world'.\n"
        "(Smalltalk snapshotAs: '%s' thenQuit: false) ifFalse: [^'resumed'].\n"
        "f close.\n"
        "^(FileStream oldFileNamed: '%s') contentsOfEntireFile\n",
        held, held_snap, held);
    snprintf(image, sizeof image, "%s.im", held_snap);
    snprintf(changes, sizeof changes, "%s.changes", held_snap);
    unlink(held);
    if (write_file(held_probe, text) == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", held_probe);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL FILES-3: could not run the server\n");
        } else
            expect(out, "==> 'hello world'",
                   "FILES-3 a stream written across a snapshot");
    }
    unlink(held);
    unlink(image);
    unlink(changes);

    /*
     *  FILES-5.  A method compiled before snapshotAs: has its source in
     *  the image that resumes: the snapshot copies the changes file
     *  beside the image, as saveAs: does.  And an image copied with cp,
     *  which has no changes file of its own, gets a copy of the one it
     *  names at its first load, rather than an empty one.  Before, both
     *  read 'apshotAs: ...' -- the doit logged at that offset of the
     *  empty file the VM had made -- or `position out of bounds'.
     */
    snprintf(text, sizeof text,
        "Object compile: 'zorkA ^''source-of-zorkA''' classified: #probe "
        "notifying: nil.\n"
        "(Smalltalk snapshotAs: '%s' thenQuit: true) ifTrue: [^'saved'].\n"
        "^'zorkA source ', ([(Object compiledMethodAt: #zorkA) getSource "
        "asString = 'zorkA ^''source-of-zorkA'''] on: Error do: [:e | "
        "e messageText]) printString\n",
        src_snap);
    snprintf(image, sizeof image, "%s.im", src_snap);
    snprintf(changes, sizeof changes, "%s.changes", src_snap);
    snprintf(copy, sizeof copy, "%s.im", src_copy);
    unlink(image);
    unlink(changes);
    if (write_file(src_probe, text) == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n", src_probe);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL FILES-5: could not run the server\n");
        } else {
            ++st_test_checks;
            if (access(changes, R_OK) != 0) {
                ++st_test_failures;
                printf("  FAIL FILES-5: the snapshot made no %s\n", changes);
            }
            snprintf(command, sizeof command,
                     ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
                     " %s -serve %s -workers 2 2>&1",
                     st2026, image);
            status = run(command, out, sizeof out);
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL FILES-5: could not run the saved image\n");
            } else
                expect(out, "==> 'zorkA source true'",
                       "FILES-5 a method's source after snapshotAs:");
            /*  The cp case: the saved image under a name with nothing beside it.  */
            snprintf(command, sizeof command, "cp %s %s", image, copy);
            status = run(command, out, sizeof out);
            if (status == 0) {
                snprintf(command, sizeof command,
                         ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
                         " %s -serve %s -workers 2 2>&1",
                         st2026, copy);
                status = run(command, out, sizeof out);
                ++st_test_checks;
                if (status < 0) {
                    ++st_test_failures;
                    printf("  FAIL FILES-5: could not run the copied image\n");
                } else
                    expect(out, "==> 'zorkA source true'",
                           "FILES-5 a method's source in a copy of the image");
            }
        }
    }
    unlink(image);
    unlink(changes);
    snprintf(changes, sizeof changes, "%s.im.changes", src_snap);
    unlink(changes);
    unlink(copy);
    snprintf(changes, sizeof changes, "%s.im.changes", src_copy);
    unlink(changes);
    snprintf(changes, sizeof changes, "%s.changes", src_copy);
    unlink(changes);
}

/*
 *  Bugs6 FIXES-1 (medium) and COMP-3 (low-medium): the `^' through ensure:
 *  at the top of -eval, and the doctest run that could not fail.  Both are
 *  the binary's own paths, so both are run as commands.
 */
static void
bugs6_medium_eval(void)
{
    static char out[65536];
    const char *mixed = fixture("bugs6-comp3-mixed.class.st");
    const char *clean = fixture("bugs6-comp3-clean.class.st");
    const char *lacks = fixture("bugs6-comp3-lacks.class.st");
    char        command[4096];
    int         status;

    /*
     *  FIXES-1.  The body of -eval is the bottom of its process, and a
     *  `^' from a block with an ensure: or ifCurtailed: between it and
     *  that body answers the value, as `[^9] value. 5' always did.
     *  Before: `Error: 'a primitive has failed'', then 5, exit 1.
     */
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-eval '[[^9] ensure: [1]] value. 5' 2>/dev/null", st2026);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0 || strcmp(out, "9\n") != 0) {
        ++st_test_failures;
        printf("  FAIL FIXES-1 -eval ^ through ensure: exit %d, output '%s', "
               "want 9\n", status, out);
    }
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-eval '[:x | [^x] ifCurtailed: [1]] value: 9. 5' 2>/dev/null",
             st2026);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0 || strcmp(out, "9\n") != 0) {
        ++st_test_failures;
        printf("  FAIL FIXES-1 -eval ^ through ifCurtailed: exit %d, "
               "output '%s', want 9\n", status, out);
    }

    /*
     *  COMP-3.  -doctests tells a broken example -- one that raises, or
     *  does not compile -- from one that needs a selector this image has
     *  not got, names each with its reason, reads several examples out of
     *  one comment, lets an example end in a period or hold the separator
     *  in a string, and exits 1 unless every example passed.  Before, the
     *  five that were not `3 + 4 >>> 7' all counted as needing something
     *  not here, and the exit status was 0.
     */
    if (write_file(mixed,
            "Class {\n\t#name : 'Bugs6Dt',\n\t#superclass : 'Object',\n"
            "\t#category : 'Bugs6-Dt'\n}\n\n"
            "Bugs6Dt >> a [\n\t\"3 + 4 >>> 7\"\n\t^1\n]\n\n"
            "Bugs6Dt >> b [\n\t\"3 zork >>> 8\"\n\t^1\n]\n\n"
            "Bugs6Dt >> c [\n\t\"3 + >>> 7\"\n\t^1\n]\n\n"
            "Bugs6Dt >> d [\n\t\"3 + 4 >>> 7.\"\n\t^1\n]\n\n"
            "Bugs6Dt >> e [\n\t\"3 + 4 >>> 7. 2 + 2 >>> 4\"\n\t^1\n]\n\n"
            "Bugs6Dt >> f [\n\t\"(Error signal: 'boom') >>> 5\"\n\t^1\n]\n\n"
            "Bugs6Dt >> g [\n\t\"('a>>>b' size) >>> 5.\n\t 3 + 0.5 >>> 3.5\"\n\t^1\n]\n")
        == 0) {
        snprintf(command, sizeof command,
                 "%s -bootstrap -profile profiles/st2026.profile "
                 "-doctests %s 2>&1", st2026, mixed);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status != 1) {
            ++st_test_failures;
            printf("  FAIL COMP-3 -doctests with broken examples exited %d, "
                   "want 1\n", status);
        }
        expect(out, "9 doctests in 7 methods of 1 files: 6 passed, 0 wrong, "
                    "1 need something not here, 2 broken",
               "COMP-3 the four counts");
        expect(out, "missing: 3 zork  (", "COMP-3 a missing selector named");
        expect(out, "Bugs6Dt): Message not understood: zork",
               "COMP-3 a missing selector's reason");
        expect(out, "broken: (Error signal: 'boom')  (",
               "COMP-3 an example that raises named");
        expect(out, "Bugs6Dt): boom", "COMP-3 the exception's text");
        expect(out, "broken: 3 +  (", "COMP-3 an example that does not compile");
        expect_absent(out, "wrong:", "COMP-3 nothing counted wrong");
    }
    if (write_file(clean,
            "Class {\n\t#name : 'Bugs6Dt',\n\t#superclass : 'Object',\n"
            "\t#category : 'Bugs6-Dt'\n}\n\n"
            "Bugs6Dt >> a [\n\t\"3 + 4 >>> 7. (3 @ 4) >>> 3@4.\"\n\t^1\n]\n")
        == 0) {
        snprintf(command, sizeof command,
                 "%s -bootstrap -profile profiles/st2026.profile "
                 "-doctests %s 2>&1", st2026, clean);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status != 0) {
            ++st_test_failures;
            printf("  FAIL COMP-3 -doctests with every example passing "
                   "exited %d, want 0\n", status);
        }
        expect(out, "2 doctests in 1 methods of 1 files: 2 passed, 0 wrong, "
                    "0 need something not here, 0 broken",
               "COMP-3 two examples in one comment");
    }
    if (write_file(lacks,
            "Class {\n\t#name : 'Bugs6Dt',\n\t#superclass : 'Object',\n"
            "\t#category : 'Bugs6-Dt'\n}\n\n"
            "Bugs6Dt >> a [\n\t\"3 zork >>> 7\"\n\t^1\n]\n")
        == 0) {
        snprintf(command, sizeof command,
                 "%s -bootstrap -profile profiles/st2026.profile "
                 "-doctests %s 2>&1", st2026, lacks);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status != 1) {
            ++st_test_failures;
            printf("  FAIL COMP-3 -doctests needing a selector exited %d, "
                   "want 1\n", status);
        }
    }
    unlink(mixed);
    unlink(clean);
    unlink(lacks);
}

/*
 *  A profile in the fixture directory that adds one package directory to
 *  st2026: what the bootstrap and the survey are asked about below.
 */
static void
write_probe_profile(const char *path, const char *name, const char *package)
{
    char    cwd[2048];
    char    text[4096];

    if (!getcwd(cwd, sizeof cwd))
        cwd[0] = '\0';
    /*
     *  Absolute, both of them: a profile's paths are relative to the
     *  profile's own directory, and the test directory is named relative
     *  to the tree.
     */
    snprintf(text, sizeof text,
             "Profile {\n\t#name : '%s',\n\t#requires : [ '%s/profiles/st2026' ],\n"
             "\t#dialect : 'closures',\n\t#packages : [ '%s%s%s' ]\n}\n",
             name, cwd, package[0] == '/' ? "" : cwd,
             package[0] == '/' ? "" : "/", package);
    write_file(path, text);
}

/*
 *  Bugs6, the low findings that are the binary's own paths: SCHED-5,
 *  COMP-4, COMP-9, COMP-10, COMP-11, COMP-15 and COMP-16.
 */
static void
bugs6_low_vm(void)
{
    static char out[65536];
    static char text[8192];
    const char *dir_a    = fixture("bugs6-comp9a");
    const char *dir_b    = fixture("bugs6-comp9b");
    const char *dir_c    = fixture("bugs6-comp9c");
    const char *dir_lp   = fixture("bugs6-comp10");
    const char *dir_ck   = fixture("bugs6-comp15");
    const char *dir_lit  = fixture("bugs6-comp16");
    const char *prof_a   = fixture("bugs6-comp9a.profile");
    const char *prof_b   = fixture("bugs6-comp9b.profile");
    const char *prof_c   = fixture("bugs6-comp9c.profile");
    const char *prof_lp  = fixture("bugs6-comp10.profile");
    const char *prof_ck  = fixture("bugs6-comp15.profile");
    char        file[1024];
    char        file2[1024];
    char        command[4096];
    int         status;
    unsigned    i;
    size_t      n;

    /*
     *  SCHED-5.  An -eval the scheduler stopped for want of anything to
     *  run exits 1 and says so; it printed the verdict, then nil, and
     *  exited 0.
     */
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-eval 'Semaphore new wait. 1' 2>&1", st2026);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 1) {
        ++st_test_failures;
        printf("  FAIL SCHED-5 a blocked -eval exited %d, want 1\n", status);
    }
    expect(out, "every process is blocked", "SCHED-5 the verdict");
    expect(out, "the expression did not finish: every process was blocked",
           "SCHED-5 the exit's reason");

    /*
     *  SCHED-5, found alongside by the thread sanitizer: the idle loop's
     *  five-minute backstop gave the verdict while another worker was
     *  still computing -- one process allocating for six minutes under
     *  TSAN, three workers idle, and the image ended under it as blocked,
     *  the dump showing interpreter 0 executing.  The backstop now counts
     *  only while nobody runs Smalltalk.  ST_IDLE_BACKSTOP_SECONDS brings
     *  it within reach of a test: two seconds, and a six-second loop on
     *  one of four workers must finish, with no verdict printed.
     */
    if (write_file(BATCH,
            "| start x | start := Time millisecondClockValue. x := 0. "
            "[Time millisecondClockValue - start < 6000] whileTrue: [x := x + 1]. "
            "'busy done'\n") == 0) {
        snprintf(command, sizeof command,
                 "ST_IDLE_BACKSTOP_SECONDS=2 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
                 " %s -serve %s -workers 4 \"$(cat %s)\" 2>&1",
                 st2026, IMAGE, BATCH);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL SCHED-5 backstop: could not run the server\n");
        } else {
            expect(out, "==> 'busy done'", "SCHED-5 one worker busy past the backstop");
            expect_absent(out, "every process is blocked",
                          "SCHED-5 no verdict while a worker runs");
        }
    }

    /*  COMP-4.  -eval prints true as true.  */
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-eval '^3 < 4' 2>/dev/null", st2026);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0 || strcmp(out, "true\n") != 0) {
        ++st_test_failures;
        printf("  FAIL COMP-4 -eval '^3 < 4': exit %d, output '%s', want true\n",
               status, out);
    }

    /*
     *  COMP-9.  A bootstrap syntax error names the file line it is on:
     *  the same bad line alone, after a three-line comment, and after a
     *  two-line string.  Before: 16, 17 and 18 for 14, 17 and 17.
     */
    mkdir(dir_a, 0755);
    mkdir(dir_b, 0755);
    mkdir(dir_c, 0755);
    snprintf(file, sizeof file, "%s/Lc.class.st", dir_a);
    write_file(file,
        "Class {\n\t#name : 'Lc',\n\t#superclass : 'Object',\n\t#category : 'Lc'\n}\n\n"
        "{ #category : 'x' }\nLc >> good [\n\t^1\n]\n\n"
        "{ #category : 'x' }\nLc >> bad [\n\t^1 foo: )\n]\n");
    snprintf(file, sizeof file, "%s/Lc.class.st", dir_b);
    write_file(file,
        "Class {\n\t#name : 'Lc',\n\t#superclass : 'Object',\n\t#category : 'Lc'\n}\n\n"
        "{ #category : 'x' }\nLc >> good [\n\t^1\n]\n\n"
        "{ #category : 'x' }\nLc >> bad [\n\t\"one\n\t two\n\t three\"\n\t^1 foo: )\n]\n");
    snprintf(file, sizeof file, "%s/Lc.class.st", dir_c);
    write_file(file,
        "Class {\n\t#name : 'Lc',\n\t#superclass : 'Object',\n\t#category : 'Lc'\n}\n\n"
        "{ #category : 'x' }\nLc >> good [\n\t^1\n]\n\n"
        "{ #category : 'x' }\nLc >> bad [\n\t| s |\n\ts := 'one\n\t two'.\n\t^1 foo: )\n]\n");
    write_probe_profile(prof_a, "zzlca", dir_a);
    write_probe_profile(prof_b, "zzlcb", dir_b);
    write_probe_profile(prof_c, "zzlcc", dir_c);
    snprintf(command, sizeof command,
             "%s -bootstrap -profile %s -eval '^1' 2>&1", st2026, prof_a);
    run(command, out, sizeof out);
    expect(out, "Lc.class.st:14: in Lc: unexpected token",
           "COMP-9 the bad line alone");
    snprintf(command, sizeof command,
             "%s -bootstrap -profile %s -eval '^1' 2>&1", st2026, prof_b);
    run(command, out, sizeof out);
    expect(out, "Lc.class.st:17: in Lc: unexpected token",
           "COMP-9 after a three-line comment");
    snprintf(command, sizeof command,
             "%s -bootstrap -profile %s -eval '^1' 2>&1", st2026, prof_c);
    run(command, out, sizeof out);
    expect(out, "Lc.class.st:17: in Lc: unexpected token",
           "COMP-9 after a two-line string");

    /*
     *  COMP-10.  A method pattern longer than 511 bytes is read whole by
     *  the C reader: eleven keywords, a 209-byte selector, 571 bytes in
     *  all, with the 512th byte inside an argument name.
     */
    mkdir(dir_lp, 0755);
    snprintf(file, sizeof file, "%s/Lp.class.st", dir_lp);
    n = (size_t) snprintf(text, sizeof text,
             "Class {\n\t#name : 'Lp',\n\t#superclass : 'Object',\n"
             "\t#category : 'Lp'\n}\n\n{ #category : 'x' }\nLp >> ");
    for (i = 0; i < 11; ++i)
        n += (size_t) snprintf(text + n, sizeof text - n,
                               "k%02uwwwwwwwwwwwwwww: a%02uxxxxxxxxxxxxxxxxxxxxxxxxxxxx%s",
                               i, i, i < 10 ? " " : "");
    snprintf(text + n, sizeof text - n,
             " [\n\t^a00xxxxxxxxxxxxxxxxxxxxxxxxxxxx\n]\n");
    write_file(file, text);
    write_probe_profile(prof_lp, "zzlp", dir_lp);
    snprintf(command, sizeof command, "%s -syntax %s 2>&1", st2026, file);
    status = run(command, out, sizeof out);
    expect(out, "1 files, 1 methods, 1 compiled, 0 failed",
           "COMP-10 the survey reads the whole pattern");
    snprintf(command, sizeof command,
             "%s -bootstrap -profile %s -eval '^((Smalltalk at: #Lp) selectors "
             "collect: [:s | s numArgs]) printString' 2>&1", st2026, prof_lp);
    run(command, out, sizeof out);
    expect(out, "a Set(11 )", "COMP-10 the bootstrap installs eleven arguments");

    /*  COMP-11.  Counts are decimal: 010 is ten, and 0x8 is not a number.  */
    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 20 %s -serve %s -workers 010 /dev/null 2>&1",
             st2026, IMAGE);
    run(command, out, sizeof out);
    expect(out, "on 10 workers", "COMP-11 -workers 010 is ten");
    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 20 %s -serve %s -workers 0x8 /dev/null 2>&1",
             st2026, IMAGE);
    run(command, out, sizeof out);
    expect(out, "not `0x8'", "COMP-11 -workers 0x8 is refused");

    /*
     *  COMP-15.  A chunk-format class comment written after a method
     *  category is kept.  The same directory holds a chunk file with a
     *  three-character binary selector, for COMP-16 below.
     */
    mkdir(dir_ck, 0755);
    snprintf(file, sizeof file, "%s/Ck4.st", dir_ck);
    write_file(file,
        "Object subclass: #Ck4\n  instanceVariableNames: ''\n  classVariableNames: ''\n"
        "  poolDictionaries: ''\n  category: 'Ck'!\n\n!Ck4 methodsFor: 'x'!\nfoo\n\t^1! !\n\n"
        "Ck4 comment: 'after the methods, no bang'!\n");
    snprintf(file2, sizeof file2, "%s/Bin.st", dir_ck);
    write_file(file2,
        "Object subclass: #Bin\n  instanceVariableNames: ''\n  classVariableNames: ''\n"
        "  poolDictionaries: ''\n  category: 'Ck'!\n\n!Bin methodsFor: 'x'!\nthree\n\t^3 <=> 4! !\n");
    write_probe_profile(prof_ck, "zzck", dir_ck);
    snprintf(command, sizeof command,
             "%s -bootstrap -profile %s -eval '^(Smalltalk at: #Ck4) comment printString' 2>&1",
             st2026, prof_ck);
    run(command, out, sizeof out);
    expect(out, "'after the methods, no bang'", "COMP-15 the comment after the methods");

    /*
     *  COMP-16.  The survey counts literals as the bootstrap will -- a
     *  method of seventy distinct Symbols fails, one of seventy of the
     *  same Symbol passes -- and under -profile reads each file in the
     *  dialect the profile gives it: a chunk file with a three-character
     *  binary selector fails on its own and passes in a closures profile.
     */
    mkdir(dir_lit, 0755);
    snprintf(file, sizeof file, "%s/Lit.class.st", dir_lit);
    n = (size_t) snprintf(text, sizeof text,
             "Class {\n\t#name : 'Lit',\n\t#superclass : 'Object',\n"
             "\t#category : 'Lit'\n}\n\n{ #category : 'x' }\nLit >> many [\n\t^{");
    for (i = 0; i < 70; ++i)
        n += (size_t) snprintf(text + n, sizeof text - n, "#s%u%s", i,
                               i < 69 ? ". " : "");
    snprintf(text + n, sizeof text - n, "}\n]\n");
    write_file(file, text);
    snprintf(command, sizeof command, "%s -syntax %s 2>&1", st2026, file);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 1) {
        ++st_test_failures;
        printf("  FAIL COMP-16 a 70-Symbol method surveyed with exit %d, want 1\n",
               status);
    }
    expect(out, "more than 63 literals referenced", "COMP-16 the ceiling is counted");
    snprintf(file, sizeof file, "%s/Lit2.class.st", dir_lit);
    n = (size_t) snprintf(text, sizeof text,
             "Class {\n\t#name : 'Lit2',\n\t#superclass : 'Object',\n"
             "\t#category : 'Lit'\n}\n\n{ #category : 'x' }\nLit2 >> same [\n\t^{");
    for (i = 0; i < 70; ++i)
        n += (size_t) snprintf(text + n, sizeof text - n, "#same%s",
                               i < 69 ? ". " : "");
    snprintf(text + n, sizeof text - n, "}\n]\n");
    write_file(file, text);
    snprintf(command, sizeof command, "%s -syntax %s 2>&1", st2026, file);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0) {
        ++st_test_failures;
        printf("  FAIL COMP-16 seventy of one Symbol surveyed with exit %d, want 0\n",
               status);
    }
    snprintf(command, sizeof command, "%s -syntax %s 2>&1", st2026, file2);
    run(command, out, sizeof out);
    expect(out, "1 files, 1 methods, 0 compiled, 1 failed",
           "COMP-16 a chunk file alone is Blue Book");
    snprintf(command, sizeof command, "%s -syntax -profile %s 2>&1", st2026, prof_ck);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0) {
        ++st_test_failures;
        printf("  FAIL COMP-16 the closures profile's survey exited %d, want 0\n",
               status);
    }
    expect(out, " 0 failed", "COMP-16 the profile's dialect for a chunk file");
}

/*
 *  Bugs6 KERNA-24 to KERNA-30, KERNA-32 and KERNA-33: the chronology
 *  findings, which are pharo/System-Time's and so the pharo-time profile's.
 *  Each doIt is run by the binary under that profile, two of them under a
 *  named time zone, since a Date's zone is the host's.
 */
static void
bugs6_low_chron(void)
{
    static char out[65536];
    char        command[4096];

    /*
     *  KERNA-24 and KERNA-25.  A four-digit year is the year it says, the
     *  century rule applies only to one or two digits; leap years agree
     *  with the Julian-day arithmetic.
     */
    snprintf(command, sizeof command,
             "TZ=America/Chicago %s -bootstrap -profile profiles/pharo-time.profile -eval "
             "\"^{(DateAndTime fromString: '0050-01-02T00:00:00Z') printString. "
             "((DateAndTime fromString: (DateAndTime year: 50 month: 1 day: 2 hour: 0 "
             "minute: 0 second: 0 offset: Duration zero) printString) year). "
             "((Date readFrom: '0050-01-02' pattern: 'yyyy-mm-dd') year). "
             "((Date readFrom: '50-01-02' pattern: 'yy-mm-dd') year). "
             "((Date readFrom: '50-01-02' pattern: 'y-mm-dd') year). "
             "((Date readFrom: '2050-01-02' pattern: 'y-mm-dd') year). "
             "(DateAndTime year: 0 month: 3 day: 1) - (DateAndTime year: 0 month: 2 day: 1). "
             "Year isLeapYear: 0. Year isLeapYear: -1. Year isLeapYear: 2024. "
             "Year isLeapYear: 1900. Year isLeapYear: 2000. "
             "[(Date year: -1 month: 2 day: 29) printString] on: Error do: [:e | 'refused']. "
             "(Year year: -1) daysInYear. (DateAndTime year: -1 month: 12 day: 31) dayOfYear} "
             "printString\" 2>&1", st2026);
    run(command, out, sizeof out);
    expect(out, "('0050-01-02T00:00:00+00:00' 50 50 2050 2050 2050 29:00:00:00 "
                "true false true false true 'refused' 365 365 )",
           "KERNA-24 and KERNA-25 years and leap years");

    /*
     *  KERNA-26, 27, 28 and 30.  A weekday for a negative Julian day,
     *  working days Monday to Friday, a Time equal to no Duration, and
     *  the deprecated methods doing their work.
     */
    snprintf(command, sizeof command,
             "TZ=America/Chicago %s -bootstrap -profile profiles/pharo-time.profile -eval "
             "\"^{(DateAndTime year: -4714 month: 1 day: 1) dayOfWeek. "
             "(DateAndTime year: -5000 month: 6 day: 15) asDate weekday. "
             "(DateAndTime year: 2026 month: 10 day: 9) dayOfWeekName. "
             "((Timespan starting: (DateAndTime year: 2026 month: 10 day: 5) duration: 7 days) "
             "workDates collect: [:d | d weekday]) asArray. "
             "(Time seconds: 5) = (Duration seconds: 5). (Duration seconds: 5) = (Time seconds: 5). "
             "(Time seconds: 5) = (Time seconds: 5). "
             "[(DateAndTime now + 60) class name] on: Error do: [:e | e messageText]. "
             "['0:01:00:00' asDuration printString] on: Error do: [:e | e messageText]. "
             "['2026-10-09T00:00:00Z' asDateAndTime year] on: Error do: [:e | e messageText]} "
             "printString\" 2>&1", st2026);
    run(command, out, sizeof out);
    expect(out, "(3 Friday Friday (Monday Tuesday Wednesday Thursday Friday ) false false "
                "true DateAndTime '0:01:00:00' 2026 )",
           "KERNA-26, 27, 28 and 30");

    /*
     *  KERNA-29 and KERNA-33.  A Date from a day count is Pharo's UTC
     *  midnight, as Pharo's DateTest pins it, but it is a whole number of
     *  days from a Date by name west of Greenwich; the Unix time of an
     *  instant before the epoch is its floor.
     *
     *  Under TZ=CST6, a zone six hours west with no daylight-saving rule
     *  (Bugs07 DOCS-1).  This ran under America/Chicago and expected
     *  `-05:00', the offset of the October day it was written: Pharo's
     *  DateAndTime stamps an instant, even one in 1969, with TODAY's local
     *  offset (Time class>>localOffsetSeconds asks about now, as Pharo's
     *  LocalTimeZone does), so from November to March the same doIt
     *  answers `-06:00' and `make test' failed every winter with nothing
     *  wrong.  What the check is about -- the epoch floor and a Date a
     *  whole day west of Greenwich -- holds under any western offset; a
     *  zone with no rule answers the same on every day of the year.
     */
    snprintf(command, sizeof command,
             "TZ=CST6 %s -bootstrap -profile profiles/pharo-time.profile -eval "
             "\"^{(Date fromDays: 0) start printString. "
             "(Date fromDays: 45000) subtractDate: (Date year: 1901 month: 1 day: 1). "
             "(Date year: 1901 month: 1 day: 1) subtractDate: (Date fromDays: 45000). "
             "(Date fromDays: 0) printString. "
             "(DateAndTime fromString: '1969-12-31T23:59:59.5Z') asUnixTime. "
             "(DateAndTime fromString: '1900-12-31T23:59:59.5Z') asSeconds. "
             "(DateAndTime fromString: '1970-01-01T00:00:00.5Z') asUnixTime. "
             "(DateAndTime fromString: '1970-01-01T00:00:01Z') asUnixTime. "
             "(DateAndTime fromUnixTime: -1) printString} printString\" 2>&1", st2026);
    run(command, out, sizeof out);
    expect(out, "('1901-01-01T00:00:00+00:00' 45000 -45000 '1 January 1901' "
                "-1 -1 0 1 '1969-12-31T17:59:59-06:00' )",
           "KERNA-29 and KERNA-33 under TZ=CST6");

    /*
     *  KERNA-32.  A negative half-hour zone prints its minutes unsigned.
     *  Under TZ=NST3:30, Newfoundland's standard offset with no daylight-
     *  saving rule, for the reason above (Bugs07 DOCS-1): America/St_Johns
     *  is LT-2:30 in summer and LT-3:30 in winter.
     */
    snprintf(command, sizeof command,
             "TZ=NST3:30 %s -bootstrap -profile profiles/pharo-time.profile -eval "
             "\"DateAndTime localTimeZone abbreviation\" 2>&1", st2026);
    run(command, out, sizeof out);
    expect(out, "\nLT-3:30\n", "KERNA-32 LT-3:30");
}

/*
 *  Bugs6 KERNB-9: eight workers restarting the finalizer at once leave one
 *  finalization process, and the one from before is gone.
 */
static void
bugs6_low_kernb(void)
{
    static char out[65536];
    const char *probe = fixture("bugs6-serve-kernb9.st");
    char        batch[1024];
    int         status;

    if (write_file(probe,
            "| done before after procs |\n"
            "done := Semaphore new. before := Finalizer process.\n"
            "1 to: 8 do: [:w | [Finalizer restart. done signal] fork]. "
            "1 to: 8 do: [:i | done wait].\n"
            "(Delay forMilliseconds: 200) wait. after := Finalizer process.\n"
            "procs := Process allInstances select: [:p | p suspendedContext notNil "
            "and: [p ~~ after and: [p ~~ before and: [p priority = Processor "
            "userInterruptPriority]]]].\n"
            "^'finalizers: ', procs size printString, ' before alive: ', "
            "before suspendedContext notNil printString\n") != 0)
        return;
    snprintf(batch, sizeof batch,
             "Compiler evaluate: (FileStream oldFileNamed: '%s') contentsOfEntireFile\n",
             probe);
    status = serve(batch, 8, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL KERNB-9: could not run the server\n");
    }  else
        expect(out, "==> 'finalizers: 0 before alive: false'",
               "KERNB-9 one finalization process after eight restarts");
    unlink(probe);
}

/*
 *  Bugs07, the virtual machine's findings: become:'s pin test, the
 *  receiver that changes shape under its method, the Message pushed onto a
 *  full frame, and the timer thread freeing TimingSemaphore.
 */
static void
bugs7_vm(void)
{
    static char out[65536];
    const char *pins  = fixture("bugs7-serve-pins.st");
    const char *loops = fixture("bugs7-serve-loops.st");
    char        batch[1024];
    char        command[2048];
    int         status;

    /*
     *  KERNB-1: become:'s pin test walked field 3 of ANY object as a
     *  process list and field 0 of each "link" as nextLink.  A Set or an
     *  OrderedCollection grows by become:, and an element whose first
     *  instance variable names the collection's holder made every grow a
     *  refused become: -- the Set stopped at 3 elements, the
     *  OrderedCollection at 10.  A process on a ready list is still
     *  refused, as Bugs6 OM-3 meant.
     */
    if (write_file(pins,
            "| holder r out item p |\n"
            "Object subclass: #Bugs7Holder instanceVariableNames: 'items name' "
            "classVariableNames: '' poolDictionaries: '' category: 'Bugs7'.\n"
            "Object subclass: #Bugs7Item instanceVariableNames: 'owner name' "
            "classVariableNames: '' poolDictionaries: '' category: 'Bugs7'.\n"
            "out := OrderedCollection new.\n"
            "holder := (Smalltalk at: #Bugs7Holder) new. "
            "holder instVarAt: 1 put: Set new.\n"
            "r := [1 to: 200 do: [:i | item := (Smalltalk at: #Bugs7Item) new. "
            "item instVarAt: 1 put: holder. (holder instVarAt: 1) add: item]. #ok] "
            "on: Error do: [:e | e return: #refused].\n"
            "out add: r; add: (holder instVarAt: 1) size.\n"
            "holder := (Smalltalk at: #Bugs7Holder) new. "
            "holder instVarAt: 1 put: OrderedCollection new.\n"
            "r := [1 to: 200 do: [:i | item := (Smalltalk at: #Bugs7Item) new. "
            "item instVarAt: 1 put: holder. (holder instVarAt: 1) addFirst: item]. "
            "#ok] on: Error do: [:e | e return: #refused].\n"
            "out add: r; add: (holder instVarAt: 1) size.\n"
            "p := [3 + 4] forkAt: Processor systemBackgroundPriority.\n"
            "out add: ([p become: Object new. #bad] on: Error do: [:e | #refused]).\n"
            "p terminate.\n"
            "^out asArray\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", pins);
        status = serve(batch, 1, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL KERNB-1: could not run the server\n");
        } else {
            expect(out, "==> (ok 200 ok 200 refused )",
                   "KERNB-1 collections whose elements name their holder grow");
            expect(out, "3 + 4 ==> 7", "KERNB-1 the image survived");
        }
    }

    /*
     *  KERNB-1: an element whose first instance variable is itself made
     *  the walk a cycle, run in C inside the safepoint with every other
     *  worker parked behind it: the image stopped, deaf to SIGTERM.
     */
    if (write_file(loops,
            "| s r item |\n"
            "Object subclass: #Bugs7Loop instanceVariableNames: 'next name' "
            "classVariableNames: '' poolDictionaries: '' category: 'Bugs7'.\n"
            "s := Set new.\n"
            "r := [1 to: 200 do: [:i | item := (Smalltalk at: #Bugs7Loop) new. "
            "item instVarAt: 1 put: item. s add: item]. #ok] "
            "on: Error do: [:e | e return: #refused].\n"
            "^Array with: r with: s size\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", loops);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL KERNB-1: could not run the server\n");
        } else {
            expect(out, "==> (ok 200 )",
                   "KERNB-1 a Set of self-referencing objects grows");
            expect(out, "3 + 4 ==> 7", "KERNB-1 the pool survived the cycle");
        }
    }

    /*
     *  KERNB-1: Pharo's AnnouncementSubscription -> Announcer ->
     *  SubscriptionRegistry -> its IdentitySet is such a chain, and an
     *  Announcer took six subscriptions.
     */
    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -bootstrap "
             "-profile profiles/pharo-announcements.profile -eval "
             "\"| ann sub r | ann := Announcer new. sub := Object new. "
             "r := [1 to: 50 do: [:i | ann when: Announcement do: [:a | 1] "
             "for: sub]. #ok] on: Error do: [:e | e return: #refused]. "
             "^{r. ann numberOfSubscriptions} printString\" 2>&1", st2026);
    run(command, out, sizeof out);
    expect(out, "\n(ok 50 )\n", "KERNB-1 an Announcer takes fifty subscriptions");

    /*
     *  KERNB-4: a method running on one worker while another reshapes its
     *  receiver's class.  A variable added in front of `d' made the
     *  running `d := d + 1' write into what was now `c' (28 million
     *  increments landed there); one removed made `d' read past the new
     *  body.  The old activation is abandoned with a CorruptMethod at its
     *  first moved field, and `c' is untouched.  A variable appended at
     *  the end moves nothing, and the same loop runs to the end.
     */
    {
        static const struct {
            const char *fields;
            const char *want;
            const char *what;
        } reshapes[] = {
            { "x a b c d", "==> (CorruptMethod 0 )",
              "KERNB-4 a field inserted under a running method" },
            { "b c d",     "==> (CorruptMethod 0 )",
              "KERNB-4 a field removed under a running method" },
            { "a b c d e", "==> (3000000 0 )",
              "KERNB-4 a field appended under a running method" },
        };
        size_t  k;

        for (k = 0; k < sizeof reshapes / sizeof reshapes[0]; ++k) {
            char    text[2048];

            snprintf(text, sizeof text,
                "| cls obj done r |\n"
                "Object subclass: #Bugs7R instanceVariableNames: 'a b c d' "
                "classVariableNames: '' poolDictionaries: '' category: 'Bugs7'.\n"
                "cls := Smalltalk at: #Bugs7R.\n"
                "cls compile: 'init a := 0. b := 0. c := 0. d := 0'.\n"
                "cls compile: 'spin: n | i | i := 0. [i < n] whileTrue: "
                "[d := d + 1. i := i + 1]. ^d'.\n"
                "obj := cls new init; yourself. done := Semaphore new.\n"
                "[r := [obj spin: 3000000] on: Error do: [:e | e class name]. "
                "done signal] fork.\n"
                "(Delay forMilliseconds: 20) wait.\n"
                "Object subclass: #Bugs7R instanceVariableNames: '%s' "
                "classVariableNames: '' poolDictionaries: '' category: 'Bugs7'.\n"
                "done wait.\n"
                "^Array with: r with: (obj instVarAt: "
                "((Smalltalk at: #Bugs7R) allInstVarNames indexOf: 'c'))\n",
                reshapes[k].fields);
            if (write_file(pins, text) != 0)
                continue;
            snprintf(batch, sizeof batch,
                     "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                     "contentsOfEntireFile\n3 + 4\n", pins);
            status = serve(batch, 4, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL KERNB-4: could not run the server\n");
            } else {
                expect(out, reshapes[k].want, reshapes[k].what);
                expect(out, "3 + 4 ==> 7", "KERNB-4 the pool survived");
            }
        }
    }

    /*
     *  KERNB-4 (INTERP-4): `self become:' a smaller object and then the
     *  method's own instance variables, in the same frame.  The cached
     *  bound was the old body's: the reads answered other objects' fields
     *  and the writes killed the image.  Both are refused now.
     */
    if (write_file(pins,
            "| cls r1 r2 |\n"
            "Object subclass: #Bugs7B instanceVariableNames: 'a b c d e f g h' "
            "classVariableNames: '' poolDictionaries: '' category: 'Bugs7'.\n"
            "cls := Smalltalk at: #Bugs7B.\n"
            "cls compile: 'shrinkAndRead self become: (Array new: 1). "
            "^Array with: e with: f with: g with: h'.\n"
            "cls compile: 'shrinkAndWrite self become: (Array new: 1). "
            "e := 42. f := 43. g := 44. h := 45'.\n"
            "r1 := [cls new shrinkAndRead] on: Error do: [:e | e class name].\n"
            "r2 := [cls new shrinkAndWrite. #wrote] on: Error do: [:e | e class name].\n"
            "Smalltalk garbageCollect.\n"
            "^Array with: r1 with: r2\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", pins);
        status = serve(batch, 2, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL KERNB-4: could not run the server\n");
        } else {
            expect(out, "==> (CorruptMethod CorruptMethod )",
                   "KERNB-4 self become: a smaller object");
            expect(out, "3 + 4 ==> 7", "KERNB-4 the pool survived the shrink");
        }
    }

    /*  The same with no workers at all, three thousand times over.  */
    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -bootstrap "
             "-profile profiles/st2026.profile -eval \"| c keep | "
             "c := Object subclass: #Bugs7F instanceVariableNames: "
             "'a b c d e f g h i j' classVariableNames: '' poolDictionaries: '' "
             "category: 'Bugs7'. c compile: 'poke: v self become: Object new. "
             "j := v. i := v. h := v'. keep := (1 to: 3000) collect: [:k | | o | "
             "o := c new. [o poke: k] on: Error do: [:e | nil]. o]. keep := nil. "
             "Smalltalk garbageCollect. ^#survived\" 2>/dev/null", st2026);
    /*  stdout only: each refused poke says so on stderr, and three
     *  thousand lines would push the answer past what run() keeps.  */
    status = run(command, out, sizeof out);
    expect(out, "survived\n", "KERNB-4 three thousand shrinks with no workers");

    unlink(pins);
    unlink(loops);
}

/*
 *  Bugs07 FILES-1 through RestServer: a COMMIT the database refuses is an
 *  error reply, and the pooled connection goes on to the next request
 *  with no transaction open.  The refusal is SQLite's for a deferred
 *  foreign key still violated at COMMIT.  Before: `('true' 'true' 'true'
 *  0 0 )' -- the client was told the orphan went in, the connection went
 *  back to the pool inside the failed transaction, the next request's
 *  valid row was written into it and lost with it, and was reported as
 *  committed too.  The backend is a fixture of its own, so that the
 *  shared tests/rest-backend is not changed under the tests that count
 *  its services.  Skipped where there is no SQLITE3 driver.
 */
static void
bugs7_lib_files1(void)
{
    static char out[65536];
    const char *backend = fixture("bugs7-files1-backend");
    const char *dbfile  = fixture("bugs7-files1-rest.db");
    const char *probe   = fixture("bugs7-serve-files1.st");
    char        services[1024], service[1100], text[2048], batch[1024];
    int         status;

    snprintf(services, sizeof services, "%s/services", backend);
    snprintf(service, sizeof service, "%s/Bugs7Tx.class.st", services);
    mkdir(backend, 0777);
    mkdir(services, 0777);
    unlink(dbfile);
    if (write_file(service,
            "Class {\n"
            "\t#name : 'Bugs7Tx',\n"
            "\t#superclass : 'RestService',\n"
            "\t#category : 'services'\n"
            "}\n\n"
            "{ #category : 'services' }\n"
            "Bugs7Tx >> reset [\n"
            "\tdb autoCommit: true.\n"
            "\tdb executeImmediate: 'DROP TABLE IF EXISTS ch'.\n"
            "\tdb executeImmediate: 'DROP TABLE IF EXISTS p'.\n"
            "\tdb executeImmediate: 'CREATE TABLE p (id INTEGER PRIMARY KEY)'.\n"
            "\tdb executeImmediate: 'CREATE TABLE ch (id INTEGER PRIMARY KEY, "
            "pid INTEGER REFERENCES p(id) DEFERRABLE INITIALLY DEFERRED)'.\n"
            "\tdb executeImmediate: 'PRAGMA foreign_keys=ON'.\n"
            "\tdb autoCommit: false\n"
            "]\n\n"
            "{ #category : 'services' }\n"
            "Bugs7Tx >> orphan [\n"
            "\tdb executeImmediate: 'INSERT INTO ch VALUES (1, 999)'\n"
            "]\n\n"
            "{ #category : 'services' }\n"
            "Bugs7Tx >> parent [\n"
            "\tdb executeImmediate: 'INSERT INTO p VALUES (6)'\n"
            "]\n") != 0)
        return;
    snprintf(text, sizeof text,
            "| server url call r c |\n"
            "c := [DbConnection open: 'DRIVER=SQLITE3;Database=%s;'] on: Error do: [:e | nil].\n"
            "c isNil ifTrue: [^'no sqlite driver']. c close.\n"
            "server := RestServer new port: 0; bindAddress: '127.0.0.1';\n"
            "  backendDirectory: '%s'; database: 'DRIVER=SQLITE3;Database=%s;';\n"
            "  connectionPoolSize: 1; requireAuthentication: false; name: 'bugs7'; yourself.\n"
            "server start.\n"
            "url := 'http://127.0.0.1:', server port printString, '/rest'.\n"
            "call := [:m | ((HttpClient new post: url\n"
            "  body: '{\"_class\":\"services.Bugs7Tx\",\"_method\":\"', m, '\"}'\n"
            "  contentType: 'application/json') json booleanAt: '_Success') printString].\n"
            "r := OrderedCollection new.\n"
            "r add: (call value: 'reset'); add: (call value: 'orphan'); add: (call value: 'parent').\n"
            "server stop.\n"
            "c := DbConnection open: 'DRIVER=SQLITE3;Database=%s;'.\n"
            "r add: ((c fetchAll: 'select count(*) as n from p') first at: 'n');\n"
            "  add: ((c fetchAll: 'select count(*) as n from ch') first at: 'n').\n"
            "c close.\n"
            "^r printString\n", dbfile, backend, dbfile, dbfile);
    if (write_file(probe, text) != 0)
        return;
    snprintf(batch, sizeof batch,
             "Compiler evaluate: (FileStream oldFileNamed: '%s') contentsOfEntireFile\n",
             probe);
    status = serve(batch, 2, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL FILES-1: could not run the server\n");
    } else if (strstr(out, "==> 'no sqlite driver'"))
        printf("  skipped FILES-1 (Bugs07): no SQLITE3 driver\n");
    else
        expect(out, "==> 'an OrderedCollection(''true'' ''false'' ''true'' 1 0 )'",
               "FILES-1 a refused COMMIT is an error reply, and the next request's is kept");
    unlink(probe);
    unlink(service);
    rmdir(services);
    rmdir(backend);
    unlink(dbfile);
}

/*
 *  Bugs07, the library group.  KERNB-2: the blocks handed to Smalltalk
 *  run after its lock is given back.  An ifAbsent: block that interned a
 *  Symbol held the globals lock while it took the Symbol lock, and a
 *  compile takes them the other way round, so four workers doing the one
 *  while four did the other stopped the image -- `every process is
 *  blocked; nothing can run', three runs in three.  The second half is
 *  at:ifAbsentPut: raced eight ways: every process gets the one value
 *  that was declared, the first, and nobody's raises.  test_image holds
 *  the one-thread checks.
 */
static void
bugs7_lib(void)
{
    static char out[65536];
    const char *probe = fixture("bugs7-serve-kernb2.st");
    char        batch[1024];
    int         status;

    if (write_file(probe,
            "| done n answers lazy same |\n"
            "done := Semaphore new. n := 0. answers := Array new: 8.\n"
            "1 to: 4 do: [:w | [1 to: 2000 do: [:i | Smalltalk at: #Bugs7KbNoSuchGlobal "
            "ifAbsent: [('bugs7kbsym', i printString, 'w', w printString) asSymbol]]. "
            "done signal] fork].\n"
            "1 to: 4 do: [:w | [1 to: 200 do: [:i | Compiler evaluate: '3 + 4']. "
            "done signal] fork].\n"
            "1 to: 8 do: [:i | done wait. n := n + 1].\n"
            "1 to: 8 do: [:w | [answers at: w put: ([Smalltalk at: #Bugs7KbLazy "
            "ifAbsentPut: [Object new]] on: Error do: [:e | e messageText]). "
            "done signal] fork].\n"
            "1 to: 8 do: [:i | done wait].\n"
            "lazy := Smalltalk at: #Bugs7KbLazy. same := true.\n"
            "answers do: [:x | x == lazy ifFalse: [same := false]].\n"
            "Smalltalk removeKey: #Bugs7KbLazy.\n"
            "^'finished ', n printString, ' same: ', same printString\n") != 0)
        return;
    snprintf(batch, sizeof batch,
             "Compiler evaluate: (FileStream oldFileNamed: '%s') contentsOfEntireFile\n",
             probe);
    status = serve(batch, 8, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL KERNB-2: could not run the server\n");
    } else {
        expect(out, "==> 'finished 8 same: true'",
               "KERNB-2 interning in an ifAbsent: block beside compiles");
        expect_absent(out, "every process is blocked", "KERNB-2 no deadlock");
    }
    unlink(probe);

    bugs7_lib_files1();
}

/*
 *  Bugs07, the scheduler's stop-and-wake findings: one probe file per
 *  check, evaluated by the batch harness on the number of workers given,
 *  and the answer looked for.  A hang is a failure here like any other:
 *  serve kills the server after SERVE_SECONDS and the answer is missing.
 */
static void
bugs7_probe(const char *name, const char *text, unsigned workers,
            const char *wanted, const char *what)
{
    static char out[65536];
    const char *probe = fixture(name);
    char        batch[1024];
    int         status;

    ++st_test_checks;
    if (write_file(probe, text) != 0) {
        ++st_test_failures;
        printf("  FAIL %s: could not write the probe\n", what);
        return;
    }
    snprintf(batch, sizeof batch,
             "Compiler evaluate: (FileStream oldFileNamed: '%s') "
             "contentsOfEntireFile\n3 + 4\n", probe);
    status = serve(batch, workers, out, sizeof out);
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL %s: could not run the server\n", what);
    }  else  {
        expect(out, wanted, what);
        expect(out, "3 + 4 ==> 7", what);
    }
    unlink(probe);
}

static void
bugs7_sched(void)
{
    /*
     *  SCHED-1: a stopper stopped part-way.  primDetach: left its target
     *  HELD until the stopper released it in an ensure: entered several
     *  bytecodes later (suspend had none), so a stopper terminated in
     *  between left the target held for good -- resume refused it as
     *  `running', every later terminate of it yielded and asked again for
     *  ever -- or, stopped a little later, let it go without resuming it:
     *  on no list, in nobody's hands, holding its Mutex.  Now the detach
     *  and everything done with its answer run where a stop of the
     *  stopper is put off until they are done, and terminate waits that
     *  out.  One worker: a stopper suspending and resuming its target,
     *  terminated every millisecond; nothing else can be running there,
     *  so a refused resume is a leaked hold.  The old binary leaked one
     *  within two kills.
     */
    bugs7_probe("bugs7-sched1-suspender.st",
        "| w k kills bad msg fin |\n"
        "fin := Semaphore new.\n"
        "[w := [[true] whileTrue: [Processor yield]] forkAt: 4.\n"
        " kills := 0. bad := 0.\n"
        " [kills >= 300 or: [bad > 0]] whileFalse: [\n"
        "\tk := [[true] whileTrue: [w suspend. w resume]] forkAt: 4.\n"
        "\t(Delay forMilliseconds: 1) wait.\n"
        "\tk terminate. kills := kills + 1.\n"
        "\tmsg := [w resume. #ok] on: Error do: [:e | e messageText].\n"
        "\tmsg = 'cannot resume a process that is running' "
        "ifTrue: [bad := bad + 1]].\n"
        " bad = 0 ifTrue: [w terminate].\n"
        " fin signal] forkAt: 6.\n"
        "fin wait.\n"
        "^'SCHED-1 held after ', kills printString, ' kills: ', "
        "bad printString\n",
        1, "==> 'SCHED-1 held after 300 kills: 0'",
        "SCHED-1 a terminated suspender leaves no hold");

    /*
     *  The terminating kind, on one worker and on eight: a stopper
     *  forking Mutex users and terminating each, itself terminated every
     *  millisecond.  Afterwards no victim may be held, and none may be
     *  parked by nobody -- alive, on no list, in no hands.  The old binary
     *  left one of the second kind, owning its Mutex, within two kills on
     *  either count.
     */
    {
        static const char *text =
            "| m k kills ws held susp fin |\n"
            "fin := Semaphore new.\n"
            "[kills := 0. held := 0. susp := 0.\n"
            " [kills >= 150 or: [held + susp > 0]] whileFalse: [\n"
            "\tws := Array new: 10000. m := Mutex new.\n"
            "\tk := [ | i | i := 0. [true] whileTrue: [ | w |\n"
            "\t\tw := [[true] whileTrue: [m critical: [Processor yield]]] "
            "forkAt: 4.\n"
            "\t\tws at: (i := i + 1) put: w. Processor yield. "
            "w terminate]] forkAt: 4.\n"
            "\t(Delay forMilliseconds: 1) wait.\n"
            "\tk terminate. kills := kills + 1.\n"
            "\tws do: [:w | | where |\n"
            "\t\t(w notNil and: [w suspendedContext notNil]) ifTrue: [\n"
            "\t\t\twhere := w primDetach: false.\n"
            "\t\t\twhere = 6 ifTrue: [held := held + 1].\n"
            "\t\t\t(where = 0 and: [w suspendedContext notNil]) "
            "ifTrue: [susp := susp + 1].\n"
            "\t\t\twhere ~= 6 ifTrue: [w primRelease: false. "
            "w terminate]]]].\n"
            " fin signal] forkAt: 6.\n"
            "fin wait.\n"
            "^'SCHED-1 stoppers killed ', kills printString, "
            "': victims held ', held printString, ', parked by nobody ', "
            "susp printString\n";

        bugs7_probe("bugs7-sched1-terminator.st", text, 1,
            "==> 'SCHED-1 stoppers killed 150: victims held 0, "
            "parked by nobody 0'",
            "SCHED-1 a terminated terminator, one worker");
        bugs7_probe("bugs7-sched1-terminator.st", text, 8,
            "==> 'SCHED-1 stoppers killed 150: victims held 0, "
            "parked by nobody 0'",
            "SCHED-1 a terminated terminator, eight workers");
    }

    /*
     *  A full held table answers 6, and the stopper yields and asks
     *  again.  It waited in C for a slot instead, holding its worker, so
     *  on one worker the holders could never run to free one and the
     *  image stopped: three hundred never-started processes detached and
     *  not released here fill it, an unrelated terminate must then wait,
     *  and must finish once they are released.
     */
    bugs7_probe("bugs7-sched1-full.st",
        "| ps answers x done before fin |\n"
        "fin := Semaphore new.\n"
        "[ps := (1 to: 300) collect: [:i | [Semaphore new wait] newProcess].\n"
        " answers := ps collect: [:p | p primDetach: true].\n"
        " x := [Semaphore new wait] forkAt: 4.\n"
        " done := false.\n"
        " [x terminate. done := true] forkAt: 5.\n"
        " (Delay forMilliseconds: 100) wait.\n"
        " before := done.\n"
        " ps do: [:p | p primRelease: false].\n"
        " (Delay forMilliseconds: 100) wait.\n"
        " fin signal] forkAt: 6.\n"
        "fin wait.\n"
        "^{(answers includes: 6). before. done. x suspendedContext isNil}\n",
        1, "==> (true false true true )",
        "SCHED-1 a full held table makes a stopper wait, not the image");

    /*
     *  And an entry counts what it holds.  Sixty processes held and never
     *  released, dropped, collected: their oops were reused and the new
     *  Process that got one was refused its first resume -- fourteen of
     *  two hundred thousand on the old binary.
     */
    bugs7_probe("bugs7-sched1-reuse.st",
        "| ps bad fin |\n"
        "fin := Semaphore new.\n"
        "[ps := (1 to: 60) collect: [:i | [Semaphore new wait] newProcess].\n"
        " ps do: [:p | p primDetach: true].\n"
        " ps := nil.\n"
        " Smalltalk garbageCollect.\n"
        " bad := 0.\n"
        " 1 to: 200000 do: [:i | | p | p := [nil] newProcess. "
        "p priority: 5.\n"
        "\t[p resume] on: Error do: [:e | bad := bad + 1]].\n"
        " fin signal] forkAt: 6.\n"
        "fin wait.\n"
        "^'SCHED-1 new processes refused: ', bad printString\n",
        1, "==> 'SCHED-1 new processes refused: 0'",
        "SCHED-1 a held process is not reclaimed");

    /*
     *  SCHED-3: two resumes of one suspended process at the same instant.
     *  Primitive 87 decided `parked and free' by reads with no lock and
     *  no claim, so two workers could both decide it and both resume it:
     *  queued twice, or -- outranking them, as here -- nominated on two
     *  workers and run by two native threads through one context.  Four
     *  resumers released together at a process that, once running, is
     *  busy for two milliseconds before it suspends itself again: at most
     *  one of the four can find it suspended.  The old binary ran it on
     *  two threads within a few hundred rounds -- its counter turned to
     *  nil, `an index past the shared-temporary vector', and the image
     *  stopped -- so the answer below never came.
     */
    bugs7_probe("bugs7-sched3-resume.st",
        "| p count rs oks go many rounds fin spin |\n"
        "fin := Semaphore new.\n"
        "count := 0. many := 0. rounds := 0.\n"
        "spin := [:ms | | end | end := Time millisecondClockValue + ms. "
        "[Time millisecondClockValue < end] whileTrue: []].\n"
        "p := [[true] whileTrue: [count := count + 1. spin value: 2. "
        "Processor activeProcess suspend]] forkAt: 5.\n"
        "spin value: 5.\n"
        "[1 to: 1500 do: [:i |\n"
        "\tgo := false. oks := Array new: 4.\n"
        "\trs := (1 to: 4) collect: [:k | [[go] whileFalse: []. "
        "oks at: k put: ([p resume. true] on: Error do: "
        "[:e | e return: false])] forkAt: 4].\n"
        "\tspin value: 1.\n"
        "\tgo := true.\n"
        "\tspin value: 4.\n"
        "\t(oks select: [:ok | ok == true]) size > 1 "
        "ifTrue: [many := many + 1].\n"
        "\trounds := rounds + 1].\n"
        " fin signal] forkAt: 6.\n"
        "fin wait.\n"
        "p terminate.\n"
        "^'SCHED-3 rounds ', rounds printString, ', two resumes accepted: ', "
        "many printString, ', count ', count class name\n",
        8, "==> 'SCHED-3 rounds 1500, two resumes accepted: 0, "
        "count SmallInteger'",
        "SCHED-3 one resume of a suspended process at a time");

    /*
     *  SCHED-2: only the holder lets go.  Primitive 235 released whatever
     *  it was given, whoever sent it, so a stopper's late second release
     *  took away the hold the next stopper had just taken; that one's
     *  own release then failed and left the victim parked by nobody in
     *  the middle of its critical: section, holding the Mutex.  Here a
     *  second process tries to let go of -- and then to resume -- a
     *  process the first holds: neither may touch the hold, so its own
     *  detach still answers 6, and the holder's release resumes the
     *  process.  The old binary let the second process take the hold
     *  away and then take its own (0), and the holder's release failed.
     */
    bugs7_probe("bugs7-sched2-holder.st",
        "| x b r done fin |\n"
        "fin := Semaphore new.\n"
        "[done := false.\n"
        " x := [done := true] newProcess.\n"
        " r := Array new: 6.\n"
        " r at: 1 put: (x primDetach: false).\n"
        " b := [r at: 2 put: ([x primRelease: false. #released] "
        "on: Error do: [:e | #failed]).\n"
        "\tr at: 3 put: ([x primRelease: true. #resumed] "
        "on: Error do: [:e | #failed]).\n"
        "\tr at: 4 put: (x primDetach: false).\n"
        "\t(r at: 4) = 0 ifTrue: [x primRelease: false]] forkAt: 5.\n"
        " (Delay forMilliseconds: 50) wait.\n"
        " r at: 5 put: ([x primRelease: true. #resumed] "
        "on: Error do: [:e | #failed]).\n"
        " (Delay forMilliseconds: 50) wait.\n"
        " r at: 6 put: done.\n"
        " fin signal] forkAt: 6.\n"
        "fin wait.\n"
        "^r\n",
        1, "==> (0 released failed 6 resumed true )",
        "SCHED-2 only the holder lets go");

    /*
     *  And the audit's own case: two processes terminating one that is
     *  running inside a Mutex section, a thousand times on eight workers.
     *  Both terminates return, the victim is gone, and the Mutex is free.
     *  The old binary raised `a primitive has failed' out of one of the
     *  terminates in some 44% of rounds and lost the Mutex in 28%.  (On
     *  two workers this probe proves nothing: the main loop spins on one
     *  and the victim never lets the terminators onto the other.)
     */
    bugs7_probe("bugs7-sched2-twoterm.st",
        "| p m t1 t2 go fin spin rounds failed alive lost msg1 msg2 |\n"
        "fin := Semaphore new. rounds := 0. failed := 0. alive := 0. "
        "lost := 0.\n"
        "spin := [:ms | | end | end := Time millisecondClockValue + ms. "
        "[Time millisecondClockValue < end] whileTrue: []].\n"
        "[1 to: 1000 do: [:i | | ok |\n"
        "\tm := Mutex new.\n"
        "\tp := [[true] whileTrue: [m critical: "
        "[1 to: 200 do: [:k | k * k]]]] forkAt: 4.\n"
        "\tspin value: 1.\n"
        "\tgo := false. msg1 := msg2 := nil.\n"
        "\tt1 := [[go] whileFalse: []. msg1 := [p terminate. nil] "
        "on: Error do: [:e | e return: e messageText]] forkAt: 4.\n"
        "\tt2 := [[go] whileFalse: []. msg2 := [p terminate. nil] "
        "on: Error do: [:e | e return: e messageText]] forkAt: 4.\n"
        "\tspin value: 1.\n"
        "\tgo := true.\n"
        "\tspin value: 5.\n"
        "\t(msg1 notNil or: [msg2 notNil]) ifTrue: [failed := failed + 1].\n"
        "\tp suspendedContext notNil ifTrue: [alive := alive + 1].\n"
        "\tok := false. [m critical: [ok := true]] forkAt: 4. "
        "spin value: 5.\n"
        "\tok ifFalse: [lost := lost + 1].\n"
        "\trounds := rounds + 1].\n"
        " fin signal] forkAt: 6.\n"
        "fin wait.\n"
        "^'SCHED-2 rounds ', rounds printString, ', a terminate raised: ', "
        "failed printString, ', victim alive: ', alive printString, "
        "', Mutex lost: ', lost printString\n",
        8, "==> 'SCHED-2 rounds 1000, a terminate raised: 0, "
        "victim alive: 0, Mutex lost: 0'",
        "SCHED-2 two terminators of a process inside a Mutex section");
}

/*
 *  Bugs07 FIXES-3: a stop put off to the end of a critical: section waits
 *  a grace for it, and no longer.  SCHED-6/7's deferral let a process
 *  caught RUNNING in any critical: finish the section first, which for a
 *  loop inside a user's `aMutex critical:' was never: terminate returned
 *  at once, the process ran on holding the lock, and a timeout signalled
 *  into it never fired.  Now terminate and signalException: wait for the
 *  section through DeferredStop's grace, and a section still running
 *  then is given up -- the process is unwound from outside, or the
 *  exception delivered where it stands -- while a section entered inside
 *  it, a library's, is still let finish.
 */
static void
bugs7b_sched(void)
{
    /*
     *  The audit's probe, as a check: a loop inside a Mutex's and inside a
     *  Semaphore's critical: is terminated, terminate returns with it
     *  gone, it counts no further, and the lock is free.  The old image:
     *  ((false false true true ) (false false true false )).
     */
    bugs7_probe("bugs7b-fixes3-terminate.st",
        "| m p n t q s r1 r2 free e1 e2 |\n"
        "m := Mutex new. n := 0.\n"
        "p := [m critical: [[true] whileTrue: [n := n + 1]]] fork.\n"
        "(Delay forMilliseconds: 100) wait.\n"
        "p terminate. e1 := p suspendedContext isNil.\n"
        "(Delay forMilliseconds: 200) wait. t := n. (Delay forMilliseconds: 200) wait.\n"
        "r1 := {e1. p suspendedContext isNil. n > t. m isHeld}.\n"
        "s := Semaphore forMutualExclusion. n := 0.\n"
        "q := [s critical: [[true] whileTrue: [n := n + 1]]] fork.\n"
        "(Delay forMilliseconds: 100) wait.\n"
        "q terminate. e2 := q suspendedContext isNil.\n"
        "(Delay forMilliseconds: 200) wait. t := n. (Delay forMilliseconds: 200) wait.\n"
        "free := false. [s critical: [free := true]] fork. (Delay forMilliseconds: 50) wait.\n"
        "r2 := {e2. q suspendedContext isNil. n > t. free}.\n"
        "^{r1. r2}\n",
        3, "==> ((true true false false ) (true true false true ) )",
        "FIXES-3 a loop inside a critical: section is terminated");

    /*
     *  A timeout signalled into such a loop fires, and the lock is given
     *  back; a Monitor's section is its Mutex's, given up once.  The old
     *  image: ((nil true false ) (false nil )).
     */
    bugs7_probe("bugs7b-fixes3-signal.st",
        "| m p n got mon q r |\n"
        "m := Mutex new. n := 0.\n"
        "p := [[m critical: [[true] whileTrue: [n := n + 1]]] on: Error do: [:e | got := e messageText]] fork.\n"
        "(Delay forMilliseconds: 100) wait.\n"
        "p signalException: (Error new messageText: #timeout).\n"
        "(Delay forMilliseconds: 300) wait.\n"
        "r := {got. m isHeld. p suspendedContext isNil}.\n"
        "mon := Monitor new. got := nil.\n"
        "q := [mon critical: [[true] whileTrue: [n := n + 1]]] fork.\n"
        "(Delay forMilliseconds: 100) wait.\n"
        "q terminate.\n"
        "[mon critical: [got := #in]] fork. (Delay forMilliseconds: 50) wait.\n"
        "^{r. {q suspendedContext isNil. got}}\n",
        3, "==> ((timeout false true ) (true in ) )",
        "FIXES-3 a timeout reaches a loop inside a critical: section");

    /*
     *  A short section is still let finish, and terminate now returns only
     *  when it has: one worker, the victim stopped inside its section on
     *  the ready list, 20 ms of work left in it.  The old image answered
     *  at once, with the section half done: (1 nil false true ).
     */
    bugs7_probe("bugs7b-fixes3-short.st",
        "| m p x y spin |\n"
        "spin := [:ms | | end | end := Time millisecondClockValue + ms. [Time millisecondClockValue < end] whileTrue: []].\n"
        "m := Mutex new.\n"
        "p := [m critical: [x := 1. Processor yield. spin value: 20. y := 2]] fork.\n"
        "Processor yield.\n"
        "p terminate.\n"
        "^{x. y. p suspendedContext isNil. m isHeld}\n",
        1, "==> (1 2 true false )",
        "FIXES-3 a short section is still let finish");

    /*
     *  And the library's own sections, entered from inside the loop that
     *  is given up, are not torn: a loop under a user's Mutex feeding a
     *  SharedQueue and sleeping on a Delay, killed ten times; each time
     *  the queue still hands over an item, the Mutex is free and the
     *  process gone.  The old image left it running seven times in ten.
     */
    bugs7_probe("bugs7b-fixes3-library.st",
        "| bad m q p fin |\n"
        "bad := 0. fin := Semaphore new.\n"
        "[1 to: 10 do: [:i | | got |\n"
        "\tm := Mutex new. q := SharedQueue new.\n"
        "\tp := [m critical: [[true] whileTrue: [q nextPut: i. q next. (Delay forMilliseconds: 1) wait]]] fork.\n"
        "\t(Delay forMilliseconds: 20) wait.\n"
        "\tp terminate.\n"
        "\tgot := nil.\n"
        "\t[[q isEmpty] whileFalse: [q next]. q nextPut: #probe. got := q next] fork.\n"
        "\t(Delay forMilliseconds: 30) wait.\n"
        "\t(p suspendedContext isNil and: [got == #probe and: [q isEmpty and: [m isHeld not]]])\n"
        "\t\tifFalse: [bad := bad + 1]].\n"
        " fin signal] fork.\n"
        "fin wait.\n"
        "^'FIXES-3 a killed loop that uses a SharedQueue and a Delay, 10 rounds, bad ', bad printString\n",
        3, "==> 'FIXES-3 a killed loop that uses a SharedQueue and a Delay, "
        "10 rounds, bad 0'",
        "FIXES-3 a library section inside a given-up one is not torn");
}

/*
 *  As bugs7_probe, on an image other than the st2026 one: the weak
 *  dictionaries are the pharo-weak profile's.
 */
static void
bugs7c_probe_on(const char *image, const char *name, const char *text,
                unsigned workers, const char *wanted, const char *what)
{
    static char out[65536];
    const char *probe = fixture(name);
    char        batch[1024];
    char        command[2048];
    int         status;

    ++st_test_checks;
    if (write_file(probe, text) != 0) {
        ++st_test_failures;
        printf("  FAIL %s: could not write the probe\n", what);
        return;
    }
    snprintf(batch, sizeof batch,
             "Compiler evaluate: (FileStream oldFileNamed: '%s') "
             "contentsOfEntireFile\n3 + 4\n", probe);
    if (write_file(BATCH, batch) != 0) {
        ++st_test_failures;
        printf("  FAIL %s: could not write the batch\n", what);
        unlink(probe);
        return;
    }
    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS " %s -serve %s -workers %u"
             " \"$(cat %s)\" 2>&1", st2026, image, workers, BATCH);
    status = run(command, out, sizeof out);
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL %s: could not run the server\n", what);
    }  else  {
        expect(out, wanted, what);
        expect(out, "3 + 4 ==> 7", what);
    }
    unlink(probe);
}

/*
 *  Bugs07, round 3: the scheduler group's findings -- Delay's lock across
 *  a snapshot (SCHED-6), the weak dictionaries' mourning (FIXES-2,
 *  FIXES-4) and pharo-time's timeout (KERNB-5).
 */
static void
bugs7c_sched(void)
{
    static char out[65536];
    char        text[4096];
    char        image[1024];
    char        changes[1024];
    char        command[2048];
    const char *snap = fixture("bugs7c-sched6-snap");
    const char *wimg = fixture("bugs7c-serve-weak.im");
    const char *timg = fixture("bugs7c-serve-time.im");
    int         status;

    /*
     *  SCHED-6: Delay class>>preSnapshot took AccessProtect with a bare
     *  wait, and the ifCurtailed: in snapshotAs:thenQuit: that gave it
     *  back was entered only after the queue walk and the cursor: a
     *  process terminated in between kept the lock, and every Delay in
     *  the image waited for ever.  One worker, so that the snapshotter
     *  is exactly where the probe puts it.  First it is handed the lock
     *  and terminated on its ready list before it runs again: the lock
     *  must come back (excess 1) and a Delay work.  Then it is
     *  terminated while still waiting, with the lock held here: it never
     *  got it and must not give it back (excess 0, then 1 once this
     *  process lets go).  Before: the first terminate kept the lock,
     *  the Delay never returned, and the image stopped with every
     *  process blocked.
     */
    snprintf(text, sizeof text,
        "| ap p r |\n"
        "ap := Delay classPool at: #AccessProtect. ap wait.\n"
        "p := [Smalltalk snapshotAs: '%s'] fork. Processor yield.\n"
        "r := OrderedCollection new. r add: p suspendedContext method == "
        "(Semaphore compiledMethodAt: #waitAndMark:).\n"
        "ap signal. p terminate. r add: (ap instVarAt: 3); add: p suspendedContext isNil.\n"
        "(Delay forMilliseconds: 5) wait.\n"
        "p := [Smalltalk snapshotAs: '%s'] fork. ap wait. Processor yield.\n"
        "p terminate. r add: (ap instVarAt: 3). ap signal. r add: (ap instVarAt: 3).\n"
        "(Delay forMilliseconds: 5) wait. r add: #delayWorked.\n"
        "^r asArray\n", snap, snap);
    bugs7_probe("bugs7c-sched6-terminated.st", text, 1,
        "==> (true 1 true 0 1 delayWorked )",
        "SCHED-6 a snapshot terminated with Delay's lock gives it back");

    /*
     *  And the queue still crosses a snapshot.  The resumption times are
     *  written on the writer's clock now and moved by postSnapshot: in
     *  the run that resumes, by the time the file sat on disk: a process
     *  waiting on a three-second Delay when the image was saved wakes in
     *  the resumed image within those three seconds, and a fresh Delay
     *  there works.  Saved without quitting, so that the run that wrote
     *  the file goes through postSnapshot: as well and still answers.
     */
    snprintf(text, sizeof text,
        "| s t0 |\n"
        "s := Semaphore new.\n"
        "[(Delay forMilliseconds: 3000) wait. s signal] fork.\n"
        "(Delay forMilliseconds: 100) wait.\n"
        "(Smalltalk snapshotAs: '%s' thenQuit: false) ifTrue: [^'saved'].\n"
        "t0 := Time millisecondClockValue. s wait.\n"
        "(Delay forMilliseconds: 20) wait.\n"
        "^{'resumed'. (Time millisecondClockValue - t0) < 3200. "
        "(Delay classPool at: #AccessProtect) instVarAt: 3}\n", snap);
    snprintf(image, sizeof image, "%s.im", snap);
    snprintf(changes, sizeof changes, "%s.changes", snap);
    unlink(image);
    bugs7_probe("bugs7c-sched6-save.st", text, 2, "==> 'saved'",
                "SCHED-6 an image saved with a Delay pending");
    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
             " %s -serve %s -workers 2 2>&1", st2026, image);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL SCHED-6: could not run the saved image\n");
    } else
        expect(out, "==> ('resumed' true 1 )",
               "SCHED-6 a pending Delay wakes in the resumed image");
    unlink(image);
    unlink(changes);
    snprintf(changes, sizeof changes, "%s.im.changes", snap);
    unlink(changes);

    /*
     *  The weak dictionaries are the pharo-weak profile's, so that image
     *  is built here for the checks that follow.
     */
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/pharo-weak.profile "
             "-startup \"$(cat %s)\" -o %s 2>&1", st2026, STARTUP, wimg);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0) {
        ++st_test_failures;
        printf("  FAIL FIXES-4: cannot build the weak image (exit %d)\n",
               status);
        return;
    }

    /*
     *  FIXES-4: the Finalizer removed a mourned entry from the live table
     *  -- a nil, then the rest of the cluster swapped back one entry at a
     *  time -- and an owner probing on another worker for a key further
     *  down the cluster stopped at the nil and answered absent, although
     *  the key was held the whole time.  The audit's probe: every key
     *  hashes alike, so all five hundred are one cluster; four hundred
     *  die each round, and a reader asks for the hundred that live until
     *  the size says the dead are mourned.  Before: misses in the
     *  thousands (20683 of 88600), never 0.  And the same with a
     *  WeakIdentityKeyDictionary, whose keys are made to fall in the
     *  first 64 slots of a table that does not grow: it had no lock at
     *  all, and missed 12880 of 58900.
     */
    bugs7c_probe_on(wimg, "bugs7c-fixes4-read.st",
        "| cls d keep fin misses reads |\n"
        "cls := Object subclass: #Bugs7cHKey instanceVariableNames: '' "
        "classVariableNames: '' poolDictionaries: '' category: 'Probe'.\n"
        "cls compile: 'hash ^1'.\n"
        "misses := 0. reads := 0.\n"
        "d := WeakKeyDictionary new.\n"
        "keep := (1 to: 100) collect: [:i | cls new].\n"
        "1 to: 20 do: [:r |\n"
        "\t1 to: 400 do: [:j | d at: cls new put: 2].\n"
        "\tkeep do: [:k | d at: k put: 1].\n"
        "\tfin := Semaphore new.\n"
        "\t[[d size > 100] whileTrue: [\n"
        "\t\tkeep do: [:k | reads := reads + 1. (d includesKey: k) "
        "ifFalse: [misses := misses + 1]]].\n"
        "\t fin signal] fork.\n"
        "\tSmalltalk garbageCollect.\n"
        "\tfin wait].\n"
        "^'FIXES-4 misses: ', misses printString, ' size: ', "
        "d size printString\n",
        4, "==> 'FIXES-4 misses: 0 size: 100'",
        "FIXES-4 a live key is never absent while a neighbour is mourned");
    bugs7c_probe_on(wimg, "bugs7c-fixes4-identity.st",
        "| d cap keep fin misses mk |\n"
        "misses := 0.\n"
        "d := WeakIdentityKeyDictionary new: 3000. cap := d capacity.\n"
        "mk := [ | o | [o := Object new. o identityHash \\\\ cap < 64] "
        "whileFalse. o].\n"
        "keep := (1 to: 100) collect: [:i | mk value].\n"
        "1 to: 10 do: [:r |\n"
        "\t1 to: 300 do: [:j | d at: mk value put: 2].\n"
        "\tkeep do: [:k | d at: k put: 1].\n"
        "\tfin := Semaphore new.\n"
        "\t[[d size > 100] whileTrue: [\n"
        "\t\tkeep do: [:k | (d at: k ifAbsent: [nil]) isNil "
        "ifTrue: [misses := misses + 1]]].\n"
        "\t fin signal] fork.\n"
        "\tSmalltalk garbageCollect.\n"
        "\tfin wait].\n"
        "^'FIXES-4 identity misses: ', misses printString, ' size: ', "
        "d size printString, ' same table size: ', "
        "(d capacity = cap) printString\n",
        4, "==> 'FIXES-4 identity misses: 0 size: 100 same table size: true'",
        "FIXES-4 the same for a WeakIdentityKeyDictionary");

    /*
     *  FIXES-2: each mourn took its entry out under the lock every weak
     *  dictionary's writers share, walking the rest of its cluster with
     *  Pharo's fixCollisionsFrom:, which probes every entry again from
     *  its home.  Thirty thousand dead keys in a table near its growth
     *  threshold made clusters thousands long; every removal cost
     *  seconds, and the owner's next at:put: -- the one that would have
     *  grown the table -- waited on the lock for all of them.  The
     *  audit's program: three hundred rounds of three hundred keys that
     *  die, with three thousand that live read back after each.  Before:
     *  five seconds on one worker, and on eight a stall the server's
     *  time limit kills.  Now a mourn marks its entry and each drain
     *  rebuilds the table once.
     */
    bugs7c_probe_on(wimg, "bugs7c-fixes2-perf.st",
        "| d keep fin |\n"
        "d := WeakKeyDictionary new.\n"
        "keep := (1 to: 3000) collect: [:i | Object new].\n"
        "keep do: [:k | d at: k put: 1].\n"
        "fin := Semaphore new.\n"
        "[1 to: 300 do: [:round |\n"
        "\t1 to: 300 do: [:j | d at: Object new put: 2].\n"
        "\tkeep do: [:k | d includesKey: k]]. fin signal] fork.\n"
        "fin wait.\n"
        "^'FIXES-2 finished, all kept: ', "
        "(keep allSatisfy: [:k | (d at: k ifAbsent: [0]) = 1]) printString\n",
        8, "==> 'FIXES-2 finished, all kept: true'",
        "FIXES-2 an owner's writes do not wait out the mourning");
    unlink(wimg);

    /*
     *  valueWithin:onTimeout: is the pharo-time profile's.
     */
    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/pharo-time.profile "
             "-startup \"$(cat %s)\" -o %s 2>&1", st2026, STARTUP, timg);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status != 0) {
        ++st_test_failures;
        printf("  FAIL KERNB-5: cannot build the time image (exit %d)\n",
               status);
        return;
    }

    /*
     *  KERNB-5: the watchdog tested the shared theProcess and then read
     *  it again to signal, while the caller's ensure: stored nil into it
     *  on another worker: work that finished as its time ran out had
     *  `nil signalException:' in the watchdog, or a TimedOut raised in
     *  the caller after the call had returned -- in its next critical:,
     *  in an unwind block.  The audit's probe: eight processes, three
     *  hundred two-millisecond waits under two-millisecond timeouts
     *  each, with an outer handler that records a TimedOut arriving
     *  outside the call.  Before: 73-223 of 2400 calls came back, the
     *  rest of each process lost to a late TimedOut.  And timeouts
     *  still time out, nested either way, and the work that ends in
     *  time still answers.
     */
    bugs7c_probe_on(timg, "bugs7c-kernb5-race.st",
        "| done late lock n |\n"
        "done := Semaphore new. late := OrderedCollection new.\n"
        "lock := Mutex new. n := 0.\n"
        "1 to: 8 do: [:w | [[[1 to: 300 do: [:i |\n"
        "\t[[(Delay forMilliseconds: 2) wait] valueWithinMilliseconds: 2 "
        "onTimeout: [nil]] on: Error do: [:e | e return: nil].\n"
        "\tlock critical: [n := n + 1]]]\n"
        "\t\ton: TimedOut do: [:e | lock critical: [late add: "
        "e signalContext sender printString]]]\n"
        "\t\tensure: [done signal]] fork].\n"
        "1 to: 8 do: [:i | done wait].\n"
        "^'KERNB-5 calls that returned: ', n printString, ' of 2400; late: ', "
        "late asArray printString\n",
        8, "==> 'KERNB-5 calls that returned: 2400 of 2400; late: ()'",
        "KERNB-5 a TimedOut never arrives after its valueWithin: returned");
    bugs7c_probe_on(timg, "bugs7c-kernb5-still.st",
        "| r t |\n"
        "r := OrderedCollection new. t := Time millisecondClockValue.\n"
        "r add: ([(Delay forMilliseconds: 500) wait. #done] "
        "valueWithinMilliseconds: 50 onTimeout: [#timedOut]).\n"
        "r add: (Time millisecondClockValue - t) < 300.\n"
        "r add: ([#fast] valueWithinMilliseconds: 500 onTimeout: [#timedOut]).\n"
        "r add: ([[(Delay forMilliseconds: 500) wait. #inner] "
        "valueWithinMilliseconds: 1000 onTimeout: [#innerT]] "
        "valueWithinMilliseconds: 50 onTimeout: [#outerT]).\n"
        "r add: ([[(Delay forMilliseconds: 500) wait. #inner] "
        "valueWithinMilliseconds: 50 onTimeout: [#innerT]] "
        "valueWithinMilliseconds: 1000 onTimeout: [#outerT]).\n"
        "^r asArray\n",
        4, "==> (timedOut true fast outerT innerT )",
        "KERNB-5 a timeout still times out");
    unlink(timg);
}

/*
 *  Bugs07, round 2: the VM group's medium findings that need a server.
 */
static void
bugs7b_vm(void)
{
    static char out[65536];
    const char *oom = fixture("bugs7b-serve-oom.st");
    char        batch[1024];
    char        command[2048];
    int         status;
    size_t      k;
    static const struct {
        const char *element;
        const char *what;
    } runaways[] = {
        { "[:x | x + i]", "INTERP-3 a runaway building closures" },
        { "{i. i}",       "INTERP-3 a runaway building brace Arrays" },
    };

    /*
     *  INTERP-3: a runaway that ran the object table out while building
     *  a closure (bytecode 143) or a brace Array (138) cleared the
     *  interpreter's running flag -- the worker ended, and with it the
     *  pool: the program's `on: Error do:' never ran, and the server
     *  exited with `the image stopped on its own'.  The same runaway
     *  allocating through a send was told.  Both bytecodes raise an
     *  OutOfMemory now, and the pool carries on.  A million entries, as
     *  the OM-5 check.
     */
    for (k = 0; k < sizeof runaways / sizeof runaways[0]; ++k) {
        char    text[1024];

        snprintf(text, sizeof text,
            "| c i r |\n"
            "c := Array new: 2000000. i := 1.\n"
            "r := [[true] whileTrue: [c at: i put: %s. i := i + 1]] "
            "on: Error do: [:e | c := nil. e class name].\n"
            "^Array with: r with: #after\n", runaways[k].element);
        if (write_file(oom, text) != 0)
            continue;
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", oom);
        if (write_file(BATCH, batch) != 0)
            continue;
        snprintf(command, sizeof command,
                 "ST_MAX_OBJECTS=1048576 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
                 " %s -serve %s -workers 4 \"$(cat %s)\" 2>&1",
                 st2026, IMAGE, BATCH);
        status = run(command, out, sizeof out);
        ++st_test_checks;
        if (status < 0) {
            ++st_test_failures;
            printf("  FAIL INTERP-3: could not run the server\n");
            continue;
        }
        expect(out, "==> (OutOfMemory after )", runaways[k].what);
        expect(out, "3 + 4 ==> 7", "INTERP-3 the pool carried on");
    }
    unlink(oom);

    /*
     *  INTERP-3, the doesNotUnderstand: half.  Its argument Array and its
     *  Message were never checked: at the ceiling the one that failed was
     *  oop 0, and the selector or the arguments were stored through it.
     *  A runaway that keeps every Message -- a doesNotUnderstand: that
     *  answers it -- reaches the ceiling at exactly those two allocations,
     *  since the context each one runs in is freed and taken again.  With
     *  no workers, where that is deterministic.
     */
    {
        static const char *const sends[] = { "zork", "zork: i" };

        for (k = 0; k < sizeof sends / sizeof sends[0]; ++k) {
            snprintf(command, sizeof command,
                     "ST_MAX_OBJECTS=1048576 " ST_TEST_TIMEOUT " -k 2 "
                     SERVE_SECONDS " %s -bootstrap -profile "
                     "profiles/st2026.profile -eval \"| cls z c i r | "
                     "cls := Object subclass: #Bugs7bEcho instanceVariableNames: '' "
                     "classVariableNames: '' poolDictionaries: '' "
                     "category: 'Bugs7b'. "
                     "cls compile: 'doesNotUnderstand: aMessage ^aMessage'. "
                     "z := cls new. c := Array new: 2000000. i := 1. "
                     "r := [[true] whileTrue: [c at: i put: (z %s). i := i + 1]] "
                     "on: Error do: [:e | c := nil. e class name]. "
                     "^(Array with: r with: i > 1000 with: #after) printString\" "
                     "2>&1", st2026, sends[k]);
            status = run(command, out, sizeof out);
            ++st_test_checks;
            if (status != 0) {
                ++st_test_failures;
                printf("  FAIL INTERP-3 a doesNotUnderstand: at the ceiling "
                       "(%s): exit %d\n", sends[k], status);
            }
            expect(out, "(OutOfMemory true after )",
                   "INTERP-3 a doesNotUnderstand: at the ceiling");
        }
    }
}

/*
 *  Bugs07, round 3: the GUI group's thread-safety findings.  Each is a
 *  piece of 1983 graphics state that every process shares and writes,
 *  and that one green process at a time never caught out; so each check
 *  is eight workers at once, each answering for what it alone did.
 */
static void
bugs7c_gui(void)
{
    /*
     *  GUI-3: every text scanner wrote its selectors into its FONT's
     *  stop-conditions Array, and the scanners disagree about the space
     *  slot -- #space for a composer, nil for a displayer or a hit test.
     *  Four workers compose a paragraph in the image's own style and
     *  compare the line starts with the ones composed alone first; four
     *  display one and find a character in it.  Before: about three
     *  compositions in four broke mid-word, and some twelve hundred
     *  displays died with `space' not understood or `perform: nil'.
     */
    bugs7_probe("bugs7c-gui3-stops.st",
        "| done text ref compose bad errs n |\n"
        "done := Semaphore new. n := 150.\n"
        "bad := Array new: 8 withAll: 0. errs := Array new: 8 withAll: 0.\n"
        "text := 'the quick brown fox jumps over the lazy dog and keeps on "
        "running past the old mill while nobody watches it go by at all' "
        "asText.\n"
        "compose := [Paragraph withText: text style: DefaultTextStyle\n"
        "\tcompositionRectangle: (0@0 extent: 120@1000) "
        "clippingRectangle: (0@0 extent: 120@1000)].\n"
        "ref := [:p | (1 to: p numberOfLines) collect: [:i | "
        "(p lineAt: i) first]] value: compose value.\n"
        "1 to: 8 do: [:w | w odd\n"
        "  ifTrue: [[1 to: n do: [:i | [ | p | p := compose value.\n"
        "\t((1 to: p numberOfLines) collect: [:k | (p lineAt: k) first]) "
        "= ref\n"
        "\t\tifFalse: [bad at: w put: (bad at: w) + 1]]\n"
        "\t\ton: Error do: [:e | errs at: w put: (errs at: w) + 1]]. "
        "done signal] fork]\n"
        "  ifFalse: [[ | p f | f := Form extent: 130@200. "
        "p := compose value.\n"
        "\t1 to: n * 2 do: [:i | [p displayOn: f. "
        "(p characterBlockForIndex: 40) stringIndex = 40\n"
        "\t\t\tifFalse: [bad at: w put: (bad at: w) + 1]]\n"
        "\t\ton: Error do: [:e | errs at: w put: (errs at: w) + 1]]. "
        "done signal] fork]].\n"
        "8 timesRepeat: [done wait].\n"
        "^'GUI-3 wrong ', (bad inject: 0 into: [:a :b | a + b]) "
        "printString,\n"
        "\t', errors ', (errs inject: 0 into: [:a :b | a + b]) "
        "printString\n",
        8, "==> 'GUI-3 wrong 0, errors 0'",
        "GUI-3 text composed and displayed on eight workers at once");

    /*
     *  GUI-4: Form>>valueAt: and valueAt:put: blitted through one 1x1
     *  Form in a class variable, two steps with nothing held between.
     *  Each worker reads a pixel of a form it shares with three others,
     *  black for the odd workers and white for the even, and writes the
     *  same pixel of a form of its own and reads the word back.  Before:
     *  about 25,000 of the 80,000 answers wrong.
     */
    bugs7_probe("bugs7c-gui4-onebit.st",
        "| done bad blk wht n |\n"
        "done := Semaphore new. n := 5000. bad := Array new: 8 withAll: 0.\n"
        "blk := Form extent: 8@8. blk black. wht := Form extent: 8@8. "
        "wht white.\n"
        "1 to: 8 do: [:w | [ | f e own |\n"
        "\tf := w odd ifTrue: [blk] ifFalse: [wht]. "
        "e := w odd ifTrue: [1] ifFalse: [0].\n"
        "\town := Form extent: 8@8.\n"
        "\t1 to: n do: [:i |\n"
        "\t\t(f valueAt: 3@3) = e ifFalse: [bad at: w put: (bad at: w) + 1].\n"
        "\t\town valueAt: 3@3 put: e.\n"
        "\t\t((own bits at: 4) bitAnd: 16r1000) = (e * 16r1000)\n"
        "\t\t\tifFalse: [bad at: w put: (bad at: w) + 1]].\n"
        "\tdone signal] fork].\n"
        "8 timesRepeat: [done wait].\n"
        "^'GUI-4 wrong ', (bad inject: 0 into: [:a :b | a + b]) "
        "printString\n",
        8, "==> 'GUI-4 wrong 0'",
        "GUI-4 pixels read and written on eight workers at once");

    /*
     *  GUI-5: the simulator the Debugger steps with ran a primitive for
     *  real by writing its number into one of five template methods in
     *  Object and sending it -- methods every process shares.  It writes
     *  into a copy of its own now.  Eight workers each simulate a block
     *  whose loop sends +, -, *, //, \\, max: and bitAnd:, and compare
     *  the answer with the block run for real.  With the shared templates
     *  put back the workers ran one another's primitives -- `Simulated
     *  message + not understood', eqv: sent to a SmallInteger -- and the
     *  probe never answered; before the GUI-5 fix nothing could be
     *  simulated at all.
     */
    bugs7_probe("bugs7c-gui5-simulate.st",
        "| done bad errs n blk |\n"
        "done := Semaphore new. n := 30.\n"
        "bad := Array new: 8 withAll: 0. errs := Array new: 8 withAll: 0.\n"
        "blk := [:k | | s | s := 0.\n"
        "\t1 to: 12 do: [:i | s := s + (i * k) - (i // 3) + (k \\\\ 5) "
        "max: (k bitAnd: 7)].\n"
        "\ts].\n"
        "1 to: 8 do: [:w | [1 to: n do: [:i | [ | got |\n"
        "\tgot := ContextPart runSimulated: [blk value: w * 7 + i].\n"
        "\tgot = (blk value: w * 7 + i) "
        "ifFalse: [bad at: w put: (bad at: w) + 1]]\n"
        "\t\ton: Error do: [:e | errs at: w put: (errs at: w) + 1]].\n"
        "\tdone signal] fork].\n"
        "8 timesRepeat: [done wait].\n"
        "^'GUI-5 wrong ', (bad inject: 0 into: [:a :b | a + b]) "
        "printString,\n"
        "\t', errors ', (errs inject: 0 into: [:a :b | a + b]) "
        "printString\n",
        8, "==> 'GUI-5 wrong 0, errors 0'",
        "GUI-5 blocks simulated on eight workers at once");
}

/*  ----------  Bugs07 round 3, NET: a server and a client, by hand  ----------
 *
 *  The checks above talk to the image through the batch harness, which is
 *  a server only in name.  The NET findings of this round are about what a
 *  real RestServer does when a client is in the middle of a request and
 *  the operating system says something -- a SIGTERM, a refused descriptor
 *  -- so these run the production startup (`RestServer serve') as its own
 *  process, speak HTTP to it from here over the loopback, and send the
 *  signal to the st2026 process itself.  Not through tools/timeout.sh:
 *  GNU timeout answers a SIGTERM by arming a SIGKILL for its -k grace,
 *  which would end a server that is doing exactly what it should, which
 *  is taking its time.  The deadlines are kept here instead.
 */

static int64_t
net_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
net_sleep_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

/*
 *  Start st2026 with `args' (NULL-terminated, after the binary's own
 *  name), its standard output and error in `log', its environment this
 *  process's plus `env' (NAME=VALUE, or NULL), and the soft and hard
 *  descriptor limits `soft' and `hard' where they are not zero.  The
 *  st2026 process's own id, or -1.
 */
static pid_t
net_spawn(const char *const *args, const char *log, const char *env,
          unsigned long soft, unsigned long hard)
{
    const char *argv[16];
    pid_t       pid;
    int         n;

    argv[0] = st2026;
    for (n = 0; args[n] && n < 14; ++n)
        argv[n + 1] = args[n];
    argv[n + 1] = NULL;
    /*
     *  The last run's log goes now, in this process: emptied by the child
     *  it would still be read here, whole, by a caller that got in first.
     */
    unlink(log);
    pid = fork();
    if (pid != 0)
        return pid;
    {
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd < 0)
            _exit(126);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }
    if (env)
        putenv((char *) env);
    if (soft || hard) {
        struct rlimit   rl;

        if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
            if (hard)
                rl.rlim_max = (rlim_t) hard;
            if (soft)
                rl.rlim_cur = (rlim_t) soft;
            if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
                _exit(125);
        }
    }
    execv(st2026, (char *const *) argv);
    _exit(127);
}

/*  The log as it stands, into a static buffer.  */
static const char *
net_log(const char *log)
{
    static char text[262144];
    FILE       *f = fopen(log, "r");
    size_t      n = 0;

    text[0] = '\0';
    if (!f)
        return text;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';
    return text;
}

static int
net_count(const char *text, const char *what)
{
    int     n = 0;

    while ((text = strstr(text, what)) != NULL) {
        ++n;
        text += strlen(what);
    }
    return n;
}

/*  Whether `what' appears `times' times in the log within `ms'.  */
static int
net_log_wait(const char *log, const char *what, int times, long ms)
{
    int64_t deadline = net_now_ms() + ms;

    for (;;) {
        if (net_count(net_log(log), what) >= times)
            return 1;
        if (net_now_ms() > deadline)
            return 0;
        net_sleep_ms(20);
    }
}

/*  The port from `listening on port N', or -1.  */
static int
net_log_port(const char *log)
{
    const char *at = strstr(net_log(log), "listening on port ");

    return at ? atoi(at + strlen("listening on port ")) : -1;
}

/*  A connection to 127.0.0.1:port, or -1 with errno.  */
static int
net_connect(int port)
{
    struct sockaddr_in  a;
    int                 fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;
    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_port        = htons((unsigned short) port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *) &a, sizeof a) != 0) {
        int saved = errno;

        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

/*  A /rest POST of `json', the connection to be closed after it.  */
static int
net_post(int fd, const char *json)
{
    char    request[4096];
    int     n = snprintf(request, sizeof request,
                         "POST /rest HTTP/1.1\r\nHost: x\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
                         strlen(json), json);
    int     sent = 0;

    while (sent < n) {
        ssize_t k = send(fd, request + sent, (size_t) (n - sent), MSG_NOSIGNAL);

        if (k <= 0)
            return -1;
        sent += (int) k;
    }
    return 0;
}

/*
 *  Everything the peer sends until it closes, or until `ms' have passed;
 *  NUL-terminated.  The byte count, or -1 for a timeout.
 */
static long
net_read_all(int fd, char *out, size_t len, long ms)
{
    int64_t deadline = net_now_ms() + ms;
    size_t  got = 0;

    out[0] = '\0';
    for (;;) {
        struct pollfd   p;
        int64_t         left = deadline - net_now_ms();
        ssize_t         k;

        if (left <= 0)
            return -1;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        if (poll(&p, 1, (int) left) <= 0)
            continue;
        k = recv(fd, out + got, len - 1 - got, 0);
        if (k <= 0)
            break;
        got += (size_t) k;
        out[got] = '\0';
        if (got >= len - 1)
            break;
    }
    return (long) got;
}

/*
 *  Wait up to `ms' for the process to end; its exit status, or 128 plus
 *  the signal that ended it.  -1 when it was still running and had to be
 *  killed.
 */
static int
net_reap(pid_t pid, long ms)
{
    int64_t deadline = net_now_ms() + ms;
    int     status;

    for (;;) {
        pid_t   r = waitpid(pid, &status, WNOHANG);

        if (r == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status)
                 : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        if (r < 0)
            return -1;
        if (net_now_ms() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }
        net_sleep_ms(20);
    }
}

static void
net_check(int ok, const char *what, const char *log)
{
    ++st_test_checks;
    if (!ok) {
        const char *text = log ? net_log(log) : "";
        size_t      n    = strlen(text);

        ++st_test_failures;
        printf("  FAIL %s\n", what);
        if (log)
            printf("       server log ends:\n%s\n", n > 1500 ? text + n - 1500 : text);
    }
}

/*
 *  The RestServer image of this round's NET checks: the production startup,
 *  `RestServer serve', baked in as the demo's is (Makefile, demo.im), with
 *  a back end of one slow service that says on the log when it has begun.
 *  Answers the port the server listens on, -1 when it did not come up.
 */
#define NET_RIMAGE      fixture("bugs7c-net-rest.im")
#define NET_BACKEND     fixture("bugs7c-net-backend")
#define NET_CONFIG      fixture("bugs7c-net.json")
#define NET_LOG         fixture("bugs7c-net.log")

static int
net_build_rest_image(void)
{
    static char out[65536];
    char        command[2048];
    char        path[1024];

    snprintf(command, sizeof command,
             "%s -bootstrap -profile profiles/st2026.profile "
             "-startup 'RestServer serve' -o %s 2>&1", st2026, NET_RIMAGE);
    if (run(command, out, sizeof out) != 0) {
        printf("  FAIL cannot build the RestServer image:\n%s\n", out);
        return -1;
    }
    snprintf(path, sizeof path, "%s/services", NET_BACKEND);
    snprintf(command, sizeof command, "mkdir -p %s", path);
    if (system(command) != 0)
        return -1;
    snprintf(path, sizeof path, "%s/services/Slow.class.st", NET_BACKEND);
    if (write_file(path,
            "Class { #name : 'Slow', #superclass : 'RestService', "
            "#category : 'services' }\n\n"
            "{ #category : 'services' }\n"
            "Slow >> wait [\n"
            "\tRestLog info: 'slow: started'.\n"
            "\t(Delay forMilliseconds: (injson integerAt: 'ms')) wait.\n"
            "\toutjson at: 'done' put: true\n]\n") != 0)
        return -1;
    snprintf(command, sizeof command,
             "{\"port\": 0, \"bindAddress\": \"127.0.0.1\", "
             "\"backendDirectory\": \"%s\", \"requireAuthentication\": false}\n",
             NET_BACKEND);
    return write_file(NET_CONFIG, command);
}

static void
net_remove_rest_image(void)
{
    char    command[1024];

    snprintf(command, sizeof command, "rm -rf %s", NET_BACKEND);
    (void) system(command);
}

/*
 *  Start the RestServer image on `config' (NET_CONFIG unless another is
 *  given); its pid, with *port set, or -1.
 */
static pid_t
net_start_rest_on(const char *config, const char *env, unsigned long soft,
                  unsigned long hard, int *port)
{
    const char *args[] = { "-serve", NET_RIMAGE, "-workers", "2", config, NULL };
    pid_t       pid = net_spawn(args, NET_LOG, env, soft, hard);

    *port = -1;
    if (pid < 0)
        return -1;
    if (!net_log_wait(NET_LOG, "info: rest: serving", 1, 60000)) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    *port = net_log_port(NET_LOG);
    return pid;
}

static pid_t
net_start_rest(const char *env, unsigned long nofile, int *port)
{
    return net_start_rest_on(NET_CONFIG, env, nofile, 0, port);
}

/*
 *  A request to the slow service on a connection of its own, answered only
 *  when the service's `slow: started' has appeared `nth' times: the file
 *  descriptor, or -1.
 */
static int
net_slow_request(int port, long ms, int nth)
{
    char    json[256];
    int     fd = net_connect(port);

    if (fd < 0)
        return -1;
    snprintf(json, sizeof json,
             "{\"_class\": \"services.Slow\", \"_method\": \"wait\", \"ms\": %ld}", ms);
    if (net_post(fd, json) != 0
     || !net_log_wait(NET_LOG, "slow: started", nth, 60000)) {
        close(fd);
        return -1;
    }
    return fd;
}

/*
 *  Bugs07 NET-5: SIGTERM to `-serve' cut every request in flight off with
 *  no reply.  The signal stopped every worker at its next bytecode check,
 *  so `RestServer serve' -- stop accepting, let the requests finish, quit
 *  -- never ran past its polling loop.  The first signal is now a request
 *  to an image that asks for one, and these check what that promises: the
 *  request in flight answered, the listener closed meanwhile, the image
 *  quitting by itself with exit code 0; a second signal and the grace
 *  period each still stopping it; and an image that never asks still
 *  stopped at once, as before.
 */
static void
bugs7c_net_stop(void)
{
    static char reply[65536];
    int         port;
    int         fd;
    int         status;
    int         refused;
    int64_t     t0;
    pid_t       pid;

    /*  The request in flight is answered, and only then does the image quit.  */
    pid = net_start_rest(NULL, 0, &port);
    net_check(pid > 0 && port > 0, "NET-5 the RestServer came up", NET_LOG);
    if (pid <= 0 || port <= 0)
        return;
    fd = net_slow_request(port, 10, 1);             /*  loads the class  */
    if (fd >= 0) {
        net_read_all(fd, reply, sizeof reply, 30000);
        close(fd);
    }
    fd = net_slow_request(port, 3000, 2);
    net_check(fd >= 0, "NET-5 the slow request began", NET_LOG);
    kill(pid, SIGTERM);
    t0 = net_now_ms();
    /*  The listener closes within a poll of the signal; the request does not.  */
    refused = 0;
    while (!refused && net_now_ms() - t0 < 2500) {
        int probe = net_connect(port);

        if (probe < 0 && errno == ECONNREFUSED)
            refused = 1;
        else {
            if (probe >= 0)
                close(probe);
            net_sleep_ms(50);
        }
    }
    net_check(refused, "NET-5 the listener is closed while the request finishes",
              NET_LOG);
    if (fd >= 0) {
        net_read_all(fd, reply, sizeof reply, 30000);
        close(fd);
    }
    net_check(fd >= 0 && strstr(reply, "\"done\":true") != NULL
                      && strstr(reply, "HTTP/1.1 200") != NULL,
              "NET-5 the request in flight at the SIGTERM is answered", NET_LOG);
    status = net_reap(pid, 30000);
    net_check(status == 0, "NET-5 the server exits 0 after the request", NET_LOG);
    net_check(strstr(net_log(NET_LOG), "info: stopping as asked") != NULL
           && strstr(net_log(NET_LOG), "st2026: the image quit") != NULL,
              "NET-5 RestServer serve stopped the server and quit", NET_LOG);

    /*  A second signal stops it at once, the request with it.  */
    pid = net_start_rest(NULL, 0, &port);
    if (pid > 0 && port > 0) {
        fd = net_slow_request(port, 20000, 1);
        kill(pid, SIGTERM);
        net_sleep_ms(300);
        t0 = net_now_ms();
        kill(pid, SIGTERM);
        status = net_reap(pid, 10000);
        net_check(status == 0 && net_now_ms() - t0 < 5000,
                  "NET-5 a second SIGTERM stops the server at once", NET_LOG);
        net_check(strstr(net_log(NET_LOG), "st2026: stopped as asked") != NULL,
                  "NET-5 ... and says it was asked", NET_LOG);
        if (fd >= 0)
            close(fd);
    } else
        net_check(0, "NET-5 the RestServer came up again", NET_LOG);

    /*
     *  The grace: two seconds of it, so RestServer gives its request one
     *  and quits without it, saying so -- and not twenty seconds later.
     */
    pid = net_start_rest("ST_STOP_GRACE_SECONDS=2", 0, &port);
    if (pid > 0 && port > 0) {
        fd = net_slow_request(port, 20000, 1);
        t0 = net_now_ms();
        kill(pid, SIGTERM);
        status = net_reap(pid, 15000);
        net_check(status == 0 && net_now_ms() - t0 < 8000,
                  "NET-5 a request longer than the grace does not keep the "
                  "server up", NET_LOG);
        net_check(strstr(net_log(NET_LOG), "1 request(s) still in flight") != NULL,
                  "NET-5 ... and RestServer says what it left", NET_LOG);
        if (fd >= 0)
            close(fd);
    } else
        net_check(0, "NET-5 the RestServer came up a third time", NET_LOG);

    /*
     *  An image that asked and then never quits is stopped when the grace
     *  runs out; one that never asked is stopped at once.  The batch
     *  harness is both, according to whether its batch asks.
     */
    {
        const char *log = fixture("bugs7c-net-batch.log");
        const char *asks[] = { "-serve", IMAGE, "-workers", "2",
                               "Socket stopRequested\n(Delay forSeconds: 600) wait\n",
                               NULL };
        const char *deaf[] = { "-serve", IMAGE, "-workers", "2",
                               "'up'\n(Delay forSeconds: 600) wait\n", NULL };

        pid = net_spawn(asks, log, "ST_STOP_GRACE_SECONDS=1", 0, 0);
        if (pid > 0 && net_log_wait(log, "==> false", 1, 60000)) {
            t0 = net_now_ms();
            kill(pid, SIGTERM);
            status = net_reap(pid, 15000);
            net_check(status == 0 && net_now_ms() - t0 >= 900
                                  && net_now_ms() - t0 < 8000,
                      "NET-5 an image that asks and never quits is stopped "
                      "after the grace", log);
            net_check(strstr(net_log(log), "did not stop within 1 s") != NULL,
                      "NET-5 ... and the VM says so", log);
        } else {
            net_check(0, "NET-5 the asking batch ran", log);
            if (pid > 0)
                net_reap(pid, 0);
        }

        pid = net_spawn(deaf, log, NULL, 0, 0);
        if (pid > 0 && net_log_wait(log, "==> 'up'", 1, 60000)) {
            t0 = net_now_ms();
            kill(pid, SIGTERM);
            status = net_reap(pid, 15000);
            net_check(status == 0 && net_now_ms() - t0 < 3000,
                      "NET-5 an image that never asks is stopped at once", log);
        } else {
            net_check(0, "NET-5 the deaf batch ran", log);
            if (pid > 0)
                net_reap(pid, 0);
        }
        unlink(log);
    }
}


/*
 *  A /rest POST on a kept-alive connection, and its whole response: the
 *  head, then as many bytes as Content-Length says.  The byte count, or -1.
 */
static long
net_request_keep(int fd, const char *json, char *out, size_t len, long ms)
{
    char        request[4096];
    int         n = snprintf(request, sizeof request,
                             "POST /rest HTTP/1.1\r\nHost: x\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
    int64_t     deadline = net_now_ms() + ms;
    size_t      got = 0;

    if (send(fd, request, (size_t) n, MSG_NOSIGNAL) != n)
        return -1;
    out[0] = '\0';
    for (;;) {
        const char     *end = strstr(out, "\r\n\r\n");
        const char     *cl  = strstr(out, "Content-Length: ");
        struct pollfd   p;
        int64_t         left = deadline - net_now_ms();
        ssize_t         k;

        if (end && cl && cl < end
         && got >= (size_t) (end + 4 - out) + (size_t) atol(cl + 16))
            return (long) got;
        if (left <= 0 || got >= len - 1)
            return -1;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        if (poll(&p, 1, (int) left) <= 0)
            continue;
        k = recv(fd, out + got, len - 1 - got, 0);
        if (k <= 0)
            return -1;
        got += (size_t) k;
        out[got] = '\0';
    }
}

/*
 *  `n' connections that each send one byte of a request and then nothing,
 *  so that the server holds each one for its whole request deadline; the
 *  count made.
 */
static int
net_open_idle(int port, int *fds, int n)
{
    int     i;

    for (i = 0; i < n; ++i) {
        fds[i] = net_connect(port);
        if (fds[i] < 0)
            break;
        (void) send(fds[i], "G", 1, MSG_NOSIGNAL);
    }
    return i;
}

static void
net_close_all(int *fds, int n)
{
    int     i;

    for (i = 0; i < n; ++i)
        if (fds[i] >= 0)
            close(fds[i]);
}

/*  A fresh client's first line, within `ms': "HTTP/1.1 503" or the like.  */
static int
net_fresh_status(int port, char *out, size_t len, long ms)
{
    int     fd = net_connect(port);

    out[0] = '\0';
    if (fd < 0)
        return -1;
    (void) send(fd, "GET / HTTP/1.1\r\nHost: x\r\n\r\n", 27, MSG_NOSIGNAL);
    net_read_all(fd, out, len, ms);
    close(fd);
    return 0;
}

/*
 *  Bugs07 NET-3: under the usual soft descriptor limit -- 1,024 under
 *  systemd and in an Ubuntu shell, 256 on macOS -- idle connections used
 *  every descriptor while there were still fewer than maxConnections of
 *  them.  The documented 503 never came, a fresh client waited in the
 *  backlog with no answer, and a request in flight that needed a
 *  descriptor failed with `Too many open files'.  Here, at 256 soft and
 *  hard so that nothing can be raised: the default maxConnections leaves
 *  room, so the fresh client is told 503 and the request in flight loads
 *  its service file; with maxConnections set past the descriptors, the
 *  spare descriptor still answers 503, twice, and the server serves again
 *  once the connections have gone.  And where the hard limit allows, the
 *  soft one is raised and the default is the full 1,024.
 */
static void
bugs7c_net_descriptors(void)
{
    static char reply[65536];
    static int  idle[300];
    const char *max = fixture("bugs7c-net-max.json");
    char        text[1024];
    int         port;
    int         keep;
    int         n;
    int         status;
    pid_t       pid;
    struct rlimit   own;

    /*  This process holds three hundred client sockets of its own.  */
    if (getrlimit(RLIMIT_NOFILE, &own) == 0 && own.rlim_cur != RLIM_INFINITY
     && own.rlim_cur < 1024) {
        own.rlim_cur = own.rlim_max == RLIM_INFINITY || own.rlim_max >= 1024
                     ? 1024 : own.rlim_max;
        (void) setrlimit(RLIMIT_NOFILE, &own);
    }

    /*  The default maxConnections: the fresh client is told, the work in flight done.  */
    pid = net_start_rest_on(NET_CONFIG, NULL, 256, 256, &port);
    net_check(pid > 0 && port > 0, "NET-3 the RestServer came up at 256 descriptors",
              NET_LOG);
    if (pid > 0 && port > 0) {
        keep = net_connect(port);
        net_request_keep(keep, "{\"_class\": \"\", \"_method\": \"LoginRequired\"}",
                         reply, sizeof reply, 20000);
        net_check(strstr(reply, "\"_Success\":true") != NULL,
                  "NET-3 a kept-alive connection is served", NET_LOG);
        n = net_open_idle(port, idle, 250);
        net_fresh_status(port, reply, sizeof reply, 10000);
        net_check(strncmp(reply, "HTTP/1.1 503", 12) == 0,
                  "NET-3 past the default maxConnections a fresh client is told 503",
                  NET_LOG);
        /*  The service file is first read now, in a descriptor of its own.  */
        net_request_keep(keep, "{\"_class\": \"services.Slow\", \"_method\": \"wait\", "
                         "\"ms\": 10}", reply, sizeof reply, 20000);
        net_check(strstr(reply, "\"done\":true") != NULL,
                  "NET-3 a request in flight still has a descriptor for its work",
                  NET_LOG);
        net_check(strstr(net_log(NET_LOG), "Too many open files") == NULL,
                  "NET-3 no descriptor ran out", NET_LOG);
        net_close_all(idle, n);
        close(keep);
        kill(pid, SIGTERM);
        status = net_reap(pid, 30000);
        net_check(status == 0, "NET-3 the server stops as asked", NET_LOG);
    }

    /*  maxConnections past the descriptors: the spare still says 503, again and again.  */
    snprintf(text, sizeof text,
             "{\"port\": 0, \"bindAddress\": \"127.0.0.1\", \"backendDirectory\": \"%s\", "
             "\"requireAuthentication\": false, \"maxConnections\": 4000}\n", NET_BACKEND);
    if (write_file(max, text) == 0
     && (pid = net_start_rest_on(max, NULL, 256, 256, &port)) > 0 && port > 0) {
        int     round;

        net_check(strstr(net_log(NET_LOG), "maxConnections 4000 is more than the 256") != NULL,
                  "NET-3 a maxConnections past the descriptors is said at start",
                  NET_LOG);
        for (round = 1; round <= 2; ++round) {
            int64_t t0;

            n = net_open_idle(port, idle, 270);
            net_fresh_status(port, reply, sizeof reply, 10000);
            net_check(strncmp(reply, "HTTP/1.1 503", 12) == 0,
                      round == 1 ? "NET-3 out of descriptors a fresh client is told 503"
                                 : "NET-3 ... and told 503 again: the spare came back",
                      NET_LOG);
            net_close_all(idle, n);
            /*  Served again once the connections have gone.  */
            t0 = net_now_ms();
            do {
                int fd = net_connect(port);

                reply[0] = '\0';
                if (fd >= 0) {
                    net_request_keep(fd, "{\"_class\": \"\", \"_method\": \"LoginRequired\"}",
                                     reply, sizeof reply, 3000);
                    close(fd);
                }
                if (strstr(reply, "\"_Success\":true") == NULL)
                    net_sleep_ms(200);
            } while (strstr(reply, "\"_Success\":true") == NULL
                     && net_now_ms() - t0 < 10000);
            net_check(strstr(reply, "\"_Success\":true") != NULL,
                      "NET-3 the server serves again when the connections close",
                      NET_LOG);
        }
        kill(pid, SIGTERM);
        net_reap(pid, 30000);
    } else
        net_check(0, "NET-3 the RestServer came up with maxConnections 4000", NET_LOG);
    unlink(max);

    /*
     *  The limit, as the image sees it: raised from a soft 1,024 to the
     *  socket table and a thousand more where the hard limit allows, with
     *  the full default then; left at a hard 256, with a quarter kept.
     */
    {
        const char *log = fixture("bugs7c-net-limit.log");
        const char *line = "{Socket descriptorLimit. HttpServer new maxConnections}\n";
        const char *args[] = { "-serve", IMAGE, "-workers", "1", line, NULL };
        struct rlimit   rl;
        unsigned long   want = NET_MAX_SOCKETS + 1024;
        char            expected[128];

        if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_max != RLIM_INFINITY
         && rl.rlim_max < (rlim_t) want)
            want = (unsigned long) rl.rlim_max;
        pid = net_spawn(args, log, NULL, 1024, 0);
        status = pid > 0 ? net_reap(pid, 60000) : -1;
        snprintf(expected, sizeof expected, "==> (%lu %lu )", want,
                 want < 1024 ? want : 1024);
        /*  A batch that ends is an image that stopped on its own: exit 1.  */
        net_check(status >= 0 && strstr(net_log(log), expected) != NULL,
                  want > 1024 ? "NET-3 a soft limit of 1,024 is raised"
                              : "NET-3 a soft limit of 1,024 is kept", log);
        pid = net_spawn(args, log, NULL, 256, 256);
        status = pid > 0 ? net_reap(pid, 60000) : -1;
        net_check(status >= 0 && strstr(net_log(log), "==> (256 192 )") != NULL,
                  "NET-3 a hard limit of 256 gives 192 connections", log);
        unlink(log);
    }
}

static void
bugs7c_net(void)
{
    if (net_build_rest_image() != 0) {
        net_check(0, "bugs7c_net: cannot set up the RestServer image", NULL);
        net_remove_rest_image();
        return;
    }
    bugs7c_net_stop();
    bugs7c_net_descriptors();
    net_remove_rest_image();
    unlink(NET_LOG);
}

/*
 *  Bugs07, round 3: the VM group's findings.
 */
static void
bugs7c_vm(void)
{
    static char out[65536];
    const char *oom = fixture("bugs7c-serve-oom.st");
    char        batch[1024];
    char        command[2048];
    int         status;

    /*
     *  INTERP-3b.  A process that handles its OutOfMemory and then
     *  allocates on in the handler has spent its reserve, and when it next
     *  needs a context activate_new_method cleared the interpreter's
     *  running flag -- the worker ended, and with it the pool: the batch
     *  never printed another line and the server exited `the image stopped
     *  on its own'.  Now that process alone is ended and the rest go on.
     *  The batch waits for the runaway's handler without allocating (a
     *  Semaphore, then primitives only: the Delay timing process would
     *  have to allocate while the table is full), then forks a process so
     *  that the worker the runaway ended on has something else to run --
     *  until it does, its registers still hold the runaway's stack.  The
     *  first line may itself be told OutOfMemory (the runaway's objects are
     *  not reclaimed until that worker moves on); the second must answer.
     */
    if (write_file(oom,
            "| t told |\n"
            "told := Semaphore new.\n"
            "[ | a | a := OrderedCollection new.\n"
            "  [[a add: (Array new: 1)] repeat] on: OutOfMemory do: [:e | "
            "told signal. [a add: (Array new: 1)] repeat]] fork.\n"
            "told wait.\n"
            "t := Time millisecondClockValue.\n"
            "[Time millisecondClockValue - t < 4000] whileTrue: [Processor yield].\n"
            "[nil] fork.\n"
            "t := Time millisecondClockValue.\n"
            "[Time millisecondClockValue - t < 1000] whileTrue: [Processor yield].\n"
            "^#waited\n") == 0) {
        snprintf(batch, sizeof batch,
                 "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                 "contentsOfEntireFile\n3 + 4\n", oom);
        if (write_file(BATCH, batch) == 0) {
            snprintf(command, sizeof command,
                     "ST_MAX_OBJECTS=1048576 " ST_TEST_TIMEOUT " -k 2 "
                     SERVE_SECONDS " %s -serve %s -workers 2 \"$(cat %s)\" 2>&1",
                     st2026, IMAGE, BATCH);
            status = run(command, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL INTERP-3b: could not run the server\n");
            } else {
                expect(out, "out of memory activating a method; the process "
                       "is ended", "INTERP-3b the runaway alone was ended");
                expect(out, "contentsOfEntireFile ==> ",
                       "INTERP-3b the batch line was answered");
                expect(out, "3 + 4 ==> 7", "INTERP-3b the pool carried on");
            }
        }
        unlink(oom);
    }

    /*
     *  OM-2.  Handled OutOfMemory, one runaway at a time, each followed by
     *  something kept and a collection.  The reserve's holders were bare
     *  oops: the third round's Process was given the second's dead entry,
     *  found "itself" on the list and was refused, and the image stopped.
     *  With the processes kept alive -- no entry reused -- every lift
     *  stayed spent because the kept Array lived above the ceiling, and
     *  the seventeenth round stopped the image.  Six rounds of the first
     *  and twenty of the second, each past where it stopped; a ceiling a
     *  little above what the image needs to start, and a collection sized
     *  for the runaway so growing it leaves no garbage to collect.  A round
     *  still fills the table and collects some twenty times on the way, a
     *  couple of seconds apiece, so the second form outlasts the usual
     *  sixty-second limit on a busy machine: ten times that here.
     */
    {
        static const struct {
            int         rounds;
            const char *temps;
            const char *keep;
            const char *what;
        } forms[] = {
            { 6, "", "", "OM-2 sequential handled runaways" },
            { 20, " procs p", "p := ",
              "OM-2 sequential handled runaways, processes kept" },
        };
        size_t  k;

        for (k = 0; k < sizeof forms / sizeof forms[0]; ++k) {
            char    text[2048];
            char    want[64];

            snprintf(text, sizeof text,
                "| rs fin keep%s |\n"
                "rs := OrderedCollection new. keep := OrderedCollection new.\n"
                "%s\n"
                "1 to: %d do: [:round |\n"
                "    fin := Semaphore new.\n"
                "    %s[ | a r | r := [a := OrderedCollection new: 1000000. "
                "[a add: (Array new: 1)] repeat] on: OutOfMemory do: "
                "[:e | e return: #caught].\n"
                "      a := nil. rs add: r. fin signal] fork.\n"
                "    %s\n"
                "    fin wait.\n"
                "    keep add: (Array new: 3).\n"
                "    Smalltalk garbageCollect].\n"
                "^'seq: ', (rs select: [:x | x == #caught]) size printString, "
                "' caught'\n",
                forms[k].temps,
                *forms[k].keep ? "procs := OrderedCollection new." : "",
                forms[k].rounds,
                forms[k].keep,
                *forms[k].keep ? "procs add: p." : "");
            if (write_file(oom, text) != 0)
                continue;
            snprintf(batch, sizeof batch,
                     "Compiler evaluate: (FileStream oldFileNamed: '%s') "
                     "contentsOfEntireFile\n3 + 4\n", oom);
            if (write_file(BATCH, batch) != 0)
                continue;
            snprintf(command, sizeof command,
                     "ST_MAX_OBJECTS=655360 " ST_TEST_TIMEOUT " -k 2 "
                     SERVE_SECONDS "0 %s -serve %s -workers 2 \"$(cat %s)\" 2>&1",
                     st2026, IMAGE, BATCH);
            status = run(command, out, sizeof out);
            ++st_test_checks;
            if (status < 0) {
                ++st_test_failures;
                printf("  FAIL OM-2: could not run the server\n");
                continue;
            }
            snprintf(want, sizeof want, "==> 'seq: %d caught'",
                     forms[k].rounds);
            expect(out, want, forms[k].what);
            expect_absent(out, "out of memory activating",
                          "OM-2 no runaway was refused its reserve");
            expect(out, "3 + 4 ==> 7", "OM-2 the pool carried on");
        }
        unlink(oom);
    }
}

/*
 *  Bugs07 round 3, the object memory's: OM-5, found independently as
 *  COMP-3 -- the bootstrap, -startup, -eval and the image's compile
 *  primitive held fresh objects in C variables across further
 *  allocations, and a collection in any of those windows freed them.
 *  C now says which variables hold objects (ST_hold, interp.h) and the
 *  root walk reads them.
 *
 *  The oracle is ST_GC_AT_CLASS, a collection at every allocation of one
 *  class.  Each class below opened a window the old binary died in on
 *  its first forced collection: MethodDictionary in compile_into (a
 *  class's first class-side method freed and its dictionary mapping #new
 *  to itself), Point in BOOT_install_display (the DisplayScreen freed for
 *  its own origin), Process in install_processor_object (the scheduler
 *  freed for its first process).  The expression then asks after exactly
 *  those objects.  Two of the classes have no fixed oop, so the image is
 *  asked for them first.  Array and String opened more windows still and
 *  now pass the same way, but a collection at every one the initializers
 *  make is six to nine minutes of bootstrap each; the served compile
 *  below takes Array instead.
 */
static void
bugs7c_om(void)
{
    static char out[65536];
    char        command[2048];
    char        md[32] = "";
    char        process[32] = "";
    int         status;
    unsigned    k;
    struct { const char *name; const char *oop; } forced[] = {
        { "MethodDictionary", md },
        { "Point",            "26" },
        { "Process",          process },
    };

    snprintf(command, sizeof command,
             ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
             " %s -bootstrap -profile profiles/st2026.profile -eval "
             "\"^'oops ', (MethodDictionary asOop * 2) printString, ' ', "
             "(Process asOop * 2) printString\" 2>/dev/null", st2026);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    {
        const char *at = strstr(out, "oops ");

        if (status != 0 || !at
         || sscanf(at, "oops %31s %31s", md, process) != 2) {
            ++st_test_failures;
            printf("  FAIL OM-5: cannot ask the image for its class oops "
                   "(exit %d):\n%s\n", status, out);
            return;
        }
    }

    for (k = 0; k < sizeof forced / sizeof forced[0]; ++k) {
        char    what[128];

        snprintf(command, sizeof command,
                 "ST_GC_AT_CLASS=%s " ST_TEST_TIMEOUT " -k 2 900"
                 " %s -bootstrap -profile profiles/st2026.profile -eval "
                 "\"^'held ', (3 + 4) printString, ' ', "
                 "(ArrayedCollection class compiledMethodAt: #new) class name, ' ', "
                 "Array new size printString, ' ', "
                 "Display class name, ' ', Display bits size printString, ' ', "
                 "Display offset printString, ' ', Processor class name, ' ', "
                 "Processor activeProcess class name, ' ', "
                 "(Object instVarNames isEmpty and: [Point instVarNames = #('x' 'y')]) "
                 "printString\" 2>&1",
                 forced[k].oop, st2026);
        status = run(command, out, sizeof out);
        snprintf(what, sizeof what,
                 "OM-5/COMP-3 a bootstrap with a collection at every %s",
                 forced[k].name);
        ++st_test_checks;
        if (status != 0) {
            ++st_test_failures;
            printf("  FAIL %s: exit %d\n", what, status);
        }
        expect(out, "held 7 CompiledMethod 0 DisplayScreen 19200 0@0 "
                    "ProcessorScheduler Process true", what);
    }

    /*
     *  The `Object compile:' finding: in a loaded image the new selector's
     *  Symbol was held in C alone while its symbol-table bucket was copied
     *  into an Array one longer, and a collection there freed it.  The old
     *  binary answered a random object for the selector -- `FilePage
     *  class(Behavior)>>superclasses' -- and the send that followed did
     *  not understand itself.  A collection at every Array, one worker.
     */
    if (write_file(BATCH,
                   "(Object compile: 'bugs7cOmBrace ^{3. 4}') == #bugs7cOmBrace\n"
                   "nil bugs7cOmBrace\n"
                   "(Object compile: 'bugs7cOmLits ^#(1 $a 2.5 (3 4))') size\n"
                   "nil bugs7cOmLits\n"
                   "Object removeSelector: #bugs7cOmBrace; removeSelector: #bugs7cOmLits\n"
                   "3 + 4\n") != 0) {
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL OM-5: cannot write the batch\n");
        return;
    }
    snprintf(command, sizeof command,
             "ST_GC_AT_CLASS=16 " ST_TEST_TIMEOUT " -k 2 " SERVE_SECONDS
             " %s -serve %s -workers 1 \"$(cat %s)\" 2>&1",
             st2026, IMAGE, BATCH);
    status = run(command, out, sizeof out);
    ++st_test_checks;
    if (status < 0) {
        ++st_test_failures;
        printf("  FAIL OM-5 compile: could not run the server\n");
        return;
    }
    expect(out, "== #bugs7cOmBrace ==> true",
           "OM-5 a compiled selector's Symbol survives a collection");
    expect(out, "nil bugs7cOmBrace ==> (3 4 )",
           "OM-5 the method compiled under a collection at every Array");
    expect(out, "nil bugs7cOmLits ==> (1 $a 2.5 (3 4 ) )",
           "OM-5 a literal-array method compiled under the same");
    expect(out, "3 + 4 ==> 7", "OM-5 the server carried on");
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
    bugs6_medium_om();
    bugs6_medium_kern();
    bugs6_medium_files();
    bugs6_medium_eval();
    bugs6_low_vm();
    bugs6_low_chron();
    bugs6_low_kernb();
    bugs7_vm();
    bugs7_lib();
    bugs7_sched();
    bugs7b_sched();
    bugs7b_vm();
    bugs7c_gui();
    bugs7c_net();
    bugs7c_sched();
    bugs7c_vm();
    bugs7c_om();

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

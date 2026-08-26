/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Sockets, from Smalltalk, on real workers, with the world stopping
 *  under them.
 *
 *  lib/Network-Tests proves the Socket classes in a green image, where
 *  every wait is one interpreter switching between its own processes.
 *  That is not the case the package was written for.  The case is a
 *  listener on one worker forking a process per connection that some OTHER
 *  worker picks up, thirty of them at once, each parking on a Semaphore
 *  the VM's network thread signals -- with the collector stopping every
 *  worker in the middle of it.  Nothing about that is exercised by a suite
 *  with one thread, and a fault in it looks like a hang.
 *
 *  So: N workers run the scheduler.  Worker 0's driver forks an accept
 *  loop; every accepted socket gets a forked echo process that records
 *  which worker it ran on, how many bytes it echoed, and that it finished.
 *  2N native client threads -- plain blocking BSD sockets, nothing of this
 *  system's -- each send forty messages of varying length with their own
 *  identity in every one, and compare every echo byte for byte.  A thread
 *  asking for safepoints runs throughout, and every echo process forces a
 *  full collection on its first message, while its Semaphores are armed.
 *
 *  Every number checked has one right value: mismatches zero, bytes in
 *  equal to bytes out per client and in total, every connection counted
 *  done, and -- with more than one worker -- more than one worker did the
 *  serving, which is the claim this package makes.  A root the collector
 *  does not see arrives as a wakeup that never comes, which the alarm turns
 *  into a failure rather than a hang nobody reads.
 *
 *      make OM=mt TSAN=1 unit-test
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "compiler.h"
#include "bootstrap.h"
#include "worker.h"
#include "st_sched.h"
#include "profile.h"
#include "st_port.h"
#include "st_atomic.h"
#include "st_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ST_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET      client_fd;
#define CLIENT_INVALID  INVALID_SOCKET
#define client_close(fd)    closesocket(fd)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
typedef int         client_fd;
#define CLIENT_INVALID  (-1)
#define client_close(fd)    close(fd)
#endif

#define PROFILE             "profiles/st2026.profile"
#define MESSAGES_PER_CLIENT 40
#define MAX_CLIENTS         128

static st_names     sources;
static int         *dialects;

/*  ----------  Compiling and running, as test_parallel_shared does  ----------  */

static st_oop
compile_expression(const char *expression)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                source[8192];

    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_large_integer_digits = BOOT_make_large_integer_digits;
    ctx.make_array         = BOOT_make_array;
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;
    ctx.dialect            = ST_DIALECT_CLOSURES;

    snprintf(source, sizeof source, "doIt %s", expression);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        printf("  cannot compile: %s\n", res.error);
        return ST_OOP_INVALID;
    }
    OM_increase_ref(res.method);
    return res.method;
}

static st_oop
run_method(st_oop method)
{
    st_oop  context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 64);

    if (!OM_is_present(context))
        return ST_OOP_INVALID;
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int) (BOOT_method_initial_ip(method) + 1)));
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, method))));
    /*  A Process to be, made together with its context -- see test_parallel_shared.  */
    {
        st_oop  assoc = BOOT_lookup_global("Process", NULL);
        st_oop  cls   = OM_is_object(assoc)
                            ? OM_fetch_pointer(ST_ASSOCIATION_VALUE, assoc)
                            : ST_OOP_INVALID;

        if (OM_is_object(cls)) {
            st_oop  proc = OM_instantiate_pointers(cls, 4);

            if (OM_is_object(proc)) {
                OM_store_pointer(ST_LINK_NEXT, proc, ST_NIL);
                OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, proc, context);
                OM_store_pointer(ST_PROCESS_PRIORITY, proc, OM_int_oop(4));
                OM_store_pointer(ST_PROCESS_MY_LIST, proc, ST_NIL);
                OM_increase_ref(proc);
                st_vm.active_process = proc;
            }
        }
    }
    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    ST_interp_run(UINT64_C(40000000000));
    if (st_vm.running)
        return ST_OOP_INVALID;
    return st_vm.return_value;
}

static st_oop           driver_method;
static st_oop           single_method;
static long             single_answer;
static int              single_is_int;

static void
provide_test_roots(om_visit_fn visit)
{
    BOOT_provide_roots(visit);
    if (OM_is_object(driver_method))
        visit(driver_method);
    if (OM_is_object(single_method))
        visit(single_method);
}

static void
single_worker(st_worker *self, void *user)
{
    st_oop  value;

    (void) self; (void) user;
    ST_interp_register();
    value = run_method(single_method);
    single_is_int = OM_is_int(value);
    single_answer = single_is_int ? (long) OM_int_value(value) : -1;
    ST_interp_unregister();
}

/*  Run an expression on one worker; its integer answer is in single_answer.  */
static int
run_single(const char *source)
{
    single_method = compile_expression(source);
    if (single_method == ST_OOP_INVALID)
        return 0;
    single_is_int = 0;
    single_answer = -1;
    WORKER_start(1, single_worker, NULL);
    WORKER_stop();
    ST_interp_register();
    single_method = ST_OOP_INVALID;
    return 1;
}

/*  How many drivers have started: what the asker waits for.  */
static st_atomic_int    drivers_running;

static void
driver_worker(st_worker *self, void *user)
{
    (void) self; (void) user;
    ST_interp_register();
    ST_fetch_add_acq_rel(&drivers_running, 1);
    run_method(driver_method);
    ST_interp_unregister();
}

/*
 *  How many workers: ST_LIB_WORKERS, or one per core, or four under the
 *  thread sanitizer -- test_parallel_shared's reasoning.
 */
static unsigned
want_workers(void)
{
    const char *text = getenv("ST_LIB_WORKERS");

    if (text)
        return (unsigned) atoi(text);
#if defined(__SANITIZE_THREAD__)
    return 4;
#else
    return 0;
#endif
}

/*  ----------  The clients: native threads, plain sockets  ----------  */

static int              server_port;
static st_atomic_int    client_mismatches;
static st_atomic_int    client_failures;
static long             client_sent[MAX_CLIENTS];
static long             client_got[MAX_CLIENTS];

static void
client_main(void *arg)
{
    int                 id = (int) (intptr_t) arg;
    client_fd           fd;
    struct sockaddr_in  addr;
    int                 m;
    char                message[512];
    char                echo[512];

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == CLIENT_INVALID) {
        ST_fetch_add_relaxed(&client_failures, 1);
        return;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons((unsigned short) server_port);
    if (connect(fd, (struct sockaddr *) &addr, sizeof addr) != 0) {
        ST_fetch_add_relaxed(&client_failures, 1);
        client_close(fd);
        return;
    }
    for (m = 0; m < MESSAGES_PER_CLIENT; ++m) {
        /*
         *  A message whose length varies with m and whose text carries the
         *  client's identity: an echo from another client's socket, or a
         *  fragment of the wrong message, fails the comparison.
         */
        int     length = snprintf(message, sizeof message,
                                  "client-%d-message-%d-%0*d", id, m,
                                  1 + (m * 7) % 300, m);
        int     have = 0;

        if (send(fd, message, length, 0) != length) {
            ST_fetch_add_relaxed(&client_failures, 1);
            break;
        }
        client_sent[id] += length;
        while (have < length) {
            long    n = (long) recv(fd, echo + have, length - have, 0);

            if (n <= 0) {
                ST_fetch_add_relaxed(&client_failures, 1);
                break;
            }
            have += (int) n;
        }
        if (have != length || memcmp(message, echo, length) != 0)
            ST_fetch_add_relaxed(&client_mismatches, 1);
        client_got[id] += have;
    }
#ifdef ST_WINDOWS
    shutdown(fd, SD_SEND);
#else
    shutdown(fd, SHUT_WR);
#endif
    /*  Read to end of stream: the server closes after the echo ends.  */
    while (recv(fd, echo, sizeof echo, 0) > 0)
        ;
    client_close(fd);
}

/*  ----------  A thread that stops the world, repeatedly  ----------  */

static st_atomic_int    stop_asking;
static st_atomic_int    safepoints_asked;

static void
request_release_loop(void *unused)
{
    (void) unused;
    /*
     *  WAIT FOR THE POOL TO EXIST before asking for anything, as the
     *  database gate does: this thread is created before WORKER_start,
     *  and a request that lands while the pool is being set up reads the
     *  worker table as it is being written.
     */
    while (!ST_load_relaxed(&stop_asking)
        && ST_load_acquire(&drivers_running) == 0)
        ST_thread_yield();
    while (!ST_load_relaxed(&stop_asking)) {
        WORKER_request_safepoint();
        WORKER_release_safepoint();
        ST_fetch_add_relaxed(&safepoints_asked, 1);
        ST_sleep_ns(200000);
    }
}

/*  ----------  The Smalltalk side  ----------  */

/*
 *  Worker 0's driver forks the accept loop; every driver then waits until
 *  every connection has been served.  Each accepted socket is served by a
 *  forked process, so it runs on whichever worker takes it -- which is the
 *  property the last check asserts.  The first message of every connection
 *  forces a full collection with the socket's Semaphores armed on the
 *  other connections, which is the root walk being tested.
 *
 *  The accept loop ENDS when the listener is closed -- accept then raises
 *  NetError, which the handler turns into the end of the process.  It has
 *  to: a forked process outlives the drivers, and one left waiting on the
 *  first gate's listener woke up in the second gate's pool, found its
 *  listener closed, and -- an unhandled error in a forked process prints
 *  and carries on with nil -- went round its loop accepting nothing and
 *  reporting it, for ever.
 *
 *  The accepted socket reaches its echo process as a BLOCK ARGUMENT, not
 *  a temporary declared inside the loop.  whileTrue: is inlined, so a
 *  temporary declared in its body is one variable for every iteration,
 *  and a process forked in one iteration that captured it was reading the
 *  socket the NEXT iteration accepted -- two processes on one socket, one
 *  of them finding it closed under it.  A block argument is fresh per
 *  activation, which is the whole difference.
 */
static const char *const driver_source =
    "Processor activeWorkerIndex = 0 ifTrue: ["
    "  [[ | l | l := Smalltalk at: #NetTestListener."
    "    [true] whileTrue: [[:c |"
    "      [ | n buffer w k first |"
    "        w := Processor activeWorkerIndex + 1."
    "        n := 0. first := true. buffer := String new: 4096."
    "        [k := c receiveInto: buffer. k > 0] whileTrue: ["
    "          first ifTrue: [first := false. Smalltalk garbageCollect]."
    "          c send: (buffer copyFrom: 1 to: k). n := n + k]."
    "        c close."
    "        (Smalltalk at: #NetTestLock) critical: ["
    "          (Smalltalk at: #NetTestPerWorker) at: w put: ((Smalltalk at: #NetTestPerWorker) at: w) + 1."
    "          (Smalltalk at: #NetTestBytes) at: w put: ((Smalltalk at: #NetTestBytes) at: w) + n."
    "          Smalltalk at: #NetTestDone put: (Smalltalk at: #NetTestDone) + 1]] fork]"
    "      value: l accept]]"
    "    on: NetError do: [:e | nil]] fork]."
    "[(Smalltalk at: #NetTestDone) < %d] whileTrue: [(Delay forMilliseconds: 2) wait]. ^0";

/*  A body that does nothing: for learning how big a pool of 0 is.  */
static void
idle_worker(st_worker *self, void *user)
{
    (void) self; (void) user;
}

static void
run_gate(unsigned workers)
{
    char        source[8192];
    unsigned    clients;
    st_thread   client_threads[MAX_CLIENTS];
    st_thread   asker;
    unsigned    i;
    long        total_sent = 0;
    long        total_got = 0;
    int         safepoints_before;
    int         served_by = 0;

    if (workers == 0) {
        WORKER_start(0, idle_worker, NULL);
        workers = WORKER_count();
        WORKER_stop();
        ST_interp_register();
    }
    clients = workers * 2;
    if (clients > MAX_CLIENTS)
        clients = MAX_CLIENTS;

    ST_store_seq(&client_mismatches, 0);
    ST_store_seq(&client_failures, 0);
    ST_store_seq(&stop_asking, 0);
    ST_store_seq(&drivers_running, 0);
    ST_store_seq(&safepoints_asked, 0);
    memset(client_sent, 0, sizeof client_sent);
    memset(client_got, 0, sizeof client_got);

    CHECK(run_single(
        " Smalltalk at: #NetTestDone put: 0."
        " Smalltalk at: #NetTestLock put: Mutex new."
        " Smalltalk at: #NetTestPerWorker put: (Array new: 64 withAll: 0)."
        " Smalltalk at: #NetTestBytes put: (Array new: 64 withAll: 0)."
        " Smalltalk at: #NetTestListener put:"
        "   (ServerSocket listenOn: 0 address: '127.0.0.1' backlog: 128)."
        " ^(Smalltalk at: #NetTestListener) localPort"));
    CHECK(single_is_int && single_answer > 0);
    if (!single_is_int || single_answer <= 0)
        return;
    server_port = (int) single_answer;

    snprintf(source, sizeof source, driver_source, (int) clients);
    driver_method = compile_expression(source);
    CHECK(driver_method != ST_OOP_INVALID);
    if (driver_method == ST_OOP_INVALID)
        return;

    safepoints_before = WORKER_safepoint_count();

    /*
     *  The clients first: they block in connect until the listener's
     *  backlog takes them, and in recv until an echo process runs, both of
     *  which happen once the drivers start.  The asker too, because
     *  WORKER_stop is the join and WORKER_start returns at once.
     */
    for (i = 0; i < clients; ++i)
        ST_thread_create(&client_threads[i], client_main,
                         (void *) (intptr_t) i);
    ST_thread_create(&asker, request_release_loop, NULL);

#ifndef ST_WINDOWS
    alarm(120);
#endif
    CHECK_EQ_INT(WORKER_start(workers, driver_worker, NULL), 0);
    workers = WORKER_count();
    /*
     *  The asker goes first: it reads the worker count on every request,
     *  and WORKER_stop writes it.
     */
    ST_store_seq(&stop_asking, 1);
    ST_thread_join(asker);
    WORKER_stop();
#ifndef ST_WINDOWS
    alarm(0);
#endif
    for (i = 0; i < clients; ++i)
        ST_thread_join(client_threads[i]);
    ST_interp_register();
    driver_method = ST_OOP_INVALID;

    printf("  %u workers served %u clients, %d messages each, across %d "
           "safepoints\n", workers, clients, MESSAGES_PER_CLIENT,
           WORKER_safepoint_count() - safepoints_before);

    CHECK_EQ_INT(ST_load_seq(&client_failures), 0);
    CHECK_EQ_INT(ST_load_seq(&client_mismatches), 0);
    for (i = 0; i < clients; ++i) {
        CHECK_EQ_INT((int) client_got[i], (int) client_sent[i]);
        total_sent += client_sent[i];
        total_got  += client_got[i];
    }
    CHECK(total_sent > 0);
    /*
     *  The collector must have run WHILE connections were served.  The
     *  asker is joined before the workers stop now, so the count no longer
     *  includes its requests during teardown; the two-worker phase is over
     *  in milliseconds and sees only a few, the big one sees dozens.
     */
    CHECK(WORKER_safepoint_count() - safepoints_before >= (workers >= 8 ? 5 : 1));

    /*  Read back on a worker, not the main thread.  */
    CHECK(run_single(" ^Smalltalk at: #NetTestDone"));
    CHECK_EQ_INT((int) single_answer, (int) clients);
    CHECK(run_single(" ^(Smalltalk at: #NetTestBytes) inject: 0 into: [:a :b | a + b]"));
    CHECK_EQ_INT((int) single_answer, (int) total_got);
    CHECK(run_single(" ^((Smalltalk at: #NetTestPerWorker) select: [:n | n > 0]) size"));
    served_by = (int) single_answer;
    /*
     *  With a pool of any size the scheduler MAY hand every connection to
     *  one worker, and with two workers and four short connections it
     *  sometimes does; that is not a fault.  With eight or more the odds of
     *  it are nil, and the spread is what this gate is for.
     */
    if (workers >= 8)
        CHECK(served_by > 1);
    printf("  connections were served on %d of %u workers\n", served_by, workers);

    CHECK(run_single(" (Smalltalk at: #NetTestListener) close. ^0"));
}

int
main(void)
{
    st_bootstrap_result  boot;
    st_boot_init_report  init;
    char            profile_error[256];

    ST_TEST_BEGIN("sockets from Smalltalk, on real workers");

    if (OM_init() != 0) {
        printf("  cannot initialize the object memory\n");
        CHECK(0);
        return ST_TEST_END();
    }
    if (!PROFILE_expand(PROFILE, &sources, &dialects,
                        profile_error, sizeof profile_error)) {
        printf("skipped: %s (run from the top of the tree)\n", profile_error);
        return ST_TEST_END();
    }
    if (BOOT_build_dialects((const char *const *) sources.items, dialects,
                            sources.count, &boot) != 0) {
        printf("  bootstrap failed: %s\n", boot.error);
        CHECK(0);
        return ST_TEST_END();
    }
    CHECK(BOOT_install_display(640, 480));
    BOOT_run_initializers(&init);
    ST_interp_install_roots(provide_test_roots);
    ST_interp_register();
#ifndef ST_WINDOWS
    signal(SIGPIPE, SIG_IGN);
#endif

    run_gate(2);
    run_gate(want_workers());

    NET_shutdown();
    SCHED_timer_stop();
    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    ST_TEST_BEGIN("sockets from Smalltalk, on real workers");
    printf("skipped: this needs the 64-bit object memory\n");
    return ST_TEST_END();
}

#endif

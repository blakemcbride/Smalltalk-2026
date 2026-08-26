/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The REST server, on real workers, with clients that know nothing of it.
 *
 *  test_parallel_net proves that sockets and the scheduler agree; this
 *  proves the claim the whole port was made for: that a JSON-RPC request
 *  is served on whichever core is free, and that thirty-one of them at
 *  once are served correctly.  A RestServer runs on the pool with the back
 *  end in tests/rest-backend; 2N native client threads speak HTTP to it by
 *  hand -- a POST, the headers, the body -- twenty requests each on one
 *  kept-alive connection, every request carrying two numbers only that
 *  client knows, and check the sum that comes back and that every reply is
 *  a success.  The service also answers which worker served it, which is
 *  how the spread across cores is measured rather than assumed.
 *
 *  The safepoint-asking thread runs throughout, so the collector stops the
 *  world while requests are being parsed, dispatched and answered.
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
#define REQUESTS_PER_CLIENT 20
#define MAX_CLIENTS         128

static st_names     sources;
static int         *dialects;

/*  ----------  Compiling and running, as the other gates do  ----------  */

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

static void
idle_worker(st_worker *self, void *user)
{
    (void) self; (void) user;
}

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

/*  ----------  The clients  ----------  */

static int              server_port;
static st_atomic_int    client_failures;
static st_atomic_int    wrong_sums;
static st_atomic_int    workers_seen[ST_MAX_WORKERS];
static long             client_replies[MAX_CLIENTS];

/*  Read one HTTP response; answer the body's length or -1.  */
static long
read_response(client_fd fd, char *body, size_t max)
{
    char        head[4096];
    size_t      have = 0;
    char       *blank = NULL;
    long        length = -1;
    size_t      got;
    const char *p;

    /*  The head, up to the blank line.  */
    while (have < sizeof head - 1) {
        long    n = (long) recv(fd, head + have, 1, 0);

        if (n <= 0)
            return -1;
        have += (size_t) n;
        head[have] = '\0';
        if (have >= 4 && (blank = strstr(head, "\r\n\r\n")) != NULL)
            break;
    }
    if (!blank)
        return -1;
    if (strncmp(head, "HTTP/1.1 200", 12) != 0)
        return -1;
    for (p = head; (p = strstr(p, "\n")) != NULL; ++p) {
        if (strncasecmp(p + 1, "Content-Length:", 15) == 0) {
            length = atol(p + 16);
            break;
        }
    }
    if (length < 0 || (size_t) length >= max)
        return -1;
    got = 0;
    while (got < (size_t) length) {
        long    n = (long) recv(fd, body + got, (size_t) length - got, 0);

        if (n <= 0)
            return -1;
        got += (size_t) n;
    }
    body[got] = '\0';
    return length;
}

/*  The integer after `"name":' in a JSON text, or -1.  */
static long
json_integer(const char *body, const char *name)
{
    char        key[64];
    const char *p;

    snprintf(key, sizeof key, "\"%s\":", name);
    p = strstr(body, key);
    if (!p)
        return -1;
    return atol(p + strlen(key));
}

static void
client_main(void *arg)
{
    int                 id = (int) (intptr_t) arg;
    client_fd           fd;
    struct sockaddr_in  addr;
    int                 r;

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
    for (r = 0; r < REQUESTS_PER_CLIENT; ++r) {
        char    json[256];
        char    request[512];
        char    body[4096];
        long    a = 1000 * id + r;
        long    b = 7 * r + 3;
        int     length;
        long    sum, worker;

        length = snprintf(json, sizeof json,
                          "{\"_class\":\"services.Adder\",\"_method\":\"whoAmI\","
                          "\"num1\":%ld,\"num2\":%ld}", a, b);
        snprintf(request, sizeof request,
                 "POST /rest HTTP/1.1\r\nHost: gate\r\n"
                 "Content-Type: application/json\r\nContent-Length: %d\r\n"
                 "%s\r\n%s",
                 length, r == REQUESTS_PER_CLIENT - 1 ? "Connection: close\r\n" : "",
                 json);
        if (send(fd, request, strlen(request), 0) != (long) strlen(request)) {
            ST_fetch_add_relaxed(&client_failures, 1);
            break;
        }
        if (read_response(fd, body, sizeof body) < 0) {
            ST_fetch_add_relaxed(&client_failures, 1);
            break;
        }
        sum    = json_integer(body, "num3");
        worker = json_integer(body, "worker");
        if (sum != a + b || strstr(body, "\"_Success\":true") == NULL)
            ST_fetch_add_relaxed(&wrong_sums, 1);
        if (worker >= 0 && worker < ST_MAX_WORKERS)
            ST_fetch_add_relaxed(&workers_seen[worker], 1);
        client_replies[id]++;
    }
    client_close(fd);
}

/*  ----------  The safepoint asker  ----------  */

static st_atomic_int    stop_asking;

static void
request_release_loop(void *unused)
{
    (void) unused;
    while (!ST_load_relaxed(&stop_asking)
        && ST_load_acquire(&drivers_running) == 0)
        ST_thread_yield();
    while (!ST_load_relaxed(&stop_asking)) {
        WORKER_request_safepoint();
        WORKER_release_safepoint();
        ST_sleep_ns(500000);
    }
}

/*  ----------  The Smalltalk side  ----------  */

/*
 *  Worker 0 starts the server; every driver waits until the service has
 *  counted every request.  RestGateCount is what services/Adder.class.st's
 *  whoAmI increments when it finds the global.
 */
static const char *const setup_source =
    " Smalltalk at: #RestGateLock put: Mutex new."
    " Smalltalk at: #RestGateCount put: 0."
    " Smalltalk at: #RestGateServer put: (RestServer new port: 0; bindAddress: '127.0.0.1';"
    "   backendDirectory: 'tests/rest-backend'; name: 'gate'; yourself)."
    " (Smalltalk at: #RestGateServer) start."
    " ^(Smalltalk at: #RestGateServer) port";

static const char *const driver_source =
    "[(Smalltalk at: #RestGateCount) < %d] whileTrue: [(Delay forMilliseconds: 5) wait]. ^0";

static void
run_gate(unsigned workers)
{
    char        source[4096];
    unsigned    clients;
    st_thread   client_threads[MAX_CLIENTS];
    st_thread   asker;
    unsigned    i;
    long        replies = 0;
    int         served_by = 0;
    int         safepoints_before;

    if (workers == 0) {
        WORKER_start(0, idle_worker, NULL);
        workers = WORKER_count();
        WORKER_stop();
        ST_interp_register();
    }
    clients = workers * 2;
    if (clients > MAX_CLIENTS)
        clients = MAX_CLIENTS;

    ST_store_seq(&client_failures, 0);
    ST_store_seq(&wrong_sums, 0);
    ST_store_seq(&stop_asking, 0);
    ST_store_seq(&drivers_running, 0);
    for (i = 0; i < ST_MAX_WORKERS; ++i)
        ST_store_seq(&workers_seen[i], 0);
    memset(client_replies, 0, sizeof client_replies);

    CHECK(run_single(setup_source));
    CHECK(single_is_int && single_answer > 0);
    if (!single_is_int || single_answer <= 0)
        return;
    server_port = (int) single_answer;

    snprintf(source, sizeof source, driver_source,
             (int) (clients * REQUESTS_PER_CLIENT));
    driver_method = compile_expression(source);
    CHECK(driver_method != ST_OOP_INVALID);
    if (driver_method == ST_OOP_INVALID)
        return;

    safepoints_before = WORKER_safepoint_count();
    for (i = 0; i < clients; ++i)
        ST_thread_create(&client_threads[i], client_main,
                         (void *) (intptr_t) i);
    ST_thread_create(&asker, request_release_loop, NULL);

#ifndef ST_WINDOWS
    alarm(180);
#endif
    CHECK_EQ_INT(WORKER_start(workers, driver_worker, NULL), 0);
    workers = WORKER_count();
    /*
     *  The asker goes first: it reads the worker count on every request,
     *  and WORKER_stop writes it.  ThreadSanitizer saw the two meet.
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

    for (i = 0; i < clients; ++i)
        replies += client_replies[i];
    for (i = 0; i < workers && i < ST_MAX_WORKERS; ++i)
        if (ST_load_seq(&workers_seen[i]) > 0)
            ++served_by;

    printf("  %u workers answered %ld requests from %u clients, on %d of the "
           "workers, across %d safepoints\n", workers, replies, clients,
           served_by, WORKER_safepoint_count() - safepoints_before);

    CHECK_EQ_INT(ST_load_seq(&client_failures), 0);
    CHECK_EQ_INT(ST_load_seq(&wrong_sums), 0);
    CHECK_EQ_INT((int) replies, (int) (clients * REQUESTS_PER_CLIENT));
    /*
     *  The collector must have run WHILE connections were served.  The
     *  asker is joined before the workers stop now, so the count no longer
     *  includes its requests during teardown; the two-worker phase is over
     *  in milliseconds and sees only a few, the big one sees dozens.
     */
    CHECK(WORKER_safepoint_count() - safepoints_before >= (workers >= 8 ? 5 : 1));
    if (workers >= 8)
        CHECK(served_by > 1);

    CHECK(run_single(" ^Smalltalk at: #RestGateCount"));
    CHECK_EQ_INT((int) single_answer, (int) (clients * REQUESTS_PER_CLIENT));
    CHECK(run_single(" (Smalltalk at: #RestGateServer) stop. ^0"));
}

int
main(void)
{
    st_bootstrap_result  boot;
    st_boot_init_report  init;
    char            profile_error[256];

    ST_TEST_BEGIN("the REST server, on real workers");

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
    ST_TEST_BEGIN("the REST server, on real workers");
    printf("skipped: this needs the 64-bit object memory\n");
    return ST_TEST_END();
}

#endif

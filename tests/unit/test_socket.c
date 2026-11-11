/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The socket layer, in C, with no image.
 *
 *  src/net/st_socket.c is written so that it can be read and tested
 *  without an object memory -- it deals in descriptors, small-integer
 *  handles and opaque tokens -- and this is that test.  The Smalltalk
 *  side (lib/Network) and the gate that runs it on real workers
 *  (test_parallel_net.c) sit on top of what is checked here:
 *
 *      a listener on the loopback interface, a connection to it, bytes
 *      both ways, end of stream, and half-close;
 *
 *      the arm-wait-retry contract: a call that would block, an arm, a
 *      signal from the I/O thread delivered through the hook, and the
 *      interest cleared once -- and only once -- per arm;
 *
 *      a dropped signal is NOT a lost wakeup: a hook that refuses is
 *      asked again;
 *
 *      a closed handle, a mis-generationed handle and a handle from before
 *      NET_shutdown are all refused rather than resolved to somebody else's
 *      socket;
 *
 *      the tokens are visited and recognised, the random bytes are random,
 *      the arguments come back.
 *
 *  The hook here counts signals per token instead of signalling anything;
 *  waiting is a bounded spin on that count.  Five seconds is a hundred
 *  times what any of it takes and short enough that a hang is a failure
 *  somebody reads.
 */

#include "st_test.h"
#include "st_socket.h"
#include "st_port.h"
#include "st_atomic.h"

#include <stdio.h>
#include <string.h>

#define TOKEN_SERVER_READ   1001
#define TOKEN_SERVER_WRITE  1002
#define TOKEN_CLIENT_READ   2001
#define TOKEN_CLIENT_WRITE  2002
#define TOKEN_LISTEN_READ   3001

static st_atomic_int    signals[4];         /*  by token / 1000  */
static st_atomic_int    refusals_left;      /*  the hook says no this many times  */
static st_atomic_int    refused;

static int
counting_hook(uintptr_t token)
{
    if (ST_load_relaxed(&refusals_left) > 0) {
        ST_fetch_sub_relaxed(&refusals_left, 1);
        ST_fetch_add_relaxed(&refused, 1);
        return 0;
    }
    ST_fetch_add_relaxed(&signals[token / 1000], 1);
    return 1;
}

static int
signals_for(uintptr_t token)
{
    return ST_load_relaxed(&signals[token / 1000]);
}

/*  What NET_visit_tokens showed us.  */
static uintptr_t    visited[16];
static unsigned     visited_count;

static void
record(uintptr_t token, void *user)
{
    (void) user;
    if (visited_count < 16)
        visited[visited_count] = token;
    ++visited_count;
}

/*  Wait until the token has been signalled at least `want' times.  */
static int
wait_for(uintptr_t token, int want)
{
    int64_t deadline = ST_time_monotonic_ns() + INT64_C(5000000000);

    while (signals_for(token) < want) {
        if (ST_time_monotonic_ns() > deadline)
            return 0;
        ST_sleep_ns(200000);
    }
    return 1;
}

/*  Arm, wait for the signal, and answer whether it came.  */
static int
arm_and_wait(int64_t handle, int mask, uintptr_t token)
{
    int before = signals_for(token);

    if (NET_arm(handle, mask) != 0) {
        printf("  arm: %s\n", NET_last_error());
        return 0;
    }
    return wait_for(token, before + 1);
}

/*
 *  A host's addresses, counted, and connected to by index: what
 *  Socket class>>connectTo:port: loops over so that a name resolving to
 *  ::1 and 127.0.0.1 reaches a server listening on the second.
 */
static void
test_addresses(void)
{
    int64_t     listener;
    int         port;
    int64_t     client;
    uintptr_t   old_read;
    uintptr_t   old_write;

    CHECK_EQ_INT(NET_address_count("127.0.0.1", 1), 1);
    CHECK(NET_address_count("localhost", 1) >= 1);
    CHECK_EQ_INT(NET_address_count("no.such.host.invalid", 1), -1);

    listener = NET_listen("127.0.0.1", 0, 8);
    CHECK(listener >= 0);
    port = NET_local_port(listener);
    /*  Index 0 is the address; index 5 is past the end and refused by name.  */
    client = NET_connect("127.0.0.1", port, 0);
    CHECK(client >= 0);
    NET_close(client, &old_read, &old_write);
    CHECK(NET_connect("127.0.0.1", port, 5) < 0);
    CHECK(strstr(NET_last_error(), "no such address") != NULL);
    NET_close(listener, &old_read, &old_write);
}

/*  ----------  Bugs07 NET-15: a number reissued in mid-call  ----------  */

#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__linux__) && defined(__GLIBC__) && defined(SYS_shutdown) \
 && defined(SYS_getsockname) && defined(SYS_getpeername) \
 && defined(SYS_setsockopt) && defined(SYS_getsockopt)
#define REISSUE_TEST 1

#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/*
 *  Five calls in st_socket.c -- NET_shutdown_write, NET_local_port,
 *  NET_peer_address, NET_set_option and NET_connect_result -- looked the
 *  descriptor up under net_lock, let the lock go, and only then made
 *  their system call on it.  A close from another thread in between
 *  frees the number, the kernel gives it at once to the next socket
 *  anybody opens, and the call then acts on that stranger: half-closes
 *  its connection, reports its port or its connect error, sets Nagle on
 *  it.  The window is a few instructions wide and no stress run hits it
 *  on demand, so this test makes it as wide as it likes: it defines the
 *  five system calls itself (the executable's definition is the one
 *  libst.a's references bind to; the real call is made through syscall,
 *  and glibc's own spelling of the address argument is kept so that the
 *  definitions agree with its declarations),
 *  and the one made on this thread while armed stops, starts a closer
 *  thread -- close the handle, open a stranger in its number -- and
 *  waits up to 300 ms for the closer to finish before it goes on with
 *  the number it was given.
 *
 *  Before the fix the closer finishes inside that wait and the call
 *  lands on the stranger.  After it the call holds net_lock, so the
 *  closer's NET_close waits for it, the wait runs out, and the call is
 *  made on the socket that was asked about; the close and the stranger
 *  come after.  Every case then checks the stranger is untouched and the
 *  answer is the asked-about socket's -- and that the stranger did get
 *  the number, without which the case would prove nothing.
 */
enum { STOP_NONE, STOP_SHUTDOWN, STOP_GETSOCKNAME, STOP_GETPEERNAME,
       STOP_SETSOCKOPT, STOP_GETSOCKOPT };
enum { STRANGER_LISTENER, STRANGER_CONNECTED, STRANGER_REFUSED };

static _Thread_local int    stop_armed;     /*  only the test's own thread stops  */
static st_atomic_int        stop_fd;        /*  the number the stopped call was given, + 1  */
static st_atomic_int        closer_go;
static st_atomic_int        closer_done;
static int64_t              reissue_victim;
static int                  reissue_kind;
static int                  reissue_port;
static int64_t              reissue_stranger;
static int                  reissue_raced;

static void
stop_point(int which, int fd)
{
    int64_t deadline;

    if (stop_armed != which)
        return;
    stop_armed = STOP_NONE;
    ST_store_release(&stop_fd, fd + 1);
    ST_store_release(&closer_go, 1);
    deadline = ST_time_monotonic_ns() + INT64_C(300000000);
    while (!ST_load_acquire(&closer_done) && ST_time_monotonic_ns() < deadline)
        ST_sleep_ns(1000000);
    reissue_raced = ST_load_acquire(&closer_done);
}

int
shutdown(int fd, int how)
{
    stop_point(STOP_SHUTDOWN, fd);
    return (int) syscall(SYS_shutdown, fd, how);
}

/*  The address argument as a plain pointer, whichever form glibc gave it.  */
static void *
address_pointer(const void *argument)
{
    void   *p;

    memcpy(&p, argument, sizeof p);
    return p;
}

int
getsockname(int fd, __SOCKADDR_ARG addr, socklen_t *len)
{
    stop_point(STOP_GETSOCKNAME, fd);
    return (int) syscall(SYS_getsockname, fd, address_pointer(&addr), len);
}

int
getpeername(int fd, __SOCKADDR_ARG addr, socklen_t *len)
{
    stop_point(STOP_GETPEERNAME, fd);
    return (int) syscall(SYS_getpeername, fd, address_pointer(&addr), len);
}

int
setsockopt(int fd, int level, int name, const void *value, socklen_t len)
{
    stop_point(STOP_SETSOCKOPT, fd);
    return (int) syscall(SYS_setsockopt, fd, level, name, value, len);
}

int
getsockopt(int fd, int level, int name, void *value, socklen_t *len)
{
    stop_point(STOP_GETSOCKOPT, fd);
    return (int) syscall(SYS_getsockopt, fd, level, name, value, len);
}

/*  The port a number is bound to, asked of the kernel directly.  */
static int
port_of_number(int fd)
{
    struct sockaddr_storage addr;
    socklen_t               len = sizeof addr;

    if (syscall(SYS_getsockname, fd, (struct sockaddr *) &addr, &len) != 0)
        return -1;
    if (addr.ss_family == AF_INET)
        return (int) ntohs(((struct sockaddr_in *) &addr)->sin_port);
    return -1;
}

static void
closer_main(void *arg)
{
    int64_t     deadline = ST_time_monotonic_ns() + INT64_C(5000000000);
    uintptr_t   old_read;
    uintptr_t   old_write;

    (void) arg;
    while (!ST_load_acquire(&closer_go)) {
        if (ST_time_monotonic_ns() > deadline)
            return;
        ST_sleep_ns(1000000);
    }
    NET_close(reissue_victim, &old_read, &old_write);
    if (reissue_kind == STRANGER_LISTENER)
        reissue_stranger = NET_listen("127.0.0.1", 0, 4);
    else
        reissue_stranger = NET_connect("127.0.0.1", reissue_port, 0);
    if (reissue_kind != STRANGER_LISTENER) {
        /*  Connected, or refused, before the stopped call goes on.  */
        struct pollfd   p;

        p.fd      = ST_load_acquire(&stop_fd) - 1;
        p.events  = POLLOUT;
        p.revents = 0;
        poll(&p, 1, 2000);
    }
    ST_store_release(&closer_done, 1);
}

/*  A connected pair on the listener: *client and *server, or 0.  */
static int
pair_on(int64_t listener, int port, int64_t *client, int64_t *server)
{
    int64_t deadline = ST_time_monotonic_ns() + INT64_C(5000000000);

    *client = NET_connect("127.0.0.1", port, 0);
    if (*client < 0)
        return 0;
    while ((*server = NET_accept(listener)) == NET_WOULD_BLOCK) {
        if (ST_time_monotonic_ns() > deadline)
            return 0;
        ST_sleep_ns(1000000);
    }
    while (NET_connect_result(*client) == NET_WOULD_BLOCK) {
        if (ST_time_monotonic_ns() > deadline)
            return 0;
        ST_sleep_ns(1000000);
    }
    return *server >= 0;
}

/*  Arm the stop, start the closer; the caller makes the call, then joins.  */
static st_thread
arm_reissue(int which, int64_t victim, int kind, int port)
{
    st_thread   t;

    ST_store_relaxed(&stop_fd, 0);
    ST_store_relaxed(&closer_go, 0);
    ST_store_relaxed(&closer_done, 0);
    reissue_victim   = victim;
    reissue_kind     = kind;
    reissue_port     = port;
    reissue_stranger = -1;
    reissue_raced    = 0;
    ST_thread_create(&t, closer_main, NULL);
    stop_armed = which;
    return t;
}

static void
test_bugs7f_net_reissue(void)
{
    int64_t     listener;
    int         port;
    int64_t     client;
    int64_t     server;
    int64_t     accepted;
    int64_t     deadline;
    st_thread   t;
    int         number;
    int         answer;
    int         expected;
    int         nodelay;
    socklen_t   len;
    char        text[64];
    char        buffer[16];
    uintptr_t   r;
    uintptr_t   w;

    listener = NET_listen("127.0.0.1", 0, 8);
    CHECK(listener >= 0);
    port = NET_local_port(listener);

    /*
     *  NET_shutdown_write.  The stranger is a new connection to the same
     *  listener; its server side must not read end of stream.
     */
    CHECK(pair_on(listener, port, &client, &server));
    t = arm_reissue(STOP_SHUTDOWN, client, STRANGER_CONNECTED, port);
    CHECK_EQ_INT(NET_shutdown_write(client), 0);
    stop_armed = STOP_NONE;
    ST_thread_join(t);
    CHECK_EQ_INT(reissue_raced, 0);
    number = ST_load_acquire(&stop_fd) - 1;
    CHECK(reissue_stranger >= 0);
    CHECK_EQ_INT(port_of_number(number), NET_local_port(reissue_stranger));
    deadline = ST_time_monotonic_ns() + INT64_C(5000000000);
    while ((accepted = NET_accept(listener)) == NET_WOULD_BLOCK
        && ST_time_monotonic_ns() < deadline)
        ST_sleep_ns(1000000);
    CHECK(accepted >= 0);
    ST_sleep_ns(20000000);
    CHECK_EQ_INT((int) NET_recv(accepted, buffer, sizeof buffer), NET_WOULD_BLOCK);
    /*  And the socket asked about was the one shut: its peer reads the end.  */
    CHECK_EQ_INT((int) NET_recv(server, buffer, sizeof buffer), 0);
    NET_close(accepted, &r, &w);
    NET_close(reissue_stranger, &r, &w);
    NET_close(server, &r, &w);

    /*
     *  NET_local_port.  The stranger is a listener on a port of its own;
     *  the answer must be the client's port, read before the stop.
     */
    CHECK(pair_on(listener, port, &client, &server));
    expected = NET_local_port(client);
    t = arm_reissue(STOP_GETSOCKNAME, client, STRANGER_LISTENER, 0);
    answer = NET_local_port(client);
    stop_armed = STOP_NONE;
    ST_thread_join(t);
    CHECK_EQ_INT(reissue_raced, 0);
    number = ST_load_acquire(&stop_fd) - 1;
    CHECK(reissue_stranger >= 0);
    CHECK_EQ_INT(port_of_number(number), NET_local_port(reissue_stranger));
    CHECK_EQ_INT(answer, expected);
    NET_close(reissue_stranger, &r, &w);
    NET_close(server, &r, &w);

    /*
     *  NET_peer_address.  The asked-about socket is the server side,
     *  whose peer is 127.0.0.1; the stranger is a listener, which has no
     *  peer at all.
     */
    CHECK(pair_on(listener, port, &client, &server));
    t = arm_reissue(STOP_GETPEERNAME, server, STRANGER_LISTENER, 0);
    answer = NET_peer_address(server, text, sizeof text);
    stop_armed = STOP_NONE;
    ST_thread_join(t);
    CHECK_EQ_INT(reissue_raced, 0);
    number = ST_load_acquire(&stop_fd) - 1;
    CHECK(reissue_stranger >= 0);
    CHECK_EQ_INT(port_of_number(number), NET_local_port(reissue_stranger));
    CHECK_EQ_INT(answer, 0);
    CHECK_EQ_STR(answer == 0 ? text : NET_last_error(), "127.0.0.1");
    NET_close(reissue_stranger, &r, &w);
    NET_close(client, &r, &w);

    /*
     *  NET_set_option.  Nagle off on the client; the stranger, a new
     *  connection, must keep the default.
     */
    CHECK(pair_on(listener, port, &client, &server));
    t = arm_reissue(STOP_SETSOCKOPT, client, STRANGER_CONNECTED, port);
    CHECK_EQ_INT(NET_set_option(client, NET_OPTION_NODELAY, 1), 0);
    stop_armed = STOP_NONE;
    ST_thread_join(t);
    CHECK_EQ_INT(reissue_raced, 0);
    number = ST_load_acquire(&stop_fd) - 1;
    CHECK(reissue_stranger >= 0);
    CHECK_EQ_INT(port_of_number(number), NET_local_port(reissue_stranger));
    nodelay = -1;
    len     = sizeof nodelay;
    syscall(SYS_getsockopt, number, IPPROTO_TCP, TCP_NODELAY, &nodelay, &len);
    CHECK_EQ_INT(nodelay, 0);
    NET_close(reissue_stranger, &r, &w);
    NET_close(server, &r, &w);
    deadline = ST_time_monotonic_ns() + INT64_C(2000000000);
    while ((accepted = NET_accept(listener)) == NET_WOULD_BLOCK
        && ST_time_monotonic_ns() < deadline)
        ST_sleep_ns(1000000);
    if (accepted >= 0)
        NET_close(accepted, &r, &w);

    /*
     *  NET_connect_result.  The client's connect succeeded; the stranger's
     *  is refused -- a port whose listener has gone -- and its SO_ERROR
     *  must not be read as the client's.
     */
    {
        int64_t gone      = NET_listen("127.0.0.1", 0, 1);
        int     gone_port = NET_local_port(gone);

        NET_close(gone, &r, &w);
        CHECK(pair_on(listener, port, &client, &server));
        t = arm_reissue(STOP_GETSOCKOPT, client, STRANGER_REFUSED, gone_port);
        answer = NET_connect_result(client);
        stop_armed = STOP_NONE;
        ST_thread_join(t);
        CHECK_EQ_INT(reissue_raced, 0);
        CHECK(reissue_stranger >= 0);
        CHECK_EQ_INT(answer, 0);
        if (answer != 0)
            printf("  NET-15 connect result: %s\n", NET_last_error());
        NET_close(reissue_stranger, &r, &w);
        NET_close(server, &r, &w);
    }

    NET_close(listener, &r, &w);
    CHECK_EQ_INT(NET_open_count(), 0);
}

#endif  /*  REISSUE_TEST  */

int
main(void)
{
    int64_t     listener;
    int64_t     client;
    int64_t     server;
    int         port;
    char        buffer[256];
    long        n;
    uintptr_t   old_read;
    uintptr_t   old_write;

    ST_TEST_BEGIN("the socket layer, in C");

    CHECK_EQ_INT(NET_init(counting_hook), 0);
    CHECK_EQ_INT(NET_init(counting_hook), 0);       /*  again is fine  */
    test_addresses();
    CHECK_EQ_INT(NET_available(), 1);
    CHECK_EQ_INT(NET_open_count(), 0);
#ifdef REISSUE_TEST
    test_bugs7f_net_reissue();
#else
    printf("  skipped NET-15's reissue test: not Linux with glibc\n");
#endif
    CHECK_EQ_INT(NET_waits_pending(), 0);

    /*  ----------  A listener, a connection  ----------  */

    listener = NET_listen("127.0.0.1", 0, 8);
    if (listener < 0)
        printf("  listen: %s\n", NET_last_error());
    CHECK(listener >= 0);
    port = NET_local_port(listener);
    CHECK(port > 0);
    CHECK_EQ_INT(NET_open_count(), 1);
    CHECK_EQ_INT(NET_set_tokens(listener, TOKEN_LISTEN_READ, 0,
                                &old_read, &old_write), 0);
    CHECK_EQ_INT((int) old_read, 0);

    /*  Nobody has connected: accept cannot complete, and says so.  */
    CHECK_EQ_INT((int) NET_accept(listener), NET_WOULD_BLOCK);

    client = NET_connect("127.0.0.1", port, 0);
    if (client < 0)
        printf("  connect: %s\n", NET_last_error());
    CHECK(client >= 0);
    CHECK_EQ_INT(NET_set_tokens(client, TOKEN_CLIENT_READ, TOKEN_CLIENT_WRITE,
                                &old_read, &old_write), 0);

    /*  The connect completes when the socket is writable.  */
    CHECK(arm_and_wait(client, NET_ARM_WRITE, TOKEN_CLIENT_WRITE));
    CHECK_EQ_INT(NET_connect_result(client), 0);
    CHECK_EQ_INT(NET_waits_pending(), 0);           /*  one-shot: cleared  */

    /*  And the listener is readable: a connection is waiting.  */
    CHECK(arm_and_wait(listener, NET_ARM_READ, TOKEN_LISTEN_READ));
    server = NET_accept(listener);
    if (server < 0)
        printf("  accept: %s\n", NET_last_error());
    CHECK(server >= 0);
    CHECK_EQ_INT(NET_open_count(), 3);
    CHECK_EQ_INT(NET_set_tokens(server, TOKEN_SERVER_READ, TOKEN_SERVER_WRITE,
                                &old_read, &old_write), 0);

    /*  ----------  Bytes both ways  ----------  */

    /*  Nothing sent yet: recv would block.  */
    CHECK_EQ_INT((int) NET_recv(server, buffer, sizeof buffer), NET_WOULD_BLOCK);

    n = NET_send(client, "hello, server", 13);
    CHECK_EQ_INT((int) n, 13);
    CHECK(arm_and_wait(server, NET_ARM_READ, TOKEN_SERVER_READ));
    n = NET_recv(server, buffer, sizeof buffer);
    CHECK_EQ_INT((int) n, 13);
    buffer[13] = '\0';
    CHECK_EQ_STR(buffer, "hello, server");

    n = NET_send(server, "hello, client", 13);
    CHECK_EQ_INT((int) n, 13);
    CHECK(arm_and_wait(client, NET_ARM_READ, TOKEN_CLIENT_READ));
    n = NET_recv(client, buffer, sizeof buffer);
    CHECK_EQ_INT((int) n, 13);
    buffer[13] = '\0';
    CHECK_EQ_STR(buffer, "hello, client");

    /*
     *  A signal per arm, not per poll.  The server socket has been
     *  readable since the client wrote, and was signalled exactly once,
     *  because the interest was cleared with the signal.
     */
    CHECK_EQ_INT(signals_for(TOKEN_SERVER_READ), 1);

    /*  ----------  A dropped signal is delivered later  ----------  */

    ST_store_seq(&refusals_left, 2);
    ST_store_seq(&refused, 0);
    n = NET_send(client, "again", 5);
    CHECK_EQ_INT((int) n, 5);
    CHECK(arm_and_wait(server, NET_ARM_READ, TOKEN_SERVER_READ));
    CHECK_EQ_INT(ST_load_relaxed(&refused), 2);     /*  it was refused twice  */
    CHECK_EQ_INT(signals_for(TOKEN_SERVER_READ), 2); /*  and then delivered  */
    CHECK_EQ_INT(NET_waits_pending(), 0);
    n = NET_recv(server, buffer, sizeof buffer);
    CHECK_EQ_INT((int) n, 5);

    /*  ----------  The peer  ----------  */

    CHECK_EQ_INT(NET_peer_address(server, buffer, sizeof buffer), 0);
    CHECK_EQ_STR(buffer, "127.0.0.1");
    CHECK_EQ_INT(NET_set_option(server, NET_OPTION_NODELAY, 1), 0);
    CHECK_EQ_INT(NET_set_option(server, 99, 1), -1);

    /*  ----------  The tokens, for the collector  ----------  */

    CHECK(NET_holds_token(TOKEN_SERVER_READ));
    CHECK(NET_holds_token(TOKEN_CLIENT_WRITE));
    CHECK(!NET_holds_token(4242));
    visited_count = 0;
    NET_visit_tokens(record, NULL);
    CHECK_EQ_INT((int) visited_count, 5);           /*  1 + 2 + 2  */
    {
        unsigned    i;
        int         saw_server_read = 0;

        for (i = 0; i < visited_count && i < 16; ++i)
            if (visited[i] == TOKEN_SERVER_READ)
                saw_server_read = 1;
        CHECK(saw_server_read);
    }

    /*  ----------  Half-close and end of stream  ----------  */

    CHECK_EQ_INT(NET_shutdown_write(client), 0);
    CHECK(arm_and_wait(server, NET_ARM_READ, TOKEN_SERVER_READ));
    n = NET_recv(server, buffer, sizeof buffer);
    CHECK_EQ_INT((int) n, 0);                       /*  end of stream  */
    /*  The other direction still works after a half-close.  */
    n = NET_send(server, "bye", 3);
    CHECK_EQ_INT((int) n, 3);
    CHECK(arm_and_wait(client, NET_ARM_READ, TOKEN_CLIENT_READ));
    n = NET_recv(client, buffer, sizeof buffer);
    CHECK_EQ_INT((int) n, 3);

    /*  ----------  Closing, and what a closed handle answers  ----------  */

    CHECK_EQ_INT(NET_close(client, &old_read, &old_write), 0);
    CHECK_EQ_INT((int) old_read, TOKEN_CLIENT_READ);
    CHECK_EQ_INT((int) old_write, TOKEN_CLIENT_WRITE);
    CHECK_EQ_INT(NET_open_count(), 2);
    CHECK(!NET_holds_token(TOKEN_CLIENT_READ));

    /*  The handle is dead: every call refuses it, none reaches a socket.  */
    CHECK_EQ_INT((int) NET_recv(client, buffer, sizeof buffer), -1);
    CHECK(strstr(NET_last_error(), "no such socket") != NULL);
    CHECK_EQ_INT(NET_arm(client, NET_ARM_READ), -1);
    CHECK_EQ_INT(NET_close(client, NULL, NULL), -1);

    /*
     *  A handle with the right index and the wrong generation -- what an
     *  image resumed from a snapshot holds -- is refused the same way, and
     *  does not resolve to the socket that now lives in that slot.
     */
    {
        int64_t forged = (int64_t) ((((uint64_t) server >> 12) + 7) << 12)
                       | (int64_t) ((uint64_t) server & 0xFFF);

        CHECK_EQ_INT((int) NET_recv(forged, buffer, sizeof buffer), -1);
        CHECK_EQ_INT(NET_local_port(forged), -1);
    }
    CHECK_EQ_INT(NET_local_port(-1), -1);
    CHECK_EQ_INT(NET_local_port(INT64_C(1) << 40), -1);

    /*  Reading the server end now finds the peer gone.  */
    CHECK(arm_and_wait(server, NET_ARM_READ, TOKEN_SERVER_READ));
    n = NET_recv(server, buffer, sizeof buffer);
    CHECK(n == 0 || n == -1);

    /*  ----------  Two OS services  ----------  */

    {
        unsigned char   one[16];
        unsigned char   two[16];
        unsigned        i;
        int             zero = 1;

        memset(one, 0, sizeof one);
        memset(two, 0, sizeof two);
        CHECK_EQ_INT(NET_random_bytes(one, sizeof one), 0);
        CHECK_EQ_INT(NET_random_bytes(two, sizeof two), 0);
        for (i = 0; i < sizeof one; ++i)
            if (one[i] != 0)
                zero = 0;
        CHECK(!zero);
        CHECK(memcmp(one, two, sizeof one) != 0);
    }
    {
        char   *argv[] = { "server.json", "-eval", "1 + 1" };

        NET_set_arguments(3, argv);
        CHECK_EQ_INT(NET_argument_count(), 3);
        CHECK_EQ_STR(NET_argument(0), "server.json");
        CHECK_EQ_STR(NET_argument(2), "1 + 1");
        CHECK(NET_argument(3) == NULL);
        CHECK(NET_argument(-1) == NULL);
    }

    CHECK(NET_environment("PATH") != NULL);
    CHECK(NET_environment("ST_NO_SUCH_VARIABLE_FOR_THIS_TEST") == NULL);
    CHECK(NET_environment("") == NULL);
    CHECK(NET_environment(NULL) == NULL);

    /*  ----------  TLS, against a peer that does not speak it  ----------  */

    /*
     *  There is no TLS server in this system, and a certificate to test
     *  one with would have to be made, so what is checked here is the
     *  shape: TLS attaches to a connected socket and not to a listener, a
     *  stranger or a socket that has it already; the handshake waits the
     *  way everything here waits, saying which way; and a peer that answers
     *  in plain text is refused with OpenSSL's words rather than accepted
     *  or hung on.  The real thing, against a real certificate, is checked
     *  where a real server is: profiles/internet-live.profile.
     */
    {
        int64_t     l2;
        int64_t     c2;
        int64_t     s2;
        int         p2;
        int         rc;
        int         spins;

        l2 = NET_listen("127.0.0.1", 0, 8);
        CHECK(l2 >= 0);
        p2 = NET_local_port(l2);
        CHECK_EQ_INT(NET_set_tokens(l2, TOKEN_LISTEN_READ, 0, &old_read, &old_write), 0);
        c2 = NET_connect("127.0.0.1", p2, 0);
        CHECK(c2 >= 0);
        CHECK_EQ_INT(NET_set_tokens(c2, TOKEN_CLIENT_READ, TOKEN_CLIENT_WRITE,
                                    &old_read, &old_write), 0);
        CHECK(arm_and_wait(c2, NET_ARM_WRITE, TOKEN_CLIENT_WRITE));
        CHECK_EQ_INT(NET_connect_result(c2), 0);
        CHECK(arm_and_wait(l2, NET_ARM_READ, TOKEN_LISTEN_READ));
        s2 = NET_accept(l2);
        CHECK(s2 >= 0);
        CHECK_EQ_INT(NET_set_tokens(s2, TOKEN_SERVER_READ, TOKEN_SERVER_WRITE,
                                    &old_read, &old_write), 0);

        CHECK_EQ_INT(NET_is_tls(c2), 0);
        CHECK_EQ_INT(NET_tls_start(l2, "localhost"), -1);              /*  a listener  */
        CHECK_EQ_INT(NET_tls_start(c2 + (1 << 20), "localhost"), -1);  /*  a stranger  */
        CHECK_EQ_INT(NET_tls_start(c2, ""), -1);                       /*  no name  */
        CHECK_EQ_INT(NET_tls_handshake(c2), -1);                       /*  not started  */
        CHECK(strstr(NET_last_error(), "not started") != NULL);
        if (NET_tls_available()) {
            CHECK_EQ_INT(NET_tls_start(c2, "localhost"), 0);
            CHECK_EQ_INT(NET_is_tls(c2), 1);
            CHECK_EQ_INT(NET_tls_start(c2, "localhost"), -1);          /*  once  */
            /*  The client hello goes out; the handshake waits for the answer.  */
            rc = NET_tls_handshake(c2);
            CHECK(rc == NET_WANT_READ || rc == NET_WANT_WRITE);
            /*  The "server" reads a TLS record and answers in plain text.  */
            CHECK(arm_and_wait(s2, NET_ARM_READ, TOKEN_SERVER_READ));
            n = NET_recv(s2, buffer, sizeof buffer);
            CHECK(n > 5);
            CHECK_EQ_INT((unsigned char) buffer[0], 0x16);             /*  a handshake record  */
            CHECK_EQ_INT((int) NET_send(s2, "HTTP/1.1 400 Bad Request\r\n\r\n", 28), 28);
            for (spins = 0; spins < 50 && (rc == NET_WANT_READ || rc == NET_WANT_WRITE); ++spins) {
                CHECK(arm_and_wait(c2, rc == NET_WANT_READ ? NET_ARM_READ : NET_ARM_WRITE,
                                   rc == NET_WANT_READ ? TOKEN_CLIENT_READ : TOKEN_CLIENT_WRITE));
                rc = NET_tls_handshake(c2);
            }
            CHECK_EQ_INT(rc, -1);
            printf("  tls refused a plain peer: %s\n", NET_last_error());
            CHECK(strstr(NET_last_error(), "TLS handshake") != NULL);
            /*  A state whose handshake failed carries nothing after.  */
            CHECK_EQ_INT((int) NET_recv(c2, buffer, sizeof buffer), -1);
            CHECK_EQ_INT((int) NET_send(c2, "x", 1), -1);
        } else {
            CHECK_EQ_INT(NET_tls_start(c2, "localhost"), -1);
            CHECK(strstr(NET_last_error(), "no TLS") != NULL);
            CHECK_EQ_INT(NET_is_tls(c2), 0);
        }
        CHECK_EQ_INT(NET_close(c2, &old_read, &old_write), 0);
        CHECK_EQ_INT(NET_close(s2, &old_read, &old_write), 0);
        CHECK_EQ_INT(NET_close(l2, &old_read, &old_write), 0);
    }

    /*  ----------  Down, and up again  ----------  */

    /*  Armed at shutdown, so the count comes down with the table.  */
    CHECK_EQ_INT(NET_arm(server, NET_ARM_WRITE), 0);
    NET_shutdown();
    CHECK_EQ_INT(NET_waits_pending(), 0);

    CHECK_EQ_INT(NET_init(counting_hook), 0);
    CHECK_EQ_INT(NET_open_count(), 0);
    /*  A handle from the previous life is nobody's.  */
    CHECK_EQ_INT((int) NET_recv(server, buffer, sizeof buffer), -1);
    CHECK_EQ_INT(NET_close(listener, NULL, NULL), -1);
    NET_shutdown();

    return ST_TEST_END();
}

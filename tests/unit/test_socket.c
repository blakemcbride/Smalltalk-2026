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
    CHECK_EQ_INT(NET_available(), 1);
    CHECK_EQ_INT(NET_open_count(), 0);
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

    client = NET_connect("127.0.0.1", port);
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

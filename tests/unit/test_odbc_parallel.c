/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The database, from real worker threads, with the world stopping under it.
 *
 *  This is the only test that exercises what doc/DATABASE.md claims, and it
 *  exists because discovering that was uncomfortable.  lib/Database-Live-Tests
 *  runs 129 tests through a real driver and proves a great deal -- but a
 *  `st80 -tests' run starts no worker pool, so its forked processes are green
 *  and WORKER_enter_native answers immediately, current_worker being NULL.
 *  The parking path, which is the entire reason a query does not stall every
 *  core, was never once taken.
 *
 *  So: N native threads, each with its own connection, each running
 *  statements, while the main thread repeatedly stops the world.  Two
 *  properties are checked and neither is a hope that a sanitizer noticed.
 *
 *      A safepoint is REACHED while workers are inside the driver.  If
 *      WORKER_enter_native did not count a blocked worker as parked, the
 *      request would wait for a thread that is waiting for a database and
 *      the test would hang rather than fail -- which is why the counter is
 *      checked afterwards rather than trusted.
 *
 *      Every worker's own rows come back, exactly.  Each writes rows only it
 *      writes and counts only its own, so a connection handed to two threads
 *      or an indicator read from a moved parameter shows up as arithmetic
 *      rather than as a crash.  The second of those is not hypothetical:
 *      the parameters used to live in one realloc'd array and the driver was
 *      reading freed memory.  See st_odbc_param in src/db/st_odbc.c.
 *
 *  Run it where it earns its keep:
 *
 *      make OM=mt TSAN=1 unit-test
 *
 *  It SKIPS, rather than fails, without ODBC or without a SQLite driver.
 *  A machine building this system is not assumed to have a database, and a
 *  test that failed for want of one would be noise on every such machine --
 *  which is how a real failure stops being read.
 */

#include "st_test.h"

#if defined(ST_OM_MT) && defined(ST_HAVE_ODBC)

#include "om.h"
#include "interp.h"
#include "worker.h"
#include "st_sched.h"
#include "st_odbc.h"
#include "st_atomic.h"

#include <stdio.h>
#include <string.h>

#define ROWS_PER_WORKER     40
/*
 *  A ceiling on the asker's rounds.
 *
 *  It is a runaway guard and it is also, honestly, a limit that gets
 *  reached: a request/release pair over parked workers costs a few
 *  microseconds, so the asker gets through its budget well before the
 *  workers get through theirs.  That is fine.  What the test needs is that
 *  stopping the world OVERLAPS the database work, not that it continues to
 *  the last statement, and 200,000 of them against 1,240 statements is not
 *  a coverage question.
 *
 *  A FIXED SMALL number was the first version and it was worthless: thirty
 *  rounds complete in microseconds, so they were over before the first
 *  worker had opened its connection.  Combined with telling the asker to
 *  stop between WORKER_start and WORKER_stop -- which is not where the
 *  workers finish, see main -- the run reported one safepoint, passed every
 *  assertion, and measured nothing.  Both mistakes are recorded because
 *  either one alone produces a green test that tests nothing.
 */
#define SAFEPOINT_CEILING   200000
#define DATABASE_FILE       "st80-odbc-parallel-test.db"

/*
 *  Whether a database is reachable at all.  Decided once, before any thread
 *  starts, so that a machine without a driver skips instead of having N
 *  threads each discover the same absence.
 */
static int          have_database;
static char         connection_string[512];

static st_atomic_int    rows_written;
static st_atomic_int    worker_failures;
static st_atomic_int    workers_running;
static st_atomic_int    stop_asking;
static st_atomic_int    safepoints_asked;
static st_atomic_int    query_running;
static int64_t          query_ns;
static int64_t          request_ns;
static st_atomic_int    request_timed;

/*  What each worker counted for itself, indexed by worker.  */
static int          counted[ST_MAX_WORKERS];

static void
note_failure(const char *what)
{
    fprintf(stderr, "  worker: %s: %s\n", what, ST_odbc_last_error());
    ST_fetch_add_acq_rel(&worker_failures, 1);
}

/*
 *  One worker: open, write ROWS_PER_WORKER rows nobody else writes, read
 *  back its own count, close.
 *
 *  Every statement is prepared once and run many times, which is not
 *  incidental -- it is what makes the parameters be re-bound repeatedly, and
 *  re-binding is where the address-stability bug lived.
 */
static void
database_worker(st_worker *self, void *user)
{
    int         connection;
    int         statement;
    int         id = (int) self->index;
    int         n;

    (void) user;
    ST_fetch_add_acq_rel(&workers_running, 1);

    connection = ST_odbc_connect(connection_string);
    if (connection < 0) {
        note_failure("connect");
        ST_fetch_sub_acq_rel(&workers_running, 1);
        return;
    }

    statement = ST_odbc_prepare(connection,
                                "INSERT INTO t (worker, n, note) "
                                "VALUES (?, ?, ?)");
    if (statement < 0) {
        note_failure("prepare");
        ST_odbc_disconnect(connection);
        ST_fetch_sub_acq_rel(&workers_running, 1);
        return;
    }

    for (n = 0; n < ROWS_PER_WORKER; ++n) {
        char    note[64];

        /*
         *  A note whose LENGTH varies with n, deliberately.  A fixed-width
         *  value would be bound into the same buffer every time and would
         *  never make a parameter grow; the bug this test was written after
         *  needed the growth.  It is also checked coming back: a stale
         *  indicator is a wrong length, and the symptom is a string quietly
         *  one or two characters short.
         */
        snprintf(note, sizeof note, "worker-%d-row-%0*d", id,
                 1 + (n % 8), n);

        if (ST_odbc_clear_parameters(statement) != 0
         || ST_odbc_bind_int(statement, 1, id) != 0
         || ST_odbc_bind_int(statement, 2, n) != 0
         || ST_odbc_bind_string(statement, 3, note, strlen(note)) != 0) {
            note_failure("bind");
            break;
        }
        if (ST_odbc_execute(statement) != 0) {
            /*
             *  SQLite locks the whole file for a write, so two workers can
             *  genuinely collide here.  That is the database's business and
             *  not this system's; retry once and count a real failure only
             *  if it persists.
             */
            if (ST_odbc_execute(statement) != 0) {
                note_failure("execute");
                break;
            }
        }
        if (ST_odbc_commit(connection) != 0) {
            if (ST_odbc_commit(connection) != 0) {
                note_failure("commit");
                break;
            }
        }
        ST_fetch_add_acq_rel(&rows_written, 1);

        /*
         *  Poll between statements, as an interpreter does between
         *  bytecodes.
         *
         *  Without this the test models something no real worker does.  A
         *  worker running Smalltalk alternates between bytecodes, where it
         *  parks at WORKER_poll, and primitives, where it parks at
         *  WORKER_enter_native; this loop is pure C and would only ever
         *  take the second path.  That is not merely unrealistic, it makes
         *  the test weak: a safepoint needs EVERY worker parked, so with
         *  thirty-one of them polling nowhere, one could only be reached in
         *  the instant all thirty-one happened to be inside the driver at
         *  once.  It happened exactly once in a run of 1,240 statements --
         *  which does prove that a native region counts as parked, and
         *  proves it on a sample of one.
         *
         *  With the poll here the two paths mix, which is both the real
         *  case and the interesting one: some workers park because they
         *  polled, others because they are blocked in SQLExecute, and the
         *  safepoint has to be reached across both.
         */
        WORKER_poll();
    }
    ST_odbc_close_statement(statement);

    /*  Read back only this worker's own rows.  */
    {
        int     query = ST_odbc_prepare(connection,
                            "SELECT COUNT(*), MIN(LENGTH(note)) FROM t "
                            "WHERE worker = ?");

        if (query < 0)
            note_failure("prepare count");
        else {
            if (ST_odbc_bind_int(query, 1, id) != 0
             || ST_odbc_execute(query) != 0)
                note_failure("count");
            else if (ST_odbc_fetch(query) == 1) {
                st_odbc_value   value;

                if (ST_odbc_get(query, 1, &value) == 0
                 && value.kind == ST_ODBC_INT)
                    counted[id] = (int) value.i;
                /*
                 *  The shortest note this worker wrote.  Every note is at
                 *  least strlen("worker-N-row-0") long, so a truncated bind
                 *  makes this smaller -- which is the silent form of the
                 *  parameter bug, caught as a number rather than as a
                 *  segmentation fault that may not come.
                 */
                if (ST_odbc_get(query, 2, &value) == 0
                 && value.kind == ST_ODBC_INT && value.i < 14) {
                    fprintf(stderr, "  worker %d: a note came back %d "
                                    "characters long -- truncated\n",
                            id, (int) value.i);
                    ST_fetch_add_acq_rel(&worker_failures, 1);
                }
            }
            ST_odbc_close_statement(query);
        }
    }

    ST_odbc_disconnect(connection);
    ST_fetch_sub_acq_rel(&workers_running, 1);
}

/*
 *  One worker, one query that takes real time, and no poll inside it.
 *
 *  A recursive CTE counting to three million: SQLite computes it row by row
 *  in one SQLExecute, so the worker is inside the driver for the whole of it
 *  and reaches no polling point at all.  That is the shape of a report, and
 *  it is the shape this system has to survive.
 */
static void
slow_query_worker(st_worker *self, void *user)
{
    int         connection;
    int         statement;
    int64_t     began;

    (void) self;
    (void) user;

    connection = ST_odbc_connect(connection_string);
    if (connection < 0) {
        note_failure("connect (slow)");
        return;
    }
    statement = ST_odbc_prepare(connection,
        "WITH RECURSIVE c(x) AS ("
        "  SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 3000000)"
        " SELECT COUNT(*) FROM c");
    if (statement < 0) {
        note_failure("prepare (slow)");
        ST_odbc_disconnect(connection);
        return;
    }

    began = ST_time_monotonic_ns();
    ST_store_seq(&query_running, 1);
    if (ST_odbc_execute(statement) != 0)
        note_failure("execute (slow)");
    else if (ST_odbc_fetch(statement) != 1)
        note_failure("fetch (slow)");
    ST_store_seq(&query_running, 0);
    query_ns = ST_time_monotonic_ns() - began;

    ST_odbc_close_statement(statement);
    ST_odbc_disconnect(connection);
}

/*
 *  Wait for the query to be running, then time exactly one safepoint.
 *
 *  Timed from this thread and not from the worker, because the question is
 *  how long the REQUESTER waits -- which is what a collector would wait, and
 *  what stalls every other core.
 */
static void
time_one_safepoint(void *unused)
{
    int64_t     began;

    (void) unused;

    /*
     *  Give up rather than spin for ever.  If the query never starts -- a
     *  driver that cannot prepare it, a fixture that failed -- this thread
     *  would otherwise never return and WORKER_stop would join a thread that
     *  is waiting for something that is not coming.  A hung suite says less
     *  than a failed one, and main checks request_timed so that giving up
     *  here is reported rather than passed over.
     */
    {
        long    spins = 0;

        while (ST_load_seq(&query_running) == 0) {
            if (++spins > 100000000L)
                return;
            ST_thread_yield();
        }
    }
    /*
     *  A yield after seeing the flag, so the request lands inside
     *  SQLExecute rather than in the instant before it.  The flag is set
     *  just before the call and the call is seconds long, so this is
     *  belt and braces rather than a race being papered over.
     */
    ST_thread_yield();

    began = ST_time_monotonic_ns();
    WORKER_request_safepoint();
    request_ns = ST_time_monotonic_ns() - began;
    WORKER_release_safepoint();
    ST_store_seq(&request_timed, 1);
}

/*
 *  Set the fixture up, and answer whether there is a database at all.
 *
 *  Uses SQLite through ODBC for the reason lib/Database-Live-Tests does: it
 *  is the one database needing no server, so the whole fixture is a file.
 */
static int
prepare_database(void)
{
    int     connection;

    snprintf(connection_string, sizeof connection_string,
             "DRIVER=SQLITE3;Database=%s;", DATABASE_FILE);

    if (!ST_odbc_available())
        return 0;
    connection = ST_odbc_connect(connection_string);
    if (connection < 0) {
        /*  A driver manager with no SQLite driver.  Not a failure.  */
        return 0;
    }
    if (ST_odbc_execute_direct(connection, "DROP TABLE IF EXISTS t", NULL) != 0
     || ST_odbc_execute_direct(connection,
            "CREATE TABLE t (worker INTEGER, n INTEGER, note VARCHAR(64))",
            NULL) != 0
     || ST_odbc_commit(connection) != 0) {
        ST_odbc_disconnect(connection);
        return 0;
    }
    ST_odbc_disconnect(connection);
    return 1;
}

int
main(void)
{
    unsigned    workers;
    unsigned    i;
    int         safepoints_taken;
    int         total = 0;

    ST_TEST_BEGIN("the database, from real threads, with the world stopping");

    have_database = prepare_database();
    if (!have_database) {
        printf("skipped: no ODBC driver manager, or no SQLITE3 driver\n");
        printf("  (%s)\n", ST_odbc_last_error());
        return 0;
    }

    if (OM_init() != 0) {
        printf("  cannot initialize the object memory\n");
        CHECK(0);
        return ST_TEST_END();
    }
    ST_interp_register();

    ST_store_seq(&rows_written, 0);
    ST_store_seq(&worker_failures, 0);
    ST_store_seq(&workers_running, 0);
    ST_store_seq(&stop_asking, 0);
    ST_store_seq(&safepoints_asked, 0);
    memset(counted, 0, sizeof counted);

    safepoints_taken = WORKER_safepoint_count();

    /*
     *  Start the pool and, from this thread, stop the world repeatedly while
     *  it runs.  WORKER_start blocks until the workers finish, so the
     *  safepoints are asked for from here BEFORE it returns -- which is why
     *  the requests are made by a thread that is not in the pool.
     *
     *  They cannot be made after: by then there is nobody to park.  And a
     *  worker that is inside SQLExecute is precisely the case being tested,
     *  so the request has to overlap the work rather than bracket it.
     */
    {
        st_thread   asker;

        /*
         *  A thread whose whole job is to interrupt.  It asks for a
         *  safepoint, holds it for no time at all, and asks again -- the
         *  shortest possible stop, repeated, because what is being tested is
         *  whether the request can be SATISFIED while a worker is blocked,
         *  not what happens during it.
         */
        extern void  request_release_loop(void *unused);

        if (ST_thread_create(&asker, request_release_loop, NULL) != 0) {
            printf("  cannot create the safepoint thread\n");
            CHECK(0);
            ST_interp_unregister();
            OM_shutdown();
            return ST_TEST_END();
        }

        CHECK_EQ_INT(WORKER_start(0, database_worker, NULL), 0);
        workers = WORKER_count();

        /*
         *  WORKER_STOP IS THE JOIN, and WORKER_start is not.
         *
         *  Start creates the threads and returns at once; stop is what waits
         *  for their bodies to finish.  Telling the asker to stop between the
         *  two -- which is what this did at first -- ends it microseconds
         *  after it began, and the run then reports one safepoint taken
         *  before the second worker had started.  Its arithmetic still
         *  passed, because the rows are counted after everything is over,
         *  and the test measured nothing at all.
         */
        WORKER_stop();
        ST_store_seq(&stop_asking, 1);
        ST_thread_join(asker);
    }

    /*
     *  The world was actually stopped, more than once, while this ran.
     *
     *  Checked rather than assumed: if no safepoint had been reached the
     *  test would still pass its arithmetic below and would be measuring
     *  nothing at all.  That is the failure this project's own notes keep
     *  warning about -- a suite that reports success for work nobody did.
     */
    /*
     *  SEVERAL, not merely one.  A single safepoint could have landed in the
     *  gap before the first worker opened its connection or after the last
     *  one closed it, and would prove nothing about a query being
     *  interrupted.  Five is arbitrary and the point is only that it is not
     *  one; the run reports the real number, which on this machine is in the
     *  thousands.
     */
    CHECK(WORKER_safepoint_count() - safepoints_taken >= 5);
    printf("  %u threads ran %d statements through %u connections, "
           "across %d safepoints (%d asked)\n",
           workers, ST_load_seq(&rows_written), workers,
           WORKER_safepoint_count() - safepoints_taken,
           ST_load_seq(&safepoints_asked));

    CHECK_EQ_INT(ST_load_seq(&worker_failures), 0);
    CHECK_EQ_INT(ST_load_seq(&workers_running), 0);

    /*
     *  Every row written is a row found, and each worker found its own.
     *  A connection shared between two threads, or a parameter read from a
     *  moved buffer, arrives here as a wrong number.
     */
    for (i = 0; i < workers; ++i) {
        CHECK_EQ_INT(counted[i], ROWS_PER_WORKER);
        total += counted[i];
    }
    CHECK_EQ_INT(total, (int) workers * ROWS_PER_WORKER);
    CHECK_EQ_INT(ST_load_seq(&rows_written), (int) workers * ROWS_PER_WORKER);

    /*  ----------  The gate: a safepoint during ONE long query  ----------  */

    /*
     *  Everything above exercises the parking path and does not GATE it.
     *  That was checked the only way it can be -- by breaking
     *  WORKER_enter_native deliberately and running it -- and the suite
     *  stayed green, dropping from 200,000 safepoints to 3,219 and passing
     *  every assertion.  The poll between statements is what saves it:
     *  workers still park there, so a safepoint is still reached, just
     *  later.
     *
     *  So the property is not liveness while workers poll regularly.  It is
     *  LATENCY, and it becomes liveness when one does not poll -- a worker
     *  inside a single long query, which is the case the whole design exists
     *  for and the case any report against a real database produces.
     *
     *  One worker, one query long enough to measure, no polling point inside
     *  it: it is in the driver for the duration.  If a native region counts
     *  as parked, a safepoint asked for during it is granted at once.  If it
     *  does not, the request waits for the database.
     *
     *  Measured both ways, by deleting the bodies of WORKER_enter_native and
     *  WORKER_leave_native and running this:
     *
     *      with them      a 0.24s query, safepoint granted in 0.0000s
     *      without them   a 0.24s query, safepoint granted in 0.2396s
     *
     *  Three orders of magnitude and the whole query respectively, which is
     *  what makes a timing assertion honest here rather than flaky.  This
     *  test FAILS with the parking removed; everything above it stays green.
     */
    ST_store_seq(&worker_failures, 0);
    ST_store_seq(&query_running, 0);
    ST_store_seq(&request_timed, 0);
    query_ns = 0;
    request_ns = 0;

    {
        st_thread   timer;

        if (ST_thread_create(&timer, time_one_safepoint, NULL) != 0) {
            printf("  cannot create the timing thread\n");
            CHECK(0);
            ST_interp_unregister();
            OM_shutdown();
            return ST_TEST_END();
        }
        CHECK_EQ_INT(WORKER_start(1, slow_query_worker, NULL), 0);
        WORKER_stop();
        ST_thread_join(timer);
    }

    CHECK_EQ_INT(ST_load_seq(&worker_failures), 0);

    /*
     *  THE MEASUREMENT HAPPENED.
     *
     *  Without this the comparison below is vacuous: request_ns starts at
     *  zero, and a timing thread that never reached its request leaves it
     *  there -- so `request_ns * 4 < query_ns' passes by arithmetic on a
     *  number nobody measured.  That is the same failure the rest of this
     *  file keeps warning about, and it was in this test until it was
     *  looked for.
     */
    CHECK_EQ_INT(ST_load_seq(&request_timed), 1);

    /*
     *  The query has to have been long enough for the answer to mean
     *  something.  A database that optimised it away would leave both
     *  numbers near zero and the comparison below would be noise.
     */
    CHECK(query_ns > INT64_C(100000000));           /*  0.1s  */

    printf("  a %.2fs query was interrupted by a safepoint granted in %.4fs\n",
           (double) query_ns / 1e9, (double) request_ns / 1e9);

    /*
     *  A quarter is generous on purpose.  With the region declared the
     *  request is granted in microseconds; without it the request cannot be
     *  granted until the query returns, so the two are the whole query
     *  apart.  Anything between is a machine under load, not a design that
     *  half works.
     */
    CHECK(request_ns * 4 < query_ns);

    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

/*
 *  Ask for a safepoint, let it go, and do it again until told to stop.
 *
 *  Defined after main so that the declaration above reads next to its use.
 *  The yield between rounds is deliberate and is not a sleep: a tight loop
 *  of request/release would spend every cycle in the mutex the workers need
 *  in order to park, and the test would measure lock contention rather than
 *  the property it is about.
 */
void
request_release_loop(void *unused)
{
    long    round;

    (void) unused;

    /*
     *  WAIT FOR THE POOL TO EXIST before asking for anything.
     *
     *  WORKER_request_safepoint answers immediately, and counts nothing,
     *  while lock_ready is false -- which it is until WORKER_start sets it.
     *  This thread has to be created BEFORE WORKER_start, because that call
     *  blocks until the workers finish, so without this wait every request
     *  lands in the single-threaded path and the run reports one safepoint
     *  that stopped nobody.  That is what the first version of this test
     *  did, and its arithmetic passed while it measured nothing.
     */
    while (!ST_load_relaxed(&stop_asking)
        && ST_load_seq(&workers_running) == 0)
        ST_thread_yield();

    for (round = 0; round < SAFEPOINT_CEILING; ++round) {
        if (ST_load_relaxed(&stop_asking))
            return;
        WORKER_request_safepoint();
        WORKER_release_safepoint();
        ST_fetch_add_acq_rel(&safepoints_asked, 1);
        ST_thread_yield();
    }
}

#else   /*  no threaded memory, or no ODBC in this build  */

int
main(void)
{
#ifndef ST_OM_MT
    printf("skipped: this needs the 64-bit object memory\n");
#else
    printf("skipped: this build has no ODBC\n");
#endif
    return 0;
}

#endif

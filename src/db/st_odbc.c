/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  ODBC.  See st_odbc.h for what this is and why it is shaped this way.
 */

#include "st_odbc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ST_HAVE_ODBC

#include "worker.h"
#include "st_port.h"

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

/*
 *  How many at once.
 *
 *  A connection is per worker by design -- Kiss's own documentation says one
 *  per thread, and that is exactly this system's shape -- so the connection
 *  table only has to be as large as the machine is wide, with room for the
 *  pools an application builds on top.  Statements are per cursor and there
 *  are many more of them.  Both are fixed because a table that grows needs a
 *  lock held across the growth, and every handle a caller is holding would
 *  have to be an index into something that moved.
 */
#define ST_ODBC_MAX_CONNECTIONS     64
#define ST_ODBC_MAX_STATEMENTS      1024

/*  Long enough for a driver's diagnostic and the SQLSTATE in front of it.  */
#define ST_ODBC_ERROR_MAX           1024

/*
 *  The first read of a value asks for this much and grows only if the driver
 *  says it truncated.  Large enough that ordinary columns are one call, small
 *  enough that a thousand-column fetch is not megabytes of idle buffer.
 */
#define ST_ODBC_VALUE_INITIAL       4096

/*
 *  One bound parameter.
 *
 *  THE ADDRESS OF THIS STRUCT MUST NOT CHANGE while the statement lives.
 *  SQLBindParameter records `&indicator' and the driver reads it later, at
 *  SQLExecute -- so a parameter that moves after being bound leaves the
 *  driver reading freed memory.  That is why a statement holds an array of
 *  POINTERS to these rather than an array of these: growing the array to
 *  make room for parameter N moves the array, and would move every
 *  indicator bound before it.
 *
 *  Found by adding DbParallelTest, which crashed inside the SQLite driver's
 *  own memmove.  The crash was the loud symptom; the quiet one is worse.  A
 *  stale indicator is a wrong LENGTH, so with this bug in place two
 *  single-threaded tests that had passed all along begin failing:
 *
 *      expected 'Alan' but got 'Al'
 *      expected 'Ada'  but got 'Ad'
 *
 *  String parameters were being stored truncated -- silently, into the
 *  database, permanently.
 *
 *  It had been latent for as long as the code existed.  realloc usually
 *  grows in place, so every earlier run bound its three and five parameters
 *  without anything moving; a test that runs four processes at once changed
 *  the heap enough to make realloc relocate, and the failures then appeared
 *  in tests that had nothing to do with concurrency.  AddressSanitizer does
 *  not catch it -- its allocator does not relocate where glibc's does.
 */
typedef struct {
    unsigned char  *data;           /*  the bound bytes, ours to keep     */
    size_t          capacity;
    SQLLEN          indicator;      /*  read by the driver at execute     */
    int             bound;
} st_odbc_param;

typedef struct {
    SQLSMALLINT     sql_type;
    SQLLEN          size;
    SQLSMALLINT     decimal_digits;
    SQLSMALLINT     nullable;
    char            name[128];
} st_odbc_column;

typedef struct {
    int             in_use;
    int             connection;     /*  which connection owns it          */
    SQLHSTMT        handle;

    st_odbc_param **params;         /*  pointers: see st_odbc_param  */
    unsigned        param_count;

    st_odbc_column *columns;
    unsigned        column_count;
    int             described;      /*  columns filled in yet             */

    unsigned char  *value;          /*  the buffer st_odbc_value points into  */
    size_t          value_capacity;
} st_odbc_statement;

typedef struct {
    int             in_use;
    SQLHDBC         handle;
} st_odbc_connection;

static SQLHENV              environment;
static int                  environment_ready;

static st_odbc_connection   connections[ST_ODBC_MAX_CONNECTIONS];
static st_odbc_statement    statements[ST_ODBC_MAX_STATEMENTS];

/*
 *  One lock, over the two tables and nothing else.
 *
 *  It is taken to find or claim a slot and released before any ODBC call is
 *  made, so a worker blocked in the driver holds nothing another worker
 *  wants.  Holding it across SQLExecute would serialise every database call
 *  in the image onto one at a time, which is the precise opposite of the
 *  point of this system.
 */
static st_mutex             table_lock;
static int                  table_lock_ready;

/*
 *  Per thread, because the failure a caller wants explained is the one their
 *  own call just had.  A shared last-error is a race that reports another
 *  worker's problem as yours, and does it only under load.
 */
static _Thread_local char   error_text[ST_ODBC_ERROR_MAX];

/*  ----------  Errors  ----------  */

static void
clear_error(void)
{
    error_text[0] = '\0';
}

static void
set_error(const char *text)
{
    size_t  n = strlen(text);

    if (n >= sizeof error_text)
        n = sizeof error_text - 1;
    memcpy(error_text, text, n);
    error_text[n] = '\0';
}

/*
 *  Pull the driver's own diagnostic out of a handle.
 *
 *  Record one and only one.  A failing statement often carries several, the
 *  first being the real fault and the rest being the driver narrating its
 *  recovery, and a caller shown all of them reads the last -- which is the
 *  least informative.  SQLSTATE is kept in front because it is the part that
 *  is the same across drivers and therefore the part worth matching on.
 */
static void
record_diagnostic(SQLSMALLINT type, SQLHANDLE handle, const char *what)
{
    SQLCHAR         state[6];
    SQLCHAR         message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER      native = 0;
    SQLSMALLINT     length = 0;
    char            buffer[ST_ODBC_ERROR_MAX];

    state[0] = '\0';
    message[0] = '\0';
    if (handle != SQL_NULL_HANDLE
     && SQL_SUCCEEDED(SQLGetDiagRec(type, handle, 1, state, &native,
                                    message, sizeof message, &length))) {
        snprintf(buffer, sizeof buffer, "%s: [%s] %s",
                 what, (const char *) state, (const char *) message);
    } else {
        snprintf(buffer, sizeof buffer, "%s failed, and the driver "
                                        "reported no diagnostic", what);
    }
    set_error(buffer);
}

const char *
ST_odbc_last_error(void)
{
    return error_text;
}

int
ST_odbc_available(void)
{
    return 1;
}

/*  ----------  Blocking calls  ----------  */

/*
 *  Every ODBC call that can wait on a server goes through one of these.
 *
 *  They exist as functions rather than as a macro around each call site so
 *  that the set of calls this system considers blocking is a list one can
 *  read, and so that adding a call means adding it here rather than
 *  remembering to bracket it.  See the note in worker.h: nothing between
 *  enter and leave may touch the object memory, and nothing here does --
 *  these take C buffers only.
 */
static SQLRETURN
blocking_driver_connect(SQLHDBC dbc, SQLCHAR *in, SQLCHAR *out, SQLSMALLINT max,
                        SQLSMALLINT *out_length)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLDriverConnect(dbc, NULL, in, SQL_NTS, out, max, out_length,
                         SQL_DRIVER_NOPROMPT);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_disconnect(SQLHDBC dbc)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLDisconnect(dbc);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_prepare(SQLHSTMT stmt, SQLCHAR *sql)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLPrepare(stmt, sql, SQL_NTS);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_execute(SQLHSTMT stmt)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLExecute(stmt);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_execute_direct(SQLHSTMT stmt, SQLCHAR *sql)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLExecDirect(stmt, sql, SQL_NTS);
    WORKER_leave_native();
    return r;
}

/*
 *  Fetch blocks, and it is the one people forget.
 *
 *  A driver reads rows in blocks, so most fetches answer from memory and a
 *  minority go to the server -- which means the stall is rare, large, and
 *  arrives in the middle of a loop that looks local.  Bracketing every fetch
 *  costs two uncontended mutex operations per row against that.
 */
static SQLRETURN
blocking_fetch(SQLHSTMT stmt)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLFetch(stmt);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_get_data(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT c_type,
                  SQLPOINTER buffer, SQLLEN capacity, SQLLEN *indicator)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLGetData(stmt, column, c_type, buffer, capacity, indicator);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_end_transaction(SQLHDBC dbc, SQLSMALLINT how)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLEndTran(SQL_HANDLE_DBC, dbc, how);
    WORKER_leave_native();
    return r;
}

/*
 *  The four below are the ones it is tempting to leave out, because they
 *  look local.  Each of them reaches the server on at least one supported
 *  driver:
 *
 *      SQLFreeHandle on a statement frees a server-side cursor.
 *      SQLSetConnectAttr(CURRENT_CATALOG) is a USE, which is a statement.
 *      SQLGetInfo answers from a cache on most drivers and asks on some.
 *      SQLDescribeCol is local after an execute on most drivers, and is a
 *      round trip on the ones that describe lazily.
 *
 *  "Most drivers" is the whole problem: a call that blocks on one machine
 *  and not another produces a safepoint that stalls only in production.
 *  Bracketing costs two uncontended mutex operations, which is nothing
 *  beside any of these, so the rule here is that every call crossing into
 *  the driver manager is bracketed unless it demonstrably cannot block --
 *  and SQLBindParameter, SQLAllocHandle, SQLGetDiagRec, SQLNumResultCols
 *  and SQLRowCount are the ones that meet that bar, because each of them
 *  reads or writes client-side state the driver already holds.
 */
static SQLRETURN
blocking_free_statement(SQLHSTMT stmt)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_set_connect_attr(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                          SQLINTEGER length)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLSetConnectAttr(dbc, attribute, value, length);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_get_info(SQLHDBC dbc, SQLUSMALLINT info, SQLPOINTER out,
                  SQLSMALLINT max, SQLSMALLINT *length)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLGetInfo(dbc, info, out, max, length);
    WORKER_leave_native();
    return r;
}

static SQLRETURN
blocking_describe_column(SQLHSTMT stmt, SQLUSMALLINT column, SQLCHAR *name,
                         SQLSMALLINT name_max, SQLSMALLINT *name_length,
                         SQLSMALLINT *type, SQLULEN *size,
                         SQLSMALLINT *digits, SQLSMALLINT *nullable)
{
    SQLRETURN   r;

    WORKER_enter_native();
    r = SQLDescribeCol(stmt, column, name, name_max, name_length, type, size,
                       digits, nullable);
    WORKER_leave_native();
    return r;
}

/*
 *  The catalogue calls run a query against the database's own dictionary and
 *  block exactly as any other query does.  All four have the same shape, so
 *  they share one wrapper and pass what differs.
 */
typedef SQLRETURN (*catalogue_call)(SQLHSTMT, SQLCHAR *, SQLCHAR *, SQLCHAR *,
                                    SQLCHAR *);

/*  ----------  The tables  ----------  */

static void
lock_tables(void)
{
    if (table_lock_ready)
        ST_mutex_lock(&table_lock);
}

static void
unlock_tables(void)
{
    if (table_lock_ready)
        ST_mutex_unlock(&table_lock);
}

/*
 *  The environment, made once.
 *
 *  Not made at start-up: a system that never opens a database should not
 *  load a driver manager, and on a machine with no odbc.ini the allocation
 *  is exactly where the honest failure belongs -- at the first connect, with
 *  a message, rather than at boot.
 */
static int
ensure_environment(void)
{
    SQLHENV     env = SQL_NULL_HENV;

    if (environment_ready)
        return 0;

    if (!table_lock_ready) {
        if (ST_mutex_init(&table_lock) != 0) {
            set_error("could not create the ODBC table lock");
            return -1;
        }
        table_lock_ready = 1;
    }

    lock_tables();
    if (environment_ready) {                    /*  another worker won  */
        unlock_tables();
        return 0;
    }
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env))) {
        unlock_tables();
        set_error("no ODBC environment: the driver manager could not "
                  "allocate one");
        return -1;
    }
    /*
     *  Declaring ODBC 3 is not optional.  A driver manager told nothing
     *  assumes version 2, where the date and time type codes are different
     *  numbers -- SQL_DATE was 9 and is now 91 -- and every timestamp column
     *  would be read as something else.
     */
    if (!SQL_SUCCEEDED(SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION,
                                     (SQLPOINTER) SQL_OV_ODBC3, 0))) {
        record_diagnostic(SQL_HANDLE_ENV, env, "SQLSetEnvAttr(ODBC_VERSION)");
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        unlock_tables();
        return -1;
    }
    environment = env;
    environment_ready = 1;
    unlock_tables();
    return 0;
}

static st_odbc_connection *
connection_at(int index)
{
    if (index < 0 || index >= ST_ODBC_MAX_CONNECTIONS)
        return NULL;
    if (!connections[index].in_use)
        return NULL;
    return &connections[index];
}

static st_odbc_statement *
statement_at(int index)
{
    if (index < 0 || index >= ST_ODBC_MAX_STATEMENTS)
        return NULL;
    if (!statements[index].in_use)
        return NULL;
    return &statements[index];
}

/*
 *  A connection handle, or SQL_NULL_HDBC with the error already set.
 *
 *  The lookup is under the lock and the handle is copied out, so the caller
 *  makes its ODBC call holding nothing.  A connection cannot be closed out
 *  from under it by another worker without that worker also being wrong --
 *  closing a connection another worker is using is a program error, not a
 *  race this layer can paper over -- but the TABLE stays consistent.
 */
static SQLHDBC
connection_handle(int index)
{
    st_odbc_connection *c;
    SQLHDBC             handle = SQL_NULL_HDBC;

    lock_tables();
    c = connection_at(index);
    if (c)
        handle = c->handle;
    unlock_tables();
    if (handle == SQL_NULL_HDBC)
        set_error("no such database connection");
    return handle;
}

static SQLHSTMT
statement_handle(int index)
{
    st_odbc_statement  *s;
    SQLHSTMT            handle = SQL_NULL_HSTMT;

    lock_tables();
    s = statement_at(index);
    if (s)
        handle = s->handle;
    unlock_tables();
    if (handle == SQL_NULL_HSTMT)
        set_error("no such statement");
    return handle;
}

/*  ----------  Statements  ----------  */

static void
free_statement_slot(st_odbc_statement *s)
{
    unsigned    i;

    if (s->params) {
        for (i = 0; i < s->param_count; ++i)
            if (s->params[i]) {
                free(s->params[i]->data);
                free(s->params[i]);
            }
        free(s->params);
    }
    free(s->columns);
    free(s->value);
    memset(s, 0, sizeof *s);
}

static int
claim_statement(int connection, SQLHSTMT handle)
{
    int     i;

    lock_tables();
    for (i = 0; i < ST_ODBC_MAX_STATEMENTS; ++i) {
        if (!statements[i].in_use) {
            memset(&statements[i], 0, sizeof statements[i]);
            statements[i].in_use     = 1;
            statements[i].connection = connection;
            statements[i].handle     = handle;
            unlock_tables();
            return i;
        }
    }
    unlock_tables();
    set_error("too many open statements");
    return -1;
}

/*
 *  A new statement on a connection, allocated and registered, or -1.
 *
 *  Shared by prepare and by the four catalogue calls, all of which produce a
 *  statement the caller then fetches from.
 */
static int
new_statement(int connection, SQLHDBC dbc, SQLHSTMT *out)
{
    SQLHSTMT    stmt = SQL_NULL_HSTMT;
    int         id;

    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc, "SQLAllocHandle(STMT)");
        return -1;
    }
    id = claim_statement(connection, stmt);
    if (id < 0) {
        blocking_free_statement(stmt);
        return -1;
    }
    *out = stmt;
    return id;
}

int
ST_odbc_close_statement(int statement)
{
    st_odbc_statement  *s;
    SQLHSTMT            handle = SQL_NULL_HSTMT;

    clear_error();
    lock_tables();
    s = statement_at(statement);
    if (s) {
        handle = s->handle;
        free_statement_slot(s);             /*  clears in_use  */
    }
    unlock_tables();
    if (handle == SQL_NULL_HSTMT) {
        /*
         *  Closing something already closed is not a failure.  Kiss closes
         *  a Cursor from its own close and again from the Connection's, and
         *  making the second one an error would mean every caller had to
         *  remember which had happened.
         */
        return 0;
    }
    blocking_free_statement(handle);
    return 0;
}

/*  ----------  Connections  ----------  */

int
ST_odbc_connect(const char *connection_string)
{
    SQLHDBC     dbc = SQL_NULL_HDBC;
    SQLCHAR     completed[2048];
    SQLSMALLINT completed_length = 0;
    int         i;
    int         id = -1;

    clear_error();
    if (!connection_string || !*connection_string) {
        set_error("empty ODBC connection string");
        return -1;
    }
    if (ensure_environment() != 0)
        return -1;

    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, environment, &dbc))) {
        record_diagnostic(SQL_HANDLE_ENV, environment,
                          "SQLAllocHandle(DBC)");
        return -1;
    }
    if (!SQL_SUCCEEDED(blocking_driver_connect(dbc,
                                               (SQLCHAR *) connection_string,
                                               completed, sizeof completed,
                                               &completed_length))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc, "SQLDriverConnect");
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        return -1;
    }

    lock_tables();
    for (i = 0; i < ST_ODBC_MAX_CONNECTIONS; ++i) {
        if (!connections[i].in_use) {
            connections[i].in_use = 1;
            connections[i].handle = dbc;
            id = i;
            break;
        }
    }
    unlock_tables();

    if (id < 0) {
        set_error("too many open database connections");
        blocking_disconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        return -1;
    }
    return id;
}

int
ST_odbc_disconnect(int connection)
{
    SQLHDBC     dbc = SQL_NULL_HDBC;
    int         i;

    clear_error();
    /*
     *  Close this connection's statements first, and do it before the
     *  disconnect rather than leaving them to SQLDisconnect.  A driver
     *  handed a connection with live statements is entitled to refuse, and
     *  the ones that do not refuse leak the handles instead.
     */
    lock_tables();
    for (i = 0; i < ST_ODBC_MAX_STATEMENTS; ++i)
        if (statements[i].in_use && statements[i].connection == connection) {
            SQLHSTMT    handle = statements[i].handle;

            free_statement_slot(&statements[i]);
            unlock_tables();
            blocking_free_statement(handle);
            lock_tables();
        }
    if (connection >= 0 && connection < ST_ODBC_MAX_CONNECTIONS
     && connections[connection].in_use) {
        dbc = connections[connection].handle;
        connections[connection].in_use = 0;
        connections[connection].handle = SQL_NULL_HDBC;
    }
    unlock_tables();

    if (dbc == SQL_NULL_HDBC)
        return 0;                       /*  already closed; see above  */

    /*
     *  ROLL BACK FIRST, and this is not belt and braces.
     *
     *  Autocommit is off by design -- a connection opens inside a
     *  transaction -- so a connection that did anything at all, a SELECT
     *  included, has one open when it is closed.  ODBC does not say what
     *  disconnecting with an open transaction does, and drivers disagree:
     *  some roll back, some commit, and SQLite's leaves the rollback
     *  journal on disk and the database locked.  The symptom was a test
     *  suite that hung on its second connection with a stray
     *  `<database>-journal' beside the file.
     *
     *  Rolling back is also the only honest reading.  Work that was not
     *  committed by the time the connection closed was not committed, and
     *  a driver that decides to commit it is inventing an intention nobody
     *  expressed.
     */
    blocking_end_transaction(dbc, SQL_ROLLBACK);
    blocking_disconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    return 0;
}

int
ST_odbc_is_connected(int connection)
{
    int     open;

    lock_tables();
    open = connection_at(connection) != NULL;
    unlock_tables();
    return open;
}

static int
set_connect_attribute(int connection, SQLINTEGER attribute, SQLUINTEGER value,
                      const char *what)
{
    SQLHDBC     dbc = connection_handle(connection);

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    if (!SQL_SUCCEEDED(blocking_set_connect_attr(dbc, attribute,
                                       (SQLPOINTER) (uintptr_t) value, 0))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc, what);
        return -1;
    }
    return 0;
}

int
ST_odbc_set_autocommit(int connection, int on)
{
    return set_connect_attribute(connection, SQL_ATTR_AUTOCOMMIT,
                                 on ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF,
                                 "SQLSetConnectAttr(AUTOCOMMIT)");
}

int
ST_odbc_set_read_only(int connection, int on)
{
    return set_connect_attribute(connection, SQL_ATTR_ACCESS_MODE,
                                 on ? SQL_MODE_READ_ONLY : SQL_MODE_READ_WRITE,
                                 "SQLSetConnectAttr(ACCESS_MODE)");
}

static int
end_transaction(int connection, SQLSMALLINT how, const char *what)
{
    SQLHDBC     dbc = connection_handle(connection);

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    if (!SQL_SUCCEEDED(blocking_end_transaction(dbc, how))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc, what);
        return -1;
    }
    return 0;
}

int
ST_odbc_commit(int connection)
{
    return end_transaction(connection, SQL_COMMIT, "SQLEndTran(COMMIT)");
}

int
ST_odbc_rollback(int connection)
{
    return end_transaction(connection, SQL_ROLLBACK, "SQLEndTran(ROLLBACK)");
}

int
ST_odbc_info_string(int connection, int info, char *out, size_t max)
{
    SQLHDBC     dbc = connection_handle(connection);
    SQLSMALLINT length = 0;

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    if (max == 0)
        return -1;
    out[0] = '\0';
    if (!SQL_SUCCEEDED(blocking_get_info(dbc, (SQLUSMALLINT) info, out,
                                         (SQLSMALLINT) (max - 1), &length))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc, "SQLGetInfo");
        return -1;
    }
    if ((size_t) length >= max)
        length = (SQLSMALLINT) (max - 1);
    out[length] = '\0';
    return 0;
}

int
ST_odbc_set_schema(int connection, const char *schema)
{
    SQLHDBC     dbc = connection_handle(connection);

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    if (!SQL_SUCCEEDED(blocking_set_connect_attr(dbc, SQL_ATTR_CURRENT_CATALOG,
                                                 (SQLPOINTER) schema,
                                                 SQL_NTS))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc,
                          "SQLSetConnectAttr(CURRENT_CATALOG)");
        return -1;
    }
    return 0;
}

int
ST_odbc_get_schema(int connection, char *out, size_t max)
{
    SQLHDBC     dbc = connection_handle(connection);
    SQLINTEGER  length = 0;

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    if (max == 0)
        return -1;
    out[0] = '\0';
    if (!SQL_SUCCEEDED(SQLGetConnectAttr(dbc, SQL_ATTR_CURRENT_CATALOG, out,
                                         (SQLINTEGER) (max - 1), &length))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc,
                          "SQLGetConnectAttr(CURRENT_CATALOG)");
        return -1;
    }
    if (length < 0 || (size_t) length >= max)
        length = (SQLINTEGER) (max - 1);
    out[length] = '\0';
    return 0;
}

/*  ----------  Preparing and binding  ----------  */

int
ST_odbc_prepare(int connection, const char *sql)
{
    SQLHDBC     dbc = connection_handle(connection);
    SQLHSTMT    stmt = SQL_NULL_HSTMT;
    int         id;

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    if (!sql) {
        set_error("no SQL to prepare");
        return -1;
    }
    id = new_statement(connection, dbc, &stmt);
    if (id < 0)
        return -1;
    if (!SQL_SUCCEEDED(blocking_prepare(stmt, (SQLCHAR *) sql))) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLPrepare");
        ST_odbc_close_statement(id);
        return -1;
    }
    return id;
}

/*
 *  Room for one parameter's bytes, kept BY THE STATEMENT.
 *
 *  This is the detail that makes ODBC binding different from JDBC's
 *  setObject, and getting it wrong produces a bug that looks like anything
 *  but its cause.  SQLBindParameter records the ADDRESS of the buffer; the
 *  driver reads it later, at SQLExecute.  A value bound from a local
 *  variable is therefore read after that variable has gone, and what the
 *  database stores is whatever the stack happens to hold at execute time.
 *  So every bound value is copied into storage owned by the statement and
 *  kept until the statement is closed or its parameters are cleared.
 */
static st_odbc_param *
parameter_room(int statement, int index, size_t bytes)
{
    st_odbc_statement  *s;
    st_odbc_param      *p;
    unsigned            wanted;

    if (index < 1) {
        set_error("parameter numbers begin at one");
        return NULL;
    }
    lock_tables();
    s = statement_at(statement);
    if (!s) {
        unlock_tables();
        set_error("no such statement");
        return NULL;
    }
    wanted = (unsigned) index;
    if (wanted > s->param_count) {
        st_odbc_param **grown = realloc(s->params, wanted * sizeof *grown);

        if (!grown) {
            unlock_tables();
            set_error("out of memory binding a parameter");
            return NULL;
        }
        memset(grown + s->param_count, 0,
               (wanted - s->param_count) * sizeof *grown);
        s->params      = grown;
        s->param_count = wanted;
    }
    /*
     *  The POINTER array may have just moved; the parameters it points at
     *  did not, which is the whole point.  See st_odbc_param.
     */
    if (!s->params[index - 1]) {
        s->params[index - 1] = calloc(1, sizeof **s->params);
        if (!s->params[index - 1]) {
            unlock_tables();
            set_error("out of memory binding a parameter");
            return NULL;
        }
    }
    p = s->params[index - 1];
    if (p->capacity < bytes) {
        unsigned char  *data = realloc(p->data, bytes);

        if (!data) {
            unlock_tables();
            set_error("out of memory binding a parameter");
            return NULL;
        }
        p->data     = data;
        p->capacity = bytes;
    }
    unlock_tables();
    return p;
}

static int
bind_parameter(int statement, int index, SQLSMALLINT c_type,
               SQLSMALLINT sql_type, SQLULEN column_size,
               SQLSMALLINT decimal_digits, const void *bytes, size_t length,
               SQLLEN indicator)
{
    SQLHSTMT        stmt;
    st_odbc_param  *p;

    clear_error();
    stmt = statement_handle(statement);
    if (stmt == SQL_NULL_HSTMT)
        return -1;
    p = parameter_room(statement, index, length ? length : 1);
    if (!p)
        return -1;
    if (bytes && length)
        memcpy(p->data, bytes, length);
    p->indicator = indicator;
    p->bound     = 1;
    if (!SQL_SUCCEEDED(SQLBindParameter(stmt, (SQLUSMALLINT) index,
                                        SQL_PARAM_INPUT, c_type, sql_type,
                                        column_size, decimal_digits,
                                        p->data, (SQLLEN) p->capacity,
                                        &p->indicator))) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLBindParameter");
        return -1;
    }
    return 0;
}

int
ST_odbc_bind_null(int statement, int index, int sql_type)
{
    /*
     *  A null still needs a type, and a caller who does not know one says
     *  so by passing zero.  VARCHAR is the type every database will convert
     *  from, which is what makes it the right guess rather than merely a
     *  guess: `set x = ?' with a null VARCHAR sets a null integer column to
     *  null, and there is nothing to convert.
     */
    if (sql_type == 0)
        sql_type = SQL_VARCHAR;
    return bind_parameter(statement, index, SQL_C_CHAR,
                          (SQLSMALLINT) sql_type, 1, 0, NULL, 0,
                          SQL_NULL_DATA);
}

int
ST_odbc_bind_int(int statement, int index, int64_t value)
{
    return bind_parameter(statement, index, SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
                          &value, sizeof value, 0);
}

int
ST_odbc_bind_double(int statement, int index, double value)
{
    return bind_parameter(statement, index, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0,
                          &value, sizeof value, 0);
}

int
ST_odbc_bind_boolean(int statement, int index, int value)
{
    unsigned char   bit = value ? 1 : 0;

    return bind_parameter(statement, index, SQL_C_BIT, SQL_BIT, 0, 0,
                          &bit, sizeof bit, 0);
}

int
ST_odbc_bind_string(int statement, int index, const char *text, size_t length)
{
    /*
     *  Column size is the length and not the capacity.  A driver told the
     *  buffer's size instead pads to it on the fixed-width types, and a CHAR
     *  column comes back with trailing spaces nobody asked for.
     */
    return bind_parameter(statement, index, SQL_C_CHAR,
                          length > 4000 ? SQL_LONGVARCHAR : SQL_VARCHAR,
                          length ? length : 1, 0, text, length,
                          (SQLLEN) length);
}

int
ST_odbc_bind_bytes(int statement, int index, const void *bytes, size_t length)
{
    return bind_parameter(statement, index, SQL_C_BINARY,
                          length > 4000 ? SQL_LONGVARBINARY : SQL_VARBINARY,
                          length ? length : 1, 0, bytes, length,
                          (SQLLEN) length);
}

int
ST_odbc_bind_date(int statement, int index, int year, int month, int day)
{
    SQL_DATE_STRUCT     d;

    d.year  = (SQLSMALLINT) year;
    d.month = (SQLUSMALLINT) month;
    d.day   = (SQLUSMALLINT) day;
    return bind_parameter(statement, index, SQL_C_TYPE_DATE, SQL_TYPE_DATE,
                          10, 0, &d, sizeof d, sizeof d);
}

int
ST_odbc_bind_time(int statement, int index, int hour, int minute, int second,
                  uint32_t nanoseconds)
{
    SQL_TIME_STRUCT     t;

    (void) nanoseconds;             /*  SQL_TIME_STRUCT has no fraction  */
    t.hour   = (SQLUSMALLINT) hour;
    t.minute = (SQLUSMALLINT) minute;
    t.second = (SQLUSMALLINT) second;
    return bind_parameter(statement, index, SQL_C_TYPE_TIME, SQL_TYPE_TIME,
                          8, 0, &t, sizeof t, sizeof t);
}

int
ST_odbc_bind_timestamp(int statement, int index, int year, int month, int day,
                       int hour, int minute, int second, uint32_t nanoseconds)
{
    SQL_TIMESTAMP_STRUCT    ts;

    ts.year     = (SQLSMALLINT) year;
    ts.month    = (SQLUSMALLINT) month;
    ts.day      = (SQLUSMALLINT) day;
    ts.hour     = (SQLUSMALLINT) hour;
    ts.minute   = (SQLUSMALLINT) minute;
    ts.second   = (SQLUSMALLINT) second;
    ts.fraction = (SQLUINTEGER) nanoseconds;
    /*
     *  Nine decimal digits, because the struct's fraction is nanoseconds.
     *  Saying fewer truncates on the drivers that honour it, which is a data
     *  loss that shows up only on the rows where it matters.
     */
    return bind_parameter(statement, index, SQL_C_TYPE_TIMESTAMP,
                          SQL_TYPE_TIMESTAMP, 29, 9, &ts, sizeof ts,
                          sizeof ts);
}

int
ST_odbc_clear_parameters(int statement)
{
    st_odbc_statement  *s;
    SQLHSTMT            stmt;
    unsigned            i;

    clear_error();
    stmt = statement_handle(statement);
    if (stmt == SQL_NULL_HSTMT)
        return -1;
    if (!SQL_SUCCEEDED(SQLFreeStmt(stmt, SQL_RESET_PARAMS))) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLFreeStmt(RESET_PARAMS)");
        return -1;
    }
    /*
     *  Keep the buffers, forget that they are bound.  Re-binding the same
     *  parameter next time reuses the allocation, and a statement run over
     *  ten thousand rows does its allocating on the first one.
     */
    lock_tables();
    s = statement_at(statement);
    if (s)
        for (i = 0; i < s->param_count; ++i)
            if (s->params[i])
                s->params[i]->bound = 0;
    unlock_tables();
    return 0;
}

/*  ----------  Running and fetching  ----------  */

/*
 *  Forget what the last execution described.
 *
 *  A prepared statement run twice can answer result sets of different
 *  shapes -- not for a SELECT, but the catalogue calls and anything going
 *  through a stored procedure will -- so the cached description belongs to
 *  the execution and not to the statement.
 */
static void
forget_description(int statement)
{
    st_odbc_statement  *s;

    lock_tables();
    s = statement_at(statement);
    if (s) {
        free(s->columns);
        s->columns      = NULL;
        s->column_count = 0;
        s->described    = 0;
    }
    unlock_tables();
}

int
ST_odbc_execute(int statement)
{
    SQLHSTMT    stmt;
    SQLRETURN   r;

    clear_error();
    stmt = statement_handle(statement);
    if (stmt == SQL_NULL_HSTMT)
        return -1;
    forget_description(statement);
    r = blocking_execute(stmt);
    /*
     *  SQL_NO_DATA is what a searched UPDATE or DELETE that matched nothing
     *  answers.  It is not a failure -- nothing was wrong with the statement
     *  and nothing was wrong with the database -- and a caller learns the
     *  count from SQLRowCount, which will say zero.
     */
    if (r == SQL_NO_DATA)
        return 0;
    if (!SQL_SUCCEEDED(r)) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLExecute");
        return -1;
    }
    return 0;
}

int
ST_odbc_execute_direct(int connection, const char *sql, int64_t *rows_affected)
{
    SQLHDBC     dbc = connection_handle(connection);
    SQLHSTMT    stmt = SQL_NULL_HSTMT;
    SQLRETURN   r;
    SQLLEN      rows = 0;

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt))) {
        record_diagnostic(SQL_HANDLE_DBC, dbc, "SQLAllocHandle(STMT)");
        return -1;
    }
    r = blocking_execute_direct(stmt, (SQLCHAR *) sql);
    if (r != SQL_NO_DATA && !SQL_SUCCEEDED(r)) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLExecDirect");
        blocking_free_statement(stmt);
        return -1;
    }
    if (rows_affected) {
        if (!SQL_SUCCEEDED(SQLRowCount(stmt, &rows)))
            rows = -1;
        *rows_affected = (int64_t) rows;
    }
    blocking_free_statement(stmt);
    return 0;
}

int
ST_odbc_fetch(int statement)
{
    SQLHSTMT    stmt;
    SQLRETURN   r;

    clear_error();
    stmt = statement_handle(statement);
    if (stmt == SQL_NULL_HSTMT)
        return -1;
    r = blocking_fetch(stmt);
    if (r == SQL_NO_DATA)
        return 0;
    if (!SQL_SUCCEEDED(r)) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLFetch");
        return -1;
    }
    return 1;
}

int
ST_odbc_row_count(int statement, int64_t *out)
{
    SQLHSTMT    stmt;
    SQLLEN      rows = 0;

    clear_error();
    stmt = statement_handle(statement);
    if (stmt == SQL_NULL_HSTMT)
        return -1;
    if (!SQL_SUCCEEDED(SQLRowCount(stmt, &rows))) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLRowCount");
        return -1;
    }
    if (out)
        *out = (int64_t) rows;
    return 0;
}

/*
 *  Describe every column once, after an execute, and cache it.
 *
 *  Once rather than per column, because SQLDescribeCol is a driver-manager
 *  round trip on some drivers and a fetch loop asks for the type of every
 *  column of every row.  Reading a thousand rows of ten columns would be ten
 *  thousand describes for information that could not have changed.
 */
static int
describe_columns(int statement)
{
    SQLHSTMT            stmt;
    SQLSMALLINT         count = 0;
    st_odbc_column     *columns;
    st_odbc_statement  *s;
    SQLSMALLINT         i;
    int                 already;

    lock_tables();
    s = statement_at(statement);
    already = s && s->described;
    unlock_tables();
    if (already)
        return 0;

    stmt = statement_handle(statement);
    if (stmt == SQL_NULL_HSTMT)
        return -1;
    if (!SQL_SUCCEEDED(SQLNumResultCols(stmt, &count))) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLNumResultCols");
        return -1;
    }
    if (count < 0)
        count = 0;
    columns = count ? calloc((size_t) count, sizeof *columns) : NULL;
    if (count && !columns) {
        set_error("out of memory describing a result set");
        return -1;
    }
    for (i = 0; i < count; ++i) {
        SQLCHAR         name[128];
        SQLSMALLINT     name_length = 0;
        SQLSMALLINT     type = 0, digits = 0, nullable = 0;
        SQLULEN         size = 0;

        name[0] = '\0';
        if (!SQL_SUCCEEDED(blocking_describe_column(stmt,
                                          (SQLUSMALLINT) (i + 1),
                                          name, sizeof name, &name_length,
                                          &type, &size, &digits, &nullable))) {
            record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLDescribeCol");
            free(columns);
            return -1;
        }
        if (name_length < 0
         || (size_t) name_length >= sizeof columns[i].name)
            name_length = (SQLSMALLINT) (sizeof columns[i].name - 1);
        memcpy(columns[i].name, name, (size_t) name_length);
        columns[i].name[name_length] = '\0';
        columns[i].sql_type       = type;
        columns[i].size           = (SQLLEN) size;
        columns[i].decimal_digits = digits;
        columns[i].nullable       = nullable;
    }

    lock_tables();
    s = statement_at(statement);
    if (!s) {
        unlock_tables();
        free(columns);
        set_error("no such statement");
        return -1;
    }
    free(s->columns);
    s->columns      = columns;
    s->column_count = (unsigned) count;
    s->described    = 1;
    unlock_tables();
    return 0;
}

int
ST_odbc_column_count(int statement)
{
    st_odbc_statement  *s;
    int                 count;

    if (describe_columns(statement) != 0)
        return -1;
    lock_tables();
    s = statement_at(statement);
    count = s ? (int) s->column_count : -1;
    unlock_tables();
    return count;
}

int
ST_odbc_describe_column(int statement, int column, char *name, size_t name_max,
                        int *sql_type, int64_t *size, int *decimal_digits,
                        int *nullable)
{
    st_odbc_statement  *s;
    st_odbc_column      copy;
    int                 found = 0;

    if (describe_columns(statement) != 0)
        return -1;
    lock_tables();
    s = statement_at(statement);
    if (s && column >= 1 && (unsigned) column <= s->column_count) {
        copy  = s->columns[column - 1];
        found = 1;
    }
    unlock_tables();
    if (!found) {
        set_error("no such column");
        return -1;
    }
    if (name && name_max) {
        size_t  n = strlen(copy.name);

        if (n >= name_max)
            n = name_max - 1;
        memcpy(name, copy.name, n);
        name[n] = '\0';
    }
    if (sql_type)
        *sql_type = copy.sql_type;
    if (size)
        *size = (int64_t) copy.size;
    if (decimal_digits)
        *decimal_digits = copy.decimal_digits;
    if (nullable)
        *nullable = copy.nullable;
    return 0;
}

/*  ----------  Reading a value  ----------  */

/*
 *  The statement's value buffer, at least `bytes' long.
 *
 *  One buffer per statement rather than one per call: a fetch loop reads
 *  every column of every row through here, and a malloc per column per row
 *  is the allocation this design exists to avoid.  The buffer belongs to the
 *  statement, so two workers reading two statements never meet.
 */
static unsigned char *
value_room(int statement, size_t bytes)
{
    st_odbc_statement  *s;
    unsigned char      *buffer = NULL;

    lock_tables();
    s = statement_at(statement);
    if (s) {
        if (s->value_capacity < bytes) {
            unsigned char  *grown = realloc(s->value, bytes);

            if (grown) {
                s->value          = grown;
                s->value_capacity = bytes;
            }
        }
        if (s->value_capacity >= bytes)
            buffer = s->value;
    }
    unlock_tables();
    if (!buffer)
        set_error("out of memory reading a column");
    return buffer;
}

/*
 *  Read a variable-length column, however long it turns out to be.
 *
 *  SQLGetData answers SQL_SUCCESS_WITH_INFO with SQLSTATE 01004 when it
 *  truncated, and the indicator then holds the length of the WHOLE value --
 *  which is what makes one retry enough rather than a doubling loop.
 *
 *  The retry reads the REST, not the value again.  A second SQLGetData on
 *  the same column continues where the first stopped, so re-reading into the
 *  front of the buffer would answer the tail of the value with its head
 *  overwritten -- a bug that appears only on values longer than the initial
 *  buffer, which is to say only in production.
 *
 *  The terminator is not counted by the indicator for character data and
 *  must still be paid for out of the buffer.  That is the classic off-by-one
 *  here, and it shows up as every long value being one character short.
 */
static int
get_variable(int statement, SQLHSTMT stmt, int column, SQLSMALLINT c_type,
             const unsigned char **text, size_t *length)
{
    const size_t    terminator = (c_type == SQL_C_CHAR) ? 1 : 0;
    size_t          capacity = ST_ODBC_VALUE_INITIAL;
    size_t          have;
    size_t          total;
    unsigned char  *buffer;
    SQLLEN          indicator = 0;
    SQLRETURN       r;

    buffer = value_room(statement, capacity);
    if (!buffer)
        return -1;
    r = blocking_get_data(stmt, (SQLUSMALLINT) column, c_type, buffer,
                          (SQLLEN) capacity, &indicator);
    /*
     *  Nothing left of this column: a zero-length value, or one that ended
     *  exactly on the buffer and is being asked for again.  An empty result,
     *  not a failure.
     */
    if (r == SQL_NO_DATA) {
        *text   = buffer;
        *length = 0;
        return 0;
    }
    if (!SQL_SUCCEEDED(r)) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLGetData");
        return -1;
    }
    if (indicator == SQL_NULL_DATA) {
        *text   = NULL;
        *length = 0;
        return 0;
    }
    /*
     *  The driver will not say how long the value is.  What fit is what
     *  there is to have: growing blindly would loop for as long as a driver
     *  is willing to keep refusing to measure.
     */
    if (indicator == SQL_NO_TOTAL) {
        *text   = buffer;
        *length = capacity - terminator;
        return 0;
    }

    total = (size_t) indicator;
    if (total + terminator <= capacity) {           /*  it all fit  */
        *text   = buffer;
        *length = total;
        return 0;
    }

    have   = capacity - terminator;                 /*  what the first call got  */
    buffer = value_room(statement, total + terminator);
    if (!buffer)
        return -1;                                  /*  realloc kept the bytes  */

    r = blocking_get_data(stmt, (SQLUSMALLINT) column, c_type,
                          buffer + have,
                          (SQLLEN) (total - have + terminator),
                          &indicator);
    if (r != SQL_NO_DATA && !SQL_SUCCEEDED(r)) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLGetData(rest)");
        return -1;
    }
    *text   = buffer;
    *length = total;
    return 0;
}

int
ST_odbc_get(int statement, int column, st_odbc_value *out)
{
    SQLHSTMT        stmt;
    int             sql_type = 0;
    SQLLEN          indicator = 0;
    st_odbc_value   value;

    clear_error();
    memset(&value, 0, sizeof value);
    if (!out)
        return -1;
    if (ST_odbc_describe_column(statement, column, NULL, 0, &sql_type,
                                NULL, NULL, NULL) != 0)
        return -1;
    stmt = statement_handle(statement);
    if (stmt == SQL_NULL_HSTMT)
        return -1;

    switch (sql_type) {
    case SQL_BIT:
    case SQL_TINYINT:
    case SQL_SMALLINT:
    case SQL_INTEGER:
    case SQL_BIGINT: {
        SQLBIGINT   n = 0;

        if (!SQL_SUCCEEDED(blocking_get_data(stmt, (SQLUSMALLINT) column,
                                             SQL_C_SBIGINT, &n, sizeof n,
                                             &indicator))) {
            record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLGetData(integer)");
            return -1;
        }
        if (indicator == SQL_NULL_DATA)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind = sql_type == SQL_BIT ? ST_ODBC_BOOLEAN : ST_ODBC_INT;
            value.i    = (int64_t) n;
        }
        break;
    }

    case SQL_REAL:
    case SQL_FLOAT:
    case SQL_DOUBLE: {
        double  d = 0.0;

        if (!SQL_SUCCEEDED(blocking_get_data(stmt, (SQLUSMALLINT) column,
                                             SQL_C_DOUBLE, &d, sizeof d,
                                             &indicator))) {
            record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLGetData(float)");
            return -1;
        }
        if (indicator == SQL_NULL_DATA)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind = ST_ODBC_DOUBLE;
            value.d    = d;
        }
        break;
    }

    case SQL_DECIMAL:
    case SQL_NUMERIC: {
        /*  As characters.  See the note on ST_ODBC_DECIMAL in the header.  */
        const unsigned char    *text = NULL;
        size_t                  length = 0;

        if (get_variable(statement, stmt, column, SQL_C_CHAR, &text,
                         &length) != 0)
            return -1;
        if (!text)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind   = ST_ODBC_DECIMAL;
            value.text   = (const char *) text;
            value.length = length;
        }
        break;
    }

    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY: {
        const unsigned char    *bytes = NULL;
        size_t                  length = 0;

        if (get_variable(statement, stmt, column, SQL_C_BINARY, &bytes,
                         &length) != 0)
            return -1;
        if (!bytes)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind   = ST_ODBC_BYTES;
            value.text   = (const char *) bytes;
            value.length = length;
        }
        break;
    }

    case SQL_TYPE_DATE:
    case SQL_DATE: {
        SQL_DATE_STRUCT     d;

        memset(&d, 0, sizeof d);
        if (!SQL_SUCCEEDED(blocking_get_data(stmt, (SQLUSMALLINT) column,
                                             SQL_C_TYPE_DATE, &d, sizeof d,
                                             &indicator))) {
            record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLGetData(date)");
            return -1;
        }
        if (indicator == SQL_NULL_DATA)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind  = ST_ODBC_DATE;
            value.year  = d.year;
            value.month = d.month;
            value.day   = d.day;
        }
        break;
    }

    case SQL_TYPE_TIME:
    case SQL_TIME: {
        SQL_TIME_STRUCT     t;

        memset(&t, 0, sizeof t);
        if (!SQL_SUCCEEDED(blocking_get_data(stmt, (SQLUSMALLINT) column,
                                             SQL_C_TYPE_TIME, &t, sizeof t,
                                             &indicator))) {
            record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLGetData(time)");
            return -1;
        }
        if (indicator == SQL_NULL_DATA)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind   = ST_ODBC_TIME;
            value.hour   = t.hour;
            value.minute = t.minute;
            value.second = t.second;
        }
        break;
    }

    case SQL_TYPE_TIMESTAMP:
    case SQL_TIMESTAMP: {
        SQL_TIMESTAMP_STRUCT    ts;

        memset(&ts, 0, sizeof ts);
        if (!SQL_SUCCEEDED(blocking_get_data(stmt, (SQLUSMALLINT) column,
                                             SQL_C_TYPE_TIMESTAMP, &ts,
                                             sizeof ts, &indicator))) {
            record_diagnostic(SQL_HANDLE_STMT, stmt, "SQLGetData(timestamp)");
            return -1;
        }
        if (indicator == SQL_NULL_DATA)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind       = ST_ODBC_TIMESTAMP;
            value.year       = ts.year;
            value.month      = ts.month;
            value.day        = ts.day;
            value.hour       = ts.hour;
            value.minute     = ts.minute;
            value.second     = ts.second;
            value.nanosecond = (uint32_t) ts.fraction;
        }
        break;
    }

    default: {
        /*
         *  Everything else as characters: CHAR, VARCHAR, the national and
         *  long variants, and whatever a driver invented that this switch
         *  has never heard of.  Asking for SQL_C_CHAR is the one request
         *  every driver can satisfy for every type, so the unknown case
         *  degrades to text rather than to a failure.
         */
        const unsigned char    *text = NULL;
        size_t                  length = 0;

        if (get_variable(statement, stmt, column, SQL_C_CHAR, &text,
                         &length) != 0)
            return -1;
        if (!text)
            value.kind = ST_ODBC_NULL;
        else {
            value.kind   = ST_ODBC_STRING;
            value.text   = (const char *) text;
            value.length = length;
        }
        break;
    }
    }

    *out = value;
    return 0;
}

/*  ----------  Catalogue  ----------  */

/*
 *  NULL and the empty string are different questions.
 *
 *  ODBC reads a null pattern as "any" and an empty one as "those whose name
 *  is empty", which nothing has.  A caller who does not know the schema
 *  passes nothing, and that has to arrive here as NULL or the answer is
 *  reliably no rows -- which reads like a database with no tables in it.
 */
static SQLCHAR *
pattern(const char *text)
{
    if (!text || !*text)
        return NULL;
    return (SQLCHAR *) text;
}

static int
catalogue(int connection, int which, const char *schema, const char *a,
          const char *b)
{
    SQLHDBC     dbc = connection_handle(connection);
    SQLHSTMT    stmt = SQL_NULL_HSTMT;
    SQLRETURN   r = SQL_ERROR;
    int         id;

    if (dbc == SQL_NULL_HDBC)
        return -1;
    clear_error();
    id = new_statement(connection, dbc, &stmt);
    if (id < 0)
        return -1;

    WORKER_enter_native();
    switch (which) {
    case 0:
        r = SQLTables(stmt, NULL, 0, pattern(schema), SQL_NTS,
                      pattern(a), SQL_NTS, pattern(b), SQL_NTS);
        break;
    case 1:
        r = SQLColumns(stmt, NULL, 0, pattern(schema), SQL_NTS,
                       pattern(a), SQL_NTS, pattern(b), SQL_NTS);
        break;
    case 2:
        r = SQLPrimaryKeys(stmt, NULL, 0, pattern(schema), SQL_NTS,
                           pattern(a), SQL_NTS);
        break;
    case 3:
        /*
         *  Naming the table as the FOREIGN-KEY end asks which keys point
         *  out of it, which is getImportedKeys.  Naming it as the primary
         *  end would ask the opposite question and answer a graph pointing
         *  the wrong way, which SchemaGraph would then walk backwards.
         */
        r = SQLForeignKeys(stmt, NULL, 0, NULL, 0, NULL, 0,
                           NULL, 0, pattern(schema), SQL_NTS,
                           pattern(a), SQL_NTS);
        break;
    default:
        break;
    }
    WORKER_leave_native();

    if (!SQL_SUCCEEDED(r)) {
        record_diagnostic(SQL_HANDLE_STMT, stmt, "a catalogue query");
        ST_odbc_close_statement(id);
        return -1;
    }
    return id;
}

int
ST_odbc_tables(int connection, const char *schema, const char *table,
               const char *types)
{
    return catalogue(connection, 0, schema, table, types);
}

int
ST_odbc_columns(int connection, const char *schema, const char *table,
                const char *column)
{
    return catalogue(connection, 1, schema, table, column);
}

int
ST_odbc_primary_keys(int connection, const char *schema, const char *table)
{
    return catalogue(connection, 2, schema, table, NULL);
}

int
ST_odbc_imported_keys(int connection, const char *schema, const char *table)
{
    return catalogue(connection, 3, schema, table, NULL);
}

#else   /*  no ODBC in this build  */

/*
 *  A build without ODBC.
 *
 *  Every entry point is here and every one fails with the same sentence,
 *  rather than the file being absent and its callers not compiling.  The
 *  reason is the one HEADLESS=1 has for the display: a system that builds
 *  everywhere and tells you what it cannot do is worth more than one that
 *  refuses to build, and a Smalltalk that answers "this build has no ODBC"
 *  when asked to connect is diagnosing itself.
 */

static const char   absent[] =
    "this build has no ODBC -- rebuild with unixODBC's development package "
    "installed, or see `make deps'";

int         ST_odbc_available(void)                     { return 0; }
const char *ST_odbc_last_error(void)                    { return absent; }

int  ST_odbc_connect(const char *s)                     { (void) s; return -1; }
int  ST_odbc_disconnect(int c)                          { (void) c; return -1; }
int  ST_odbc_is_connected(int c)                        { (void) c; return 0; }
int  ST_odbc_set_autocommit(int c, int o)          { (void) c; (void) o; return -1; }
int  ST_odbc_set_read_only(int c, int o)           { (void) c; (void) o; return -1; }
int  ST_odbc_commit(int c)                              { (void) c; return -1; }
int  ST_odbc_rollback(int c)                            { (void) c; return -1; }

int
ST_odbc_info_string(int c, int i, char *out, size_t max)
{
    (void) c; (void) i;
    if (out && max)
        out[0] = '\0';
    return -1;
}

int  ST_odbc_set_schema(int c, const char *s)      { (void) c; (void) s; return -1; }

int
ST_odbc_get_schema(int c, char *out, size_t max)
{
    (void) c;
    if (out && max)
        out[0] = '\0';
    return -1;
}

int  ST_odbc_prepare(int c, const char *s)         { (void) c; (void) s; return -1; }
int  ST_odbc_close_statement(int s)                     { (void) s; return -1; }
int  ST_odbc_clear_parameters(int s)                    { (void) s; return -1; }

int  ST_odbc_bind_null(int s, int i, int t)   { (void) s; (void) i; (void) t; return -1; }
int  ST_odbc_bind_int(int s, int i, int64_t v){ (void) s; (void) i; (void) v; return -1; }
int  ST_odbc_bind_double(int s, int i, double v){ (void) s; (void) i; (void) v; return -1; }
int  ST_odbc_bind_boolean(int s, int i, int v){ (void) s; (void) i; (void) v; return -1; }

int
ST_odbc_bind_string(int s, int i, const char *t, size_t n)
{
    (void) s; (void) i; (void) t; (void) n;
    return -1;
}

int
ST_odbc_bind_bytes(int s, int i, const void *b, size_t n)
{
    (void) s; (void) i; (void) b; (void) n;
    return -1;
}

int
ST_odbc_bind_date(int s, int i, int y, int m, int d)
{
    (void) s; (void) i; (void) y; (void) m; (void) d;
    return -1;
}

int
ST_odbc_bind_time(int s, int i, int h, int m, int sec, uint32_t ns)
{
    (void) s; (void) i; (void) h; (void) m; (void) sec; (void) ns;
    return -1;
}

int
ST_odbc_bind_timestamp(int s, int i, int y, int mo, int d,
                       int h, int mi, int sec, uint32_t ns)
{
    (void) s; (void) i; (void) y; (void) mo; (void) d;
    (void) h; (void) mi; (void) sec; (void) ns;
    return -1;
}

int  ST_odbc_execute(int s)                             { (void) s; return -1; }

int
ST_odbc_execute_direct(int c, const char *sql, int64_t *rows)
{
    (void) c; (void) sql; (void) rows;
    return -1;
}

int  ST_odbc_fetch(int s)                               { (void) s; return -1; }
int  ST_odbc_row_count(int s, int64_t *o)          { (void) s; (void) o; return -1; }
int  ST_odbc_column_count(int s)                        { (void) s; return -1; }

int
ST_odbc_describe_column(int s, int c, char *name, size_t max, int *type,
                        int64_t *size, int *digits, int *nullable)
{
    (void) s; (void) c; (void) type; (void) size;
    (void) digits; (void) nullable;
    if (name && max)
        name[0] = '\0';
    return -1;
}

int  ST_odbc_get(int s, int c, st_odbc_value *o)  { (void) s; (void) c; (void) o; return -1; }

int
ST_odbc_tables(int c, const char *s, const char *t, const char *y)
{
    (void) c; (void) s; (void) t; (void) y;
    return -1;
}

int
ST_odbc_columns(int c, const char *s, const char *t, const char *col)
{
    (void) c; (void) s; (void) t; (void) col;
    return -1;
}

int
ST_odbc_primary_keys(int c, const char *s, const char *t)
{
    (void) c; (void) s; (void) t;
    return -1;
}

int
ST_odbc_imported_keys(int c, const char *s, const char *t)
{
    (void) c; (void) s; (void) t;
    return -1;
}

#endif  /*  ST_HAVE_ODBC  */

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  A long value from a driver that will not say how long it is.
 *
 *  SQL Server and FreeTDS answer SQL_NO_TOTAL from SQLGetData for a
 *  varchar(max), text or image column: the part that fit, and no length
 *  for the whole.  st_odbc.c answered that first part -- 4,095 characters
 *  -- as the whole value, and nothing said the rest was gone (Bugs5
 *  FILES-11).  It now reads part after part until one fits.
 *
 *  No such server is to hand, so this makes the driver it has behave like
 *  one.  The test defines SQLGetData itself; the copy of st_odbc.c linked
 *  into this program calls it instead of the driver manager's, and it
 *  calls the driver manager's through RTLD_NEXT and, while `unmeasured' is
 *  set, replaces each truncated part's length with SQL_NO_TOTAL -- which is
 *  exactly what the real ones answer.  The last part, which fits, keeps its
 *  length, as theirs does.
 *
 *  It SKIPS without ODBC, without the SQLite driver, and off Linux and
 *  glibc, where interposing on a shared library's symbol from the program
 *  is not something to rely on.
 */

#include "st_test.h"

#if defined(ST_OM_MT) && defined(ST_HAVE_ODBC) && defined(__linux__) \
 && defined(__GLIBC__)

#include "st_odbc.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sql.h>
#include <sqlext.h>

/*
 *  The database is this run's own, "<pid>-odbc-no-total-test.db" in the
 *  build's test directory, and removed at the end.  It was one fixed name
 *  there, which two runs of this test from one tree shared (Bugs5 DOCS-6).
 */
#define DATABASE_FILE   "odbc-no-total-test.db"
#define LONG_TEXT       100000
#define LONG_BYTES      30000

static int  unmeasured;
static int  parts;

SQLRETURN SQL_API
SQLGetData(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT c_type,
           SQLPOINTER buffer, SQLLEN capacity, SQLLEN *indicator)
{
    static SQLRETURN (SQL_API *real)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT,
                                     SQLPOINTER, SQLLEN, SQLLEN *);
    SQLRETURN   r;

    if (!real) {
        *(void **) &real = dlsym(RTLD_NEXT, "SQLGetData");
        if (!real)
            return SQL_ERROR;
    }
    r = real(stmt, column, c_type, buffer, capacity, indicator);
    if (unmeasured && r == SQL_SUCCESS_WITH_INFO && indicator
     && *indicator >= 0) {
        *indicator = SQL_NO_TOTAL;
        ++parts;
    }
    return r;
}

static void
fill(char *text, size_t n)
{
    size_t  i;

    for (i = 0; i < n; ++i)
        text[i] = (char) ('a' + (i * 7 + i / 26) % 26);
}

int
main(void)
{
    char            connection_string[512];
    const char     *database_file;
    int             connection;
    int             statement;
    char           *text;
    unsigned char  *bytes;
    st_odbc_value   value;
    size_t          i;

    ST_TEST_BEGIN("a long value the driver will not measure");

    database_file = st_test_path(DATABASE_FILE);
    snprintf(connection_string, sizeof connection_string,
             "DRIVER=SQLITE3;Database=%s;", database_file);
    if (!ST_odbc_available()
     || (connection = ST_odbc_connect(connection_string)) < 0) {
        printf("skipped: no ODBC driver manager, or no SQLITE3 driver\n");
        remove(database_file);
        return 0;
    }
    text  = (char *) malloc(LONG_TEXT);
    bytes = (unsigned char *) malloc(LONG_BYTES);
    if (!text || !bytes) {
        CHECK(0);
        return ST_TEST_END();
    }
    fill(text, LONG_TEXT);
    for (i = 0; i < LONG_BYTES; ++i)
        bytes[i] = (unsigned char) (i * 31 + 7);

    CHECK_EQ_INT(ST_odbc_execute_direct(connection, "DROP TABLE IF EXISTS t",
                                        NULL), 0);
    CHECK_EQ_INT(ST_odbc_execute_direct(connection,
                     "CREATE TABLE t (s TEXT, b BLOB)", NULL), 0);
    statement = ST_odbc_prepare(connection, "INSERT INTO t VALUES (?, ?)");
    CHECK(statement >= 0);
    CHECK_EQ_INT(ST_odbc_bind_string(statement, 1, text, LONG_TEXT), 0);
    CHECK_EQ_INT(ST_odbc_bind_bytes(statement, 2, bytes, LONG_BYTES), 0);
    CHECK_EQ_INT(ST_odbc_execute(statement), 0);
    ST_odbc_close_statement(statement);

    statement = ST_odbc_prepare(connection, "SELECT s, b FROM t");
    CHECK(statement >= 0);
    CHECK_EQ_INT(ST_odbc_execute(statement), 0);
    CHECK_EQ_INT(ST_odbc_fetch(statement), 1);

    unmeasured = 1;
    parts = 0;
    memset(&value, 0, sizeof value);
    CHECK_EQ_INT(ST_odbc_get(statement, 1, &value), 0);
    CHECK(parts > 0);           /*  the simulation was reached at all  */
    CHECK_EQ_INT(value.kind, ST_ODBC_STRING);
    CHECK_EQ_INT(value.length, LONG_TEXT);
    CHECK(value.text && value.length == LONG_TEXT
          && memcmp(value.text, text, LONG_TEXT) == 0);

    parts = 0;
    memset(&value, 0, sizeof value);
    CHECK_EQ_INT(ST_odbc_get(statement, 2, &value), 0);
    CHECK(parts > 0);
    CHECK_EQ_INT(value.kind, ST_ODBC_BYTES);
    CHECK_EQ_INT(value.length, LONG_BYTES);
    CHECK(value.text && value.length == LONG_BYTES
          && memcmp(value.text, bytes, LONG_BYTES) == 0);
    unmeasured = 0;

    ST_odbc_close_statement(statement);
    ST_odbc_disconnect(connection);
    remove(database_file);
    free(text);
    free(bytes);
    return ST_TEST_END();
}

#else

int
main(void)
{
    ST_TEST_BEGIN("a long value the driver will not measure");
    printf("skipped: needs ODBC, the 64-bit memory, and Linux with glibc\n");
    return ST_TEST_END();
}

#endif

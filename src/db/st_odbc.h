/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  ODBC, as much of it as a Smalltalk needs and no more.
 *
 *  This file knows nothing about object memory, OOPs or primitives.  It
 *  answers C types and small integer handles, and prim.c does the
 *  marshalling.  That split is the same one st_port.c has with the file
 *  primitives -- st_file_open answers a descriptor, primitive 130 turns it
 *  into a Smalltalk answer -- and it exists so that this file can be read,
 *  and tested, without an image.
 *
 *  WHY ODBC and not a driver per database.  Kiss's database package reaches
 *  JDBC in about thirty places, and every one of them has a 1:1 ODBC
 *  equivalent -- including the metadata calls, which is the part that
 *  usually decides such a port is impossible.  SQLColumns, SQLPrimaryKeys
 *  and SQLForeignKeys are DatabaseMetaData's getColumns, getPrimaryKeys and
 *  getImportedKeys.  And java.sql.Types is not merely similar to ODBC's SQL
 *  type codes, it IS them: JDBC took the numbering (CHAR 1, INTEGER 4,
 *  VARCHAR 12, DATE 91, TIMESTAMP 93) from ODBC, so the type switches in the
 *  ported code carry their constants over unchanged.
 *
 *  WHY HANDLES ARE INTEGERS and not addresses.  A SQLHDBC is a pointer,
 *  and this system writes its memory to a file and reads it back in
 *  another process.  An image holding a pointer from a previous life
 *  would find it plausible and dereference it.  So connections and
 *  statements live in tables here, a handle names a slot in one, every
 *  handle is checked against the table before use, and an image resumed
 *  from a snapshot finds every slot empty -- which is the truth, and the
 *  same reason primitive 130 keeps fd_is_ours rather than trusting the
 *  number in the object.
 *
 *  AND WHY A HANDLE IS MORE THAN A SLOT NUMBER (Bugs6 FILES-1).  Empty is
 *  the truth only until the first connect of the new life fills slot 0,
 *  and a DbConnection that came back in the image holding handle 0 -- a
 *  workspace variable, a global, a RestConnectionPool -- then names that
 *  connection, which may be another worker's and mid-transaction: its
 *  fetchAll: read the other worker's rows, its isOpen answered true, and
 *  its close closed the other worker's connection out from under it.  The
 *  same happened inside one life, to a DbCommand kept after its
 *  connection was closed and the slot handed to the next connect.
 *
 *  So a handle carries a serial as well as a slot: the low bits say which
 *  slot, the rest say which CLAIM of that slot, and the table remembers
 *  the serial of the claim it is holding.  A handle whose serial is not
 *  the slot's current one is nobody's, and every call on it answers "no
 *  such database connection" (or statement) for as long as the object
 *  holding it lives.  The serial counts up from the time the process
 *  started, in a unit no process can claim faster than, so a later life
 *  never repeats an earlier one's numbers; see claim_serial in st_odbc.c.
 *  Sixty-four bits because the serial needs them; the image sees the
 *  handle as an Integer and never looks inside it.
 *
 *  THE BLOCKING PROBLEM, and it is the whole design.  A worker inside
 *  SQLExecute is not running bytecodes, so it never reaches WORKER_poll, so
 *  a collector asking for a safepoint waits for the database.  One slow
 *  query would stop every core for as long as it took.  Every call below
 *  that can block therefore brackets itself with WORKER_enter_native and
 *  WORKER_leave_native, which park the worker for the duration: it counts
 *  as stopped, the collector proceeds, and the worker rejoins on return.
 *  This is why no function here may hold a raw pointer into the object
 *  memory across a call -- prim.c copies arguments out before entering and
 *  copies results back after leaving, and nothing in between touches an OOP.
 */

#ifndef ST_ODBC_H
#define ST_ODBC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Whether this build has a database at all.  A build without ODBC still
 *  compiles every line of this file's callers; the calls answer failure and
 *  ST_odbc_last_error says why, which is a far better experience than a
 *  doesNotUnderstand on a class that was compiled out.
 */
int         ST_odbc_available(void);

/*
 *  A connection or statement handle: a slot in the table and the serial of
 *  the claim that filled it, or -1 where a call answers failure.  Never
 *  decoded outside st_odbc.c.
 */
typedef int64_t st_odbc_handle;

/*
 *  The most recent failure, as text, for the calling thread.
 *
 *  Per thread and not per handle, because the call that fails most often is
 *  the one that was going to produce the handle.  Never NULL; answers an
 *  empty string when nothing has failed.
 */
const char *ST_odbc_last_error(void);

/*  ----------  Connections  ----------  */

/*
 *  Connect, and answer a connection handle or -1.
 *
 *  The string is an ODBC connection string -- "DSN=x;UID=y;PWD=z" or the
 *  driver-specific form -- and is passed to SQLDriverConnect rather than
 *  SQLConnect so that a caller who knows their driver can say so without a
 *  DSN having to exist in a file first.
 */
st_odbc_handle ST_odbc_connect(const char *connection_string);
int         ST_odbc_disconnect(st_odbc_handle connection);
int         ST_odbc_is_connected(st_odbc_handle connection);
/*
 *  Whether the server is still there, as the driver sees it: false for a
 *  connection the server has dropped, true for a live one and for one
 *  whose driver cannot say.  is_connected asks only whether the handle is
 *  one of this process's.
 */
int         ST_odbc_is_alive(st_odbc_handle connection);

int         ST_odbc_set_autocommit(st_odbc_handle connection, int on);
int         ST_odbc_set_read_only(st_odbc_handle connection, int on);
int         ST_odbc_commit(st_odbc_handle connection);
int         ST_odbc_rollback(st_odbc_handle connection);

/*
 *  SQLGetInfo for the string-valued types this port asks for.  info is an
 *  ODBC SQL_* info constant; the two that matter are SQL_DBMS_NAME (17),
 *  which is getDatabaseProductName, and SQL_DBMS_VER (18).
 */
int         ST_odbc_info_string(st_odbc_handle connection, int info, char *out,
                                size_t max);

/*  Schema, as SQLSetConnectAttr(SQL_ATTR_CURRENT_CATALOG) understands it.  */
int         ST_odbc_set_schema(st_odbc_handle connection, const char *schema);
int         ST_odbc_get_schema(st_odbc_handle connection, char *out, size_t max);

/*  ----------  Statements  ----------  */

st_odbc_handle ST_odbc_prepare(st_odbc_handle connection, const char *sql);
int         ST_odbc_close_statement(st_odbc_handle statement);

/*
 *  Forget the bound parameters, keeping the prepared plan.
 *
 *  Kiss reuses a prepared statement across rows and calls clearParameters
 *  between them; without it a row that binds fewer parameters than the last
 *  one inherits the leftovers, silently.
 */
int         ST_odbc_clear_parameters(st_odbc_handle statement);

int         ST_odbc_bind_null(st_odbc_handle statement, int index, int sql_type);
int         ST_odbc_bind_int(st_odbc_handle statement, int index, int64_t value);
int         ST_odbc_bind_double(st_odbc_handle statement, int index, double value);
int         ST_odbc_bind_string(st_odbc_handle statement, int index, const char *text,
                                size_t length);
int         ST_odbc_bind_bytes(st_odbc_handle statement, int index, const void *bytes,
                               size_t length);
int         ST_odbc_bind_boolean(st_odbc_handle statement, int index, int value);
int         ST_odbc_bind_date(st_odbc_handle statement, int index,
                              int year, int month, int day);
int         ST_odbc_bind_time(st_odbc_handle statement, int index,
                              int hour, int minute, int second,
                              uint32_t nanoseconds);
int         ST_odbc_bind_timestamp(st_odbc_handle statement, int index,
                                   int year, int month, int day,
                                   int hour, int minute, int second,
                                   uint32_t nanoseconds);

/*  Run a prepared statement.  Answers 0, or -1 with last_error set.  */
int         ST_odbc_execute(st_odbc_handle statement);

/*
 *  Run one statement that was never prepared, on a new statement handle
 *  which is closed before this answers.  This is Connection>>executeImmediate
 *  and the DDL path; a prepare would be a second round trip for nothing.
 */
int         ST_odbc_execute_direct(st_odbc_handle connection, const char *sql,
                                   int64_t *rows_affected);

/*  1 a row was read, 0 the end of the result set, -1 failure.  */
int         ST_odbc_fetch(st_odbc_handle statement);

int         ST_odbc_row_count(st_odbc_handle statement, int64_t *out);
int         ST_odbc_column_count(st_odbc_handle statement);

/*
 *  Describe one column, one-relative as ODBC and JDBC both number them.
 *  sql_type is the ODBC/java.sql.Types code.
 */
int         ST_odbc_describe_column(st_odbc_handle statement, int column,
                                    char *name, size_t name_max,
                                    int *sql_type, int64_t *size,
                                    int *decimal_digits, int *nullable);

/*  ----------  Reading a value  ----------  */

typedef enum {
    ST_ODBC_NULL = 0,
    ST_ODBC_INT,                /*  i                                     */
    ST_ODBC_DOUBLE,             /*  d                                     */
    ST_ODBC_BOOLEAN,            /*  i, 0 or 1                             */
    ST_ODBC_STRING,             /*  text, length                          */
    ST_ODBC_DECIMAL,            /*  text, length -- digits, never a Float  */
    ST_ODBC_BYTES,              /*  text, length                          */
    ST_ODBC_DATE,               /*  year month day                        */
    ST_ODBC_TIME,               /*  hour minute second nanosecond         */
    ST_ODBC_TIMESTAMP           /*  all seven                             */
} st_odbc_kind;

/*
 *  DECIMAL and NUMERIC come back as DIGITS, deliberately.
 *
 *  A money column read through a double is wrong, quietly, and only in the
 *  cases anyone cares about.  Smalltalk has exact arithmetic -- Fraction and
 *  ScaledDecimal -- so the one place the exactness could be lost is this
 *  boundary, and the boundary is where it is easiest not to lose it: ask
 *  ODBC for the characters the database printed and let Smalltalk read them.
 *  This is the one respect in which the port is better than the Java it
 *  comes from, which is stuck with BigDecimal's own parse of the same text.
 */
typedef struct {
    st_odbc_kind    kind;
    int64_t         i;
    double          d;
    const char     *text;       /*  into the statement's buffer  */
    size_t          length;
    int             year, month, day;
    int             hour, minute, second;
    uint32_t        nanosecond;
} st_odbc_value;

/*
 *  Read column `column' of the current row.
 *
 *  `text' points into a buffer owned by the statement and is valid until the
 *  next call on that statement.  The caller copies it out at once; prim.c
 *  does exactly that, into a Smalltalk String, before it does anything else.
 */
int         ST_odbc_get(st_odbc_handle statement, int column, st_odbc_value *out);

/*  ----------  Catalogue  ----------  */

/*
 *  Each of these answers a STATEMENT HANDLE whose rows are read with the
 *  ordinary fetch and get above, which is exactly the shape JDBC gives
 *  DatabaseMetaData -- getColumns answers a ResultSet -- so the ported code
 *  keeps its structure.  NULL for a pattern means "any".
 */
st_odbc_handle ST_odbc_tables(st_odbc_handle connection, const char *schema,
                           const char *table, const char *types);
st_odbc_handle ST_odbc_columns(st_odbc_handle connection, const char *schema,
                            const char *table, const char *column);
st_odbc_handle ST_odbc_primary_keys(st_odbc_handle connection, const char *schema,
                                 const char *table);
/*
 *  The foreign keys that point OUT of `table', which is getImportedKeys and
 *  is what SchemaGraph walks.  SQLForeignKeys takes both ends and decides
 *  which question is being asked by which end is named; naming the table as
 *  the foreign-key side asks this one.
 */
st_odbc_handle ST_odbc_imported_keys(st_odbc_handle connection, const char *schema,
                                  const char *table);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_ODBC_H  */

# The Database

SQL from Smalltalk, through ODBC, on every core at once.

```smalltalk
DbConnection open: 'DRIVER=SQLITE3;Database=payroll.db;' do:
    [:db |
     (db newQueryBuilder: 'employee')
        select: 'employee.last_name';
        select: 'department.name';
        where: 'project.project_id = ?' with: (Array with: 17);
        orderBy: 'employee.last_name';
        fetchAll]
```

Nothing there says how `employee` reaches `department`, or how either reaches
`project`. The join graph does: the builder reads the tables out of the columns
and conditions, asks the schema for the foreign keys that connect them, and
writes the joins. Adding a column from a fourth table adds a join without
anybody editing a `FROM` clause — which is the point, because a hand-written
join that is one table out of date is a query that still runs.

## Why ODBC

The design this is ported from — the `org.kissweb.database` package — reaches
JDBC in about thirty places, and every one has a 1:1 ODBC equivalent. Including
the metadata calls, which is the part that usually decides such a port is
impossible: `SQLColumns`, `SQLPrimaryKeys` and `SQLForeignKeys` are
`DatabaseMetaData`'s `getColumns`, `getPrimaryKeys` and `getImportedKeys`.

And `java.sql.Types` is not merely similar to ODBC's type codes, it **is** them.
JDBC took the numbering from ODBC and never renumbered it: CHAR 1, INTEGER 4,
VARCHAR 12, DATE 91, TIMESTAMP 93. So the type switches came across with their
constants unchanged, and reading either code against the other needs no
translation table.

One API reaches PostgreSQL, MySQL, SQLite, Oracle and SQL Server. The
alternative — a client library per database — is five dependencies, five build
configurations, and five sets of metadata calls that are all spelled
differently.

**Two things ODBC does not have**, both handled where the difference belongs:

- **No `getGeneratedKeys`.** `DbRecord>>insertReturningKey:` asks the connection
  what kind of database it is and uses PostgreSQL's `RETURNING`, SQLite's
  `last_insert_rowid()`, MySQL's `LAST_INSERT_ID()` or SQL Server's
  `SCOPE_IDENTITY()`.
- **No `createArrayOf`.** It was never portable JDBC either — the Java calls it
  with PostgreSQL type names. `whereIn:` binds one parameter per element, which
  works everywhere.

## It does not stop the world

A worker inside `SQLExecute` is not running bytecodes, so it never reaches
`WORKER_poll`, so a collector asking for a safepoint would wait for the
database. One slow query would stop every core for as long as the query took.

Every ODBC call that can block therefore brackets itself with
`WORKER_enter_native` and `WORKER_leave_native` (`src/sched/worker.h`). They
park the worker exactly as the poll would — registers written back, counted
among the parked — so the collector runs *while* the worker is inside the
driver. That is correct precisely because the worker has promised not to touch
the object memory in there, and the promise is kept by construction: every
argument is copied out of the object memory before the call and every result is
built after it.

`SQLFetch` is bracketed too, and it is the one people forget. A driver reads
rows in blocks, so most fetches answer from memory and a minority go to the
server — the stall is rare, large, and arrives in the middle of a loop that
looks local.

So are four that look local and are not: freeing a statement handle releases a
server-side cursor, `SQL_ATTR_CURRENT_CATALOG` is a `USE`, and `SQLGetInfo` and
`SQLDescribeCol` answer from a client-side cache on most drivers and ask on
some. *Most drivers* is the whole problem — a call that blocks on one machine
and not another produces a stall that only ever happens in production. The rule
is that everything crossing into the driver manager is bracketed unless it
demonstrably cannot block, and only `SQLBindParameter`, `SQLAllocHandle`,
`SQLGetDiagRec`, `SQLNumResultCols` and `SQLRowCount` meet that bar.

The payoff is the thing no other Smalltalk has. Every production Smalltalk
schedules its processes green, so N processes share one connection and take
turns at one socket. Here N workers hold N connections on N cores and the
database sees N clients. **One connection per process** is not a limitation of
this implementation; it is what makes the parallelism real.

## Exact decimals

`DECIMAL` and `NUMERIC` come back as the digits the database printed, and
`Odbc class>>numberFromDigits:` reads them into a `Fraction`.

This is deliberate and it is the one respect in which the port is better than
the Java it comes from. A money column read through a `double` is wrong in
exactly the cases anybody cares about, and wrong quietly. Smalltalk has exact
arithmetic, so the only place the exactness could be lost is this boundary — and
the boundary is where it is easiest not to lose it.

```smalltalk
(Odbc numberFromDigits: '0.10') * 10   "1, exactly"
'0.10' asNumber * 10                   "1.0, a Float that was never one tenth"
```

The image's own `Number class>>readFrom:` converts the fractional part with
`asFloat` and divides. That is 1983's behaviour and it stays; this package does
not go through it.

**This works where the database has a decimal type, and SQLite does not.**
SQLite applies numeric affinity: a column declared `DECIMAL(20,4)` stores
`12345678901234.5678` as a C double and hands back `12345678901234.6`. The
digits are gone at `INSERT`, before anything here sees them, and its ODBC driver
then reports the column as `VARCHAR` — so this package correctly answers the
String it was given, and there was never an exact value to answer. Anyone
checking the exactness claim on SQLite will conclude it does not hold; it is
SQLite that does not hold it. PostgreSQL, Oracle, SQL Server and MySQL all have
a real `DECIMAL` and report it as one.

## What a column answers

| SQL | Smalltalk |
|---|---|
| `TINYINT` … `BIGINT` | `Integer`, exactly, at any width |
| `REAL` `FLOAT` `DOUBLE` | `Float` |
| `DECIMAL` `NUMERIC` | `Fraction` — exact |
| `CHAR` `VARCHAR` and the long and national forms | `String` |
| `BINARY` `VARBINARY` `BLOB` | `ByteArray` |
| `DATE` | `Date` |
| `TIME` | `Time` |
| `TIMESTAMP` | `DbTimestamp` |
| `BIT` `BOOLEAN` | `true` / `false` |
| `NULL` | `nil` |

`DbTimestamp` exists because the 1983 image has no single class for a date and
a time together and its `Time` is whole seconds. A `TIMESTAMP` is both halves at
once and carries a fraction; rounding that away at the boundary would be a data
loss nobody asked for. It answers `asDateAndTime` in an image with
`pharo/System-Time` loaded, and looks the global up at runtime so that the
package still loads in a profile that has no `DateAndTime`.

## Two divergences from the Java, both about parallelism

**Every row is a new record.** The Java reuses one `Record` across a cursor.
Here the process that took the row and the process advancing the cursor can be
on two cores at once, so reuse would turn `cursor fetchAll` into a collection of
N references to one row holding the last one's values — invisible in a
green-threaded Smalltalk, immediate here. An allocation per row is small beside
the round trip that produced it.

**`update` writes only what changed.** The Java writes every column. Two
processes updating two different fields of one row would each overwrite the
other's — last writer wins on data it never touched, and neither ever sees a
conflict. In a green-threaded Smalltalk that race needs two transactions to
interleave; here it needs two cores.

The key is matched **as it was read**, not as it now stands: a record whose
primary key is itself changed must still be found by its old key, or the
`UPDATE` matches nothing — or matches a different row that has since taken the
new value.

## Injection

Not one value written through this package is ever pasted into SQL text. Every
one goes as a bound parameter, so there is nothing for a quote in a surname to
break and nothing for an attacker to close early. The only things built into the
text are table and column names, and those come from the schema.

That is why every method taking a value takes it separately:

```smalltalk
q where: 'salary > ?' with: (Array with: amount)   "safe with any value"
q where: 'salary > ', amount printString           "not, and never"
```

## The classes

| | |
|---|---|
| `Odbc` | The only class that knows a primitive exists |
| `DbConnection` | One per process. Transactions, catalogue, caches |
| `DbCommand` | A prepared statement, reused across rows |
| `DbCursor` | A result set, one row at a time |
| `DbRecord` | One row, and the ability to write it back |
| `DbQueryBuilder` | SQL from what you want, not how to reach it |
| `DbSchemaGraph` | Tables as nodes, foreign keys as edges |
| `DbForeignKey` | One key, composite or not |
| `DbColumnInfo` | What a result set says about a column |
| `DbTimestamp` | A moment, to the precision a database keeps one |
| `DbWhereGroup` | A node of a `WHERE` tree |
| `DbError` | What the driver said, in the driver's own words |

Everything above `Odbc` is ordinary Smalltalk sending ordinary messages. The
whole package can be read without knowing ODBC, a second driver could be put
underneath without touching anything above it, and there is exactly one file to
look in when a database does something a database should not.

## The primitive

**One primitive, number 129**, taking a command number and three operands — the
same shape primitive 130 has for the whole file system. A subsystem that will
grow should grow inside its own number: numbers are a space of 255 that the Blue
Book, Squeak, Pharo and this system are all already spending, and one given away
is never given back.

Handles are small integers indexing tables in `src/db/st_odbc.c`, not addresses.
This system writes its memory to a file and reads it back in another process; an
image holding a `SQLHDBC` from a previous life would find it plausible and
dereference it. Every index is checked against the table before use, and a
resumed image finds every slot empty — which is the truth.

**Two failures, two routes**, and getting them the same way round is what makes
a diagnostic useful:

- The primitive **fails** — the Smalltalk fallback runs and raises — when the
  *arguments* were wrong. That is a bug in `Odbc`.
- The primitive **succeeds and answers nil** when the *database* said no. That
  is not a bug, it is news, and `Odbc class>>lastError` carries the driver's own
  words, SQLSTATE and all.

## Building

ODBC is optional and its absence is a first-class outcome. A build without it
has every method present and `Odbc class>>isAvailable` answering false;
attempting to connect raises with a sentence saying so. Compiling the package
out instead would turn "no database on this machine" into a
`doesNotUnderstand` on a class that does not exist, which says nothing about
databases to whoever reads it.

```
make deps      # says whether ODBC was found, and how to install it
make NODB=1    # build without it, on a machine that has it
```

Fedora: `sudo dnf install unixODBC-devel`. Debian: `unixodbc-dev`. macOS ships
iODBC and Homebrew has unixODBC; both are found. Windows carries the driver
manager in the operating system.

## Testing

The suites are split by what they need, and the split is the point.

**`lib/Database-Tests`** — 43 tests, in the `st2026` profile, run by `make test`
on every machine. The join-path search, the SQL the builder generates, and the
exact decimals. None of them needs a database, and none of them should: the
generator's job is to write one particular string and put the parameters in one
particular order, and a test that ran the query would need a database to say
what testing the text says directly.

**`tests/unit/test_odbc_parallel.c`** — the one that gates the parking, and the
only test here that runs the database from real worker threads. It skips
cleanly without ODBC or without a driver.

It exists because writing this document exposed a gap: `st80 -tests` starts no
worker pool, so the 129 tests below fork *green* processes, `WORKER_enter_native`
returns immediately with no worker to park, and the parking path — the entire
reason a query does not stall every core — was never once taken.

It runs two phases. Thirty-one threads, thirty-one connections, statements
throughout, while another thread stops the world two hundred thousand times;
then one worker inside a single long query with no polling point in it, timing
how long a safepoint takes to be granted. The second is the gate, and it was
checked by deleting the parking and re-running:

| | query | safepoint granted in |
|---|---|---|
| with the native regions declared | 0.24s | **0.0000s** |
| without them | 0.24s | **0.2396s** |

The first phase stays green either way — worth knowing, because it is the phase
that *looks* like the test.

**`lib/Database-Live-Tests`** — 129 tests, in `profiles/database-live.profile`,
run deliberately:

```
./st80 -bootstrap -profile profiles/database-live.profile -tests
```

They cover every line of the primitive, which no amount of testing the generator
will reach. SQLite through ODBC, because it is the one database needing no
server — the whole fixture is a file the test makes and empties.

They are **not** in `tests/profiles.expected` and must not be. That file is a
ratchet whose whole purpose is that a suite which stops being found reports a
perfect score; a suite that cannot run on the machine checking it would report
exactly that, every time, and teach a reader to ignore the file.

### What the tests found

Three of the four bugs found here were found by tests, and the fourth by writing
this document and noticing that nothing tested what it claimed.

The offline suite found two real faults on its first run, both of which produce
SQL that runs and is wrong:

- The scanner reading table names out of an expression skipped every
  parenthesised group as a subquery. `COUNT(employee.employee_id)` is
  parenthesised and is not a subquery, so the table being counted was never
  joined and the query answered a count over a cross product.
- A join to an aliased table wrote the real table name in its `ON` clause — a
  reference to a table the `FROM` clause never introduced.

The live suite found two more, both in C.

Autocommit is off by design, so a connection that did anything at all — a
`SELECT` included — has a transaction open when it closes. ODBC does not say what
disconnecting then does, and drivers disagree: some roll back, some commit, and
SQLite's left the rollback journal on disk and the database locked. The symptom
was a suite that hung on its second connection. `ST_odbc_disconnect` now rolls
back first, which is also the only honest reading — work that was not committed
was not committed, and a driver that decides to commit it is inventing an
intention nobody expressed.

And adding `DbParallelTest` segfaulted the VM inside the SQLite driver's own
`memmove`. `SQLBindParameter` records the *address* of a parameter's length
indicator, and the driver reads it later, at `SQLExecute`. Every parameter lived
in one `realloc`'d array, so binding parameter N moved every parameter bound
before it and left the driver reading freed memory. A statement now holds an
array of *pointers* to individually allocated parameters: the pointer array may
move, the parameters never do.

**The crash was the loud symptom. The quiet one is worse.** Putting the bug back
and running the suite again fails two tests that have nothing to do with
parallelism:

```
FAIL testBoundParametersArriveAsThemselves: expected 'Alan' but got 'Al'
FAIL testColumnNamesAreCaseInsensitive:     expected 'Ada'  but got 'Ad'
```

A stale indicator is a wrong *length*, so string parameters were being stored
truncated. Silently, into the database, permanently.

The bug had been latent since the code was written. `realloc` usually grows in
place, so every earlier run bound its three and five parameters without anything
moving. Adding a test that runs four processes at once changed the heap enough
to make `realloc` relocate — and the failures then appeared in two *single-
threaded* tests that had been passing all along.

That is the argument for the parallel test in one paragraph. It did not fail
itself; it made an existing bug reproducible in the tests that could name it.
AddressSanitizer does not catch this one — its allocator does not relocate where
glibc's does — so the concurrency was not incidental to finding it.

## Provenance

`lib/Database` is a port of `org.kissweb.database` from
[Kiss](https://github.com/blakemcbride/Kiss), same author, same 2-clause
licence. No Java was copied: the SQL generation, the Steiner-tree join search
and the type mapping are the same designs re-expressed, and the JDBC edge was
replaced with ODBC. Where the two now differ deliberately — new record per row,
update-what-changed, exact decimals — the reason is in this document and in the
class comments.

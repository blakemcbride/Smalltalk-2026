# lib/Database

A port of `org.kissweb.database` from [Kiss](https://github.com/blakemcbride/Kiss),
by the same author, under the same 2-clause BSD licence.

**No Java was copied.** This is not a translation and nothing here was produced
mechanically from the Java. What came across is the design: the fluent query
builder, the Steiner-tree approximation that finds a join path, the record that
knows how to write itself back, and the mapping between SQL types and language
types. Every line is written for this system.

## What changed, and why

**JDBC became ODBC.** The Java's contact with its driver is about thirty calls,
and every one has an ODBC equivalent — including the metadata calls, which is
the part that usually makes such a port impossible. `java.sql.Types` *is* ODBC's
type numbering, so the type switches carried over with their constants
unchanged. See [`doc/DATABASE.md`](../../doc/DATABASE.md).

**A new record per row.** The Java reuses one `Record` across a cursor. Here the
process holding a row and the process advancing the cursor can be on two cores
at once, so reuse would make `fetchAll` answer N references to one row.

**`update` writes only the changed columns.** The Java writes every column,
which makes two processes updating two different fields of one row overwrite
each other silently. Both divergences are the same fact: this system's processes
are parallel and Java's `Connection` was documented as one-per-thread.

**`DECIMAL` answers a `Fraction`.** The Java hands the digits to `BigDecimal`
and must then remember never to ask it for a double. Smalltalk's exact
arithmetic makes the boundary the natural place to keep the exactness, so this
is the one respect in which the port is better than its source.

**`getGeneratedKeys` and `createArrayOf` have no ODBC equivalent.** The first is
done per database in `DbRecord>>insertReturningKey:` — the Java has the same
switch in the same method. The second was never portable JDBC either (the Java
calls it with PostgreSQL type names); `whereIn:` binds one parameter per
element, which works everywhere.

**`restServer/`'s protocol was ported; its Groovy, Java and Lisp loaders and
its servlet plumbing were not, and will not be.** They are JVM-reflection and
servlet-container architecture rather than functionality; what a Kiss front
end speaks is functionality, and `lib/Rest-Server/PROVENANCE.md` records what
of it crossed.

## Classes that vanished

`ArrayListInteger`, `ArrayListLong`, `ArrayListShort`, `ArrayListString` and
`ArrayListType` exist in the Java to give a typed array a runtime identity the
type erasure had removed. Smalltalk collections carry their elements' classes,
so there is nothing for them to do.

## Upstream

Read from `Kiss/src/main/core/org/kissweb/database` at 6,606 lines across
fourteen files, of which `Connection`, `Command`, `Cursor`, `Record`,
`QueryBuilder`, `SchemaGraph` and `ColumnInfo` have counterparts here.

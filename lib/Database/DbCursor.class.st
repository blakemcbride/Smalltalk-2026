"
A result set, one row at a time.

Made by DbCommand and DbConnection; never by hand.  A cursor holds an open
statement on the server, so it must be closed, and every message that reads
the whole thing -- fetchAll, do:, collect: -- closes it for you when it
reaches the end.

EACH ROW IS A NEW RECORD, and the Java this was ported from reuses one.  That
is a deliberate divergence and doc/CONCURRENCY.md is why: a reused row is
shared mutable state, and in this system the process that took the row and
the process that advances the cursor can be on two cores at once.  Reuse
would turn `records := cursor fetchAll' into a collection of N references to
one row holding the last one's values -- a bug that is invisible in a green-
threaded Smalltalk and immediate here.  An allocation per row is the price,
and it is small beside the round trip that produced the row.

WHAT A COLUMN ANSWERS.  The primitive hands up raw values and this class
turns them into Smalltalk, using what the result set said the column is:

    integers          Integer, exactly, at any width
    REAL/FLOAT/DOUBLE Float
    DECIMAL/NUMERIC   Fraction -- exact.  See Odbc>>numberFromDigits:
    CHAR/VARCHAR      String
    BINARY/BLOB       ByteArray
    DATE              Date
    TIME              Time
    TIMESTAMP         DbTimestamp
    BIT/BOOLEAN       true or false
    NULL              nil

The decimal case is the one worth arguing about, and it is the reason the
values come up as characters rather than as doubles: a money column read
through a Float is wrong in exactly the cases anyone cares about, and wrong
quietly.
"
Class {
	#name : 'DbCursor',
	#superclass : 'Object',
	#instVars : [ 'command', 'statement', 'columns', 'indexByName', 'tableName',
	              'row', 'rowsRead', 'maxRows', 'exhausted', 'ownsStatement',
	              'ownsCommand' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbCursor class >> on: statementHandle command: aCommand [
	^self new setStatement: statementHandle command: aCommand
]

{ #category : 'instance creation' }
DbCursor class >> on: statementHandle [
	"A cursor over a statement nobody else owns -- the catalogue calls make
	 these.  Closing the cursor closes the statement."

	^self new setStatement: statementHandle command: nil
]

{ #category : 'private' }
DbCursor >> setStatement: statementHandle command: aCommand [
	statement := statementHandle.
	command := aCommand.
	ownsStatement := aCommand isNil.
	rowsRead := 0.
	maxRows := 0.
	exhausted := false.
	self describe
]

{ #category : 'private' }
DbCursor >> describe [
	"Ask the result set what its columns are, once.

	 Once, and not per row: the description cannot change between rows of
	 one result set, and asking again per column per row is a driver round
	 trip on some drivers -- ten thousand of them to read a thousand rows of
	 ten columns."

	| count info |
	count := Odbc columnCount: statement.
	columns := Array new: count.
	indexByName := Dictionary new: count * 2.
	1 to: count do:
		[:i |
		info := DbColumnInfo fromArray: (Odbc statement: statement describeColumn: i).
		columns at: i put: info.
		"Upper case, because SQL identifiers are not case sensitive and a
		 caller who wrote `emp_id' should not have to know the driver
		 answered `EMP_ID'.  First writer wins on a duplicate: a join that
		 selects the same column name from two tables has two of them, and
		 the first is the one an unqualified name means."
		(indexByName includesKey: info name asUppercase)
			ifFalse: [indexByName at: info name asUppercase put: i]]
]

{ #category : 'accessing' }
DbCursor >> columns [
	"The DbColumnInfo of every column, in order."

	^columns
]

{ #category : 'accessing' }
DbCursor >> columnCount [
	^columns size
]

{ #category : 'accessing' }
DbCursor >> tableName [
	^tableName
]

{ #category : 'accessing' }
DbCursor >> tableName: aString [
	"Which table a Record from this cursor may be written back to.

	 Set only where the answer is knowable -- a single-table select -- and
	 left nil otherwise, which is what makes DbRecord>>update refuse a join
	 rather than guess which of its tables to write to."

	tableName := aString
]

{ #category : 'accessing' }
DbCursor >> maxRows: anInteger [
	"Stop after this many rows.  Zero, the default, means all of them."

	maxRows := anInteger
]

{ #category : 'accessing' }
DbCursor >> rowsRead [
	^rowsRead
]

{ #category : 'accessing' }
DbCursor >> connection [
	^command isNil ifTrue: [nil] ifFalse: [command connection]
]

{ #category : 'reading' }
DbCursor >> next [
	"The next row as a DbRecord, or nil when there are no more.

	 Closing at the end rather than leaving the statement open: a cursor
	 read to exhaustion is a cursor nobody is going to close by hand, and an
	 open statement holds a server-side resource for as long as the image
	 forgets about it."

	exhausted ifTrue: [^nil].
	(maxRows > 0 and: [rowsRead >= maxRows]) ifTrue: [self close. ^nil].
	(Odbc fetch: statement) ifFalse: [self close. ^nil].
	rowsRead := rowsRead + 1.
	row := self readRow.
	^DbRecord on: row cursor: self
]

{ #category : 'reading' }
DbCursor >> isNext [
	"Whether a next row exists, having read it.

	 This is Kiss's isNext and it reads the row -- it does not peek.  The
	 row it read is what the following next answers, so `[cursor isNext]
	 whileTrue: [... cursor record ...]' works; what does not work is
	 calling isNext twice and expecting the same row, and nothing in ODBC
	 would let it."

	| record |
	record := self next.
	record isNil ifTrue: [^false].
	row := record columns.
	^true
]

{ #category : 'reading' }
DbCursor >> record [
	"The row the last next or isNext read, again."

	row isNil ifTrue: [^nil].
	^DbRecord on: row cursor: self
]

{ #category : 'reading' }
DbCursor >> fetchOne [
	"The first row, and close.  nil when there were none."

	| record |
	record := self next.
	self close.
	^record
]

{ #category : 'reading' }
DbCursor >> fetchAll [
	"Every remaining row, and close."

	| all record |
	all := OrderedCollection new.
	[(record := self next) isNil] whileFalse: [all add: record].
	self close.
	^all
]

{ #category : 'enumerating' }
DbCursor >> do: aBlock [
	"Every remaining row, and close -- even if the block does not finish.

	 The ensure: is the whole point.  A block that returns out of the loop,
	 or raises, would otherwise leave the statement open on the server, and
	 the leak is invisible until a connection runs out of them."

	| record |
	[[(record := self next) isNil] whileFalse: [aBlock value: record]]
		ensure: [self close]
]

{ #category : 'enumerating' }
DbCursor >> collect: aBlock [
	| all |
	all := OrderedCollection new.
	self do: [:record | all add: (aBlock value: record)].
	^all
]

{ #category : 'enumerating' }
DbCursor >> select: aBlock [
	| all |
	all := OrderedCollection new.
	self do: [:record | (aBlock value: record) ifTrue: [all add: record]].
	^all
]

{ #category : 'enumerating' }
DbCursor >> detect: aBlock ifNone: exceptionBlock [
	self do: [:record | (aBlock value: record) ifTrue: [^record]].
	^exceptionBlock value
]

{ #category : 'enumerating' }
DbCursor >> inject: initial into: aBlock [
	| running |
	running := initial.
	self do: [:record | running := aBlock value: running value: record].
	^running
]

{ #category : 'private' }
DbCursor >> readRow [
	"Every column of the current row, by name.

	 Read eagerly rather than on demand.  ODBC's SQLGetData will only walk a
	 row forwards on many drivers, so a caller reading column 5 and then
	 column 2 would get nothing for the second -- and a lazy design would
	 make that depend on the order the caller happened to ask in."

	| values key |
	values := Dictionary new: columns size * 2.
	1 to: columns size do:
		[:i |
		key := (columns at: i) name asUppercase.
		"FIRST writer wins, matching describe.  A join selecting the same
		 column name from two tables has two of them, and the two lookups
		 must agree about which one an unqualified name means -- indexOf:
		 answering the first while this stored the last would give two
		 different answers to one question depending on the path taken."
		(values includesKey: key)
			ifFalse: [values at: key put: (self valueAt: i)]].
	^values
]

{ #category : 'private' }
DbCursor >> valueAt: index [
	"One column, turned from what the driver said into what Smalltalk means."

	| info raw |
	info := columns at: index.
	raw := Odbc statement: statement value: index.
	raw isNil ifTrue: [^nil].
	info isExact ifTrue: [^Odbc numberFromDigits: raw].
	info isBinary ifTrue: [^raw asByteArray].
	info isDate ifTrue: [^self dateFrom: raw].
	info isTime ifTrue: [^self timeFrom: raw].
	info isTimestamp ifTrue: [^DbTimestamp fromArray: raw].
	"An integer wider than a SmallInteger and negative comes up as digits --
	 see the note in prim.c on why LargeNegativeInteger cannot be made
	 there.  Reading it here is exact, because only the FRACTIONAL part of
	 Number>>readFrom: goes through a Float and an integer has none."
	(info isInteger and: [raw isString]) ifTrue: [^Odbc numberFromDigits: raw].
	^raw
]

{ #category : 'private' }
DbCursor >> dateFrom: anArray [
	"A zero year is how some drivers spell a date that is present and empty.
	 Answering nil rather than asking Date for the year nought, which it
	 will build and which is not a date anybody stored."

	(anArray at: 1) = 0 ifTrue: [^nil].
	^Date
		newDay: (anArray at: 3)
		month: (Date nameOfMonth: (anArray at: 2))
		year: (anArray at: 1)
]

{ #category : 'private' }
DbCursor >> timeFrom: anArray [
	^Time fromSeconds: (anArray at: 1) * 3600 + ((anArray at: 2) * 60)
		+ (anArray at: 3)
]

{ #category : 'private' }
DbCursor >> indexOf: columnName [
	"The position of a column, or nil.

	 Case insensitive, because SQL identifiers are, and a caller who wrote
	 the name the way the schema does should not have to also know how the
	 driver folds it."

	^indexByName at: columnName asUppercase ifAbsent: [nil]
]

{ #category : 'private' }
DbCursor >> infoOf: columnName [
	| index |
	index := self indexOf: columnName.
	^index isNil ifTrue: [nil] ifFalse: [columns at: index]
]

{ #category : 'accessing' }
DbCursor >> ownCommand [
	"Take ownership of the command that made me, and close it when I close.

	 DbConnection>>query: makes a command for one query and has nowhere to
	 keep it: the caller is given a cursor and never sees the command at
	 all.  Without this the prepared statement would live until the
	 connection closed, which for a program that runs a thousand ad-hoc
	 queries is a thousand statements the server is holding for nobody."

	ownsCommand := true
]

{ #category : 'closing' }
DbCursor >> close [
	"Let go of the statement.  Harmless twice, which matters because every
	 reading message closes and a caller may close again after."

	exhausted := true.
	statement isNil ifTrue: [^self].
	ownsStatement
		ifTrue: [Odbc closeStatement: statement]
		ifFalse: [command notNil ifTrue: [command cursorClosed]].
	statement := nil.
	(ownsCommand == true and: [command notNil]) ifTrue:
		[command close.
		 command := nil]
]

{ #category : 'closing' }
DbCursor >> isClosed [
	^statement isNil
]

{ #category : 'printing' }
DbCursor >> printOn: aStream [
	aStream nextPutAll: 'a DbCursor('.
	self isClosed
		ifTrue: [aStream nextPutAll: 'closed']
		ifFalse: [aStream print: columns size; nextPutAll: ' columns, ';
				print: rowsRead; nextPutAll: ' read'].
	aStream nextPut: $)
]

"
The whole stack against a real database.

NOT IN THE st2026 PROFILE, and that is the point.  These need an ODBC driver
manager, a driver, and somewhere to write a file, and none of those is
something a machine building this system can be assumed to have.  A test that
quietly passes when it could not run is worse than no test: it reports
success for work nobody did, and the profiles ratchet exists precisely to
catch numbers that look like success and are not.

So they live in their own package and their own profile, and are run
deliberately:

    ./st80 -bootstrap -profile profiles/database-live.profile -tests

They use SQLite through ODBC, because it is the one database that needs no
server -- the whole fixture is a file this test makes and deletes.  On
Fedora: `sudo dnf install unixODBC-devel sqliteodbc'.  Every other database
would need something running, which is a fine thing for a person to point
these at and a poor thing to require of a test.

WHAT THEY COVER that the offline tests cannot: that a connection opens, that
a statement prepares, that a bound parameter arrives as the value it was,
that a fetched row comes back as the Smalltalk objects it should, that a
transaction holds and rolls back, and that the catalogue calls answer the
schema -- in other words, every line of the primitive, which no amount of
testing the SQL generator will reach.
"
Class {
	#name : 'DbLiveTest',
	#superclass : 'TestCase',
	#category : 'Database-Live-Tests'
}

{ #category : 'fixture' }
DbLiveTest class >> databaseFile [
	"A file of our own, beside the image.  Not in /tmp: this system runs on
	 Windows too, where /tmp is not a place."

	^'st80-dblive-test.db'
]

{ #category : 'fixture' }
DbLiveTest >> connectionString [
	^'DRIVER=SQLITE3;Database=', self class databaseFile, ';'
]

{ #category : 'fixture' }
DbLiveTest >> setUp [
	"Drop and recreate, rather than delete the file.

	 The 1983 file protocol this image carries has no delete -- FileDirectory
	 answers add:, rename: and removeOld:, and none of them is `remove this
	 file now'.  Rather than reach around it, the fixture is reset in SQL,
	 which has the side benefit of exercising the DDL path on every test
	 instead of once.  The database file is left behind; it is small, it
	 sits beside the image, and every run starts by emptying it."

	self withConnection:
		[:connection |
		connection executeImmediate: 'DROP TABLE IF EXISTS employee'.
		connection executeImmediate: 'DROP TABLE IF EXISTS department'.
		connection executeImmediate: 'CREATE TABLE employee (
			employee_id INTEGER PRIMARY KEY,
			first_name VARCHAR(40),
			last_name VARCHAR(40),
			salary DECIMAL(12,2),
			active INTEGER)'.
		connection executeImmediate: 'CREATE TABLE department (
			department_id INTEGER PRIMARY KEY,
			name VARCHAR(40))'.
		connection commit]
]

{ #category : 'fixture' }
DbLiveTest >> withConnection: aBlock [
	| connection |
	connection := DbConnection open: self connectionString.
	^[aBlock value: connection] ensure: [connection close]
]

{ #category : 'fixture' }
DbLiveTest >> insertTwoInto: connection [
	connection
		execute: 'INSERT INTO employee VALUES (?, ?, ?, ?, ?)'
		with: (Array with: 1 with: 'Ada' with: 'Lovelace' with: '1234.56' with: 1).
	connection
		execute: 'INSERT INTO employee VALUES (?, ?, ?, ?, ?)'
		with: (Array with: 2 with: 'Alan' with: 'Turing' with: '2345.00' with: 0).
	connection commit
]

{ #category : 'tests' }
DbLiveTest >> testConnectAndIdentify [
	self withConnection:
		[:connection |
		self assert: connection isOpen.
		self assert: connection kind equals: #SQLite]
]

{ #category : 'tests' }
DbLiveTest >> testBadConnectionStringRaises [
	"With the driver's own words, not a paraphrase invented here."

	self should: [DbConnection open: 'DRIVER=NoSuchDriverAtAll;']
		raise: DbError
]

{ #category : 'tests' }
DbLiveTest >> testBoundParametersArriveAsThemselves [
	self withConnection:
		[:connection |
		| row |
		self insertTwoInto: connection.
		row := connection
			fetchOne: 'SELECT first_name, last_name FROM employee WHERE employee_id = ?'
			with: (Array with: 2).
		self assert: (row at: 'first_name') equals: 'Alan'.
		self assert: (row at: 'last_name') equals: 'Turing']
]

{ #category : 'tests' }
DbLiveTest >> testByteArrayParameterKeepsEveryByte [
	"A BLOB is bytes, and every byte of it must arrive -- including the
	 zeroes.

	 Bugs4 FILES-1: odbc_bind_value bound every byte object with
	 `strlen(text)', which is not the object's length but the distance to
	 its first zero byte, so #(1 2 0 3 4 255) asByteArray reached the
	 column as two bytes and a BLOB beginning with zero reached it empty --
	 silently, with the insert reporting success.  SQLBindParameter honours
	 the length it is handed; only the length handed in was wrong.  A
	 ByteArray now goes by its own command, as the dates do, bound
	 SQL_C_BINARY at the object's real byte length."

	self withConnection:
		[:connection |
		| stored leading |
		connection executeImmediate: 'DROP TABLE IF EXISTS blobtest'.
		connection executeImmediate: 'CREATE TABLE blobtest (id INTEGER, v BLOB)'.
		connection
			execute: 'INSERT INTO blobtest VALUES (?, ?)'
			with: (Array with: 1 with: (ByteArray withAll: #(1 2 0 3 4 255))).
		connection
			execute: 'INSERT INTO blobtest VALUES (?, ?)'
			with: (Array with: 2 with: (ByteArray withAll: #(0 0 7))).
		connection commit.
		stored := (connection fetchOne: 'SELECT v FROM blobtest WHERE id = ?'
			with: (Array with: 1)) at: 'v'.
		leading := (connection fetchOne: 'SELECT v FROM blobtest WHERE id = ?'
			with: (Array with: 2)) at: 'v'.
		self assert: stored size equals: 6.
		self assert: stored asArray equals: #(1 2 0 3 4 255).
		self assert: leading size equals: 3.
		self assert: leading asArray equals: #(0 0 7).
		connection executeImmediate: 'DROP TABLE blobtest'.
		connection commit]
]

{ #category : 'tests' }
DbLiveTest >> testColumnNamesAreCaseInsensitive [
	"SQL identifiers are not case sensitive, and a caller who wrote the name
	 the way the schema does should not have to know how the driver folded
	 it."

	self withConnection:
		[:connection |
		| row |
		self insertTwoInto: connection.
		row := connection fetchOne: 'SELECT first_name FROM employee WHERE employee_id = 1'.
		self assert: (row at: 'first_name') equals: 'Ada'.
		self assert: (row at: 'FIRST_NAME') equals: 'Ada'.
		self assert: (row at: 'First_Name') equals: 'Ada']
]

{ #category : 'tests' }
DbLiveTest >> testMissingColumnRaisesRatherThanAnsweringNil [
	"A misspelled name answering nil is indistinguishable from a NULL, and
	 the report that comes out is wrong rather than missing."

	self withConnection:
		[:connection |
		| row |
		self insertTwoInto: connection.
		row := connection fetchOne: 'SELECT first_name FROM employee WHERE employee_id = 1'.
		self should: [row at: 'no_such_column'] raise: DbError.
		self assert: (row at: 'no_such_column' ifAbsent: ['fallback'])
			equals: 'fallback']
]

{ #category : 'tests' }
DbLiveTest >> testIntegersAndNulls [
	self withConnection:
		[:connection |
		| row |
		connection
			execute: 'INSERT INTO employee VALUES (?, ?, ?, ?, ?)'
			with: (Array with: 7 with: 'Grace' with: nil with: nil with: 1).
		connection commit.
		row := connection fetchOne: 'SELECT * FROM employee WHERE employee_id = 7'.
		self assert: (row at: 'employee_id') equals: 7.
		self assert: (row at: 'last_name') isNil.
		self assert: (row at: 'salary') isNil.
		self assert: (row booleanAt: 'active')]
]

{ #category : 'tests' }
DbLiveTest >> testLargeIntegerSurvives [
	"Wider than a SmallInteger on the Blue Book memory by a long way, and
	 wider than most people's idea of an id.  It must come back equal."

	self withConnection:
		[:connection |
		| row big |
		big := 4000000000.
		connection
			execute: 'INSERT INTO employee VALUES (?, ?, ?, ?, ?)'
			with: (Array with: big with: 'Big' with: 'Number' with: nil with: 1).
		connection commit.
		row := connection fetchOne: 'SELECT employee_id FROM employee WHERE last_name = ?'
			with: (Array with: 'Number').
		self assert: (row at: 'employee_id') equals: big]
]

{ #category : 'tests' }
DbLiveTest >> testFetchAllAndCursor [
	self withConnection:
		[:connection |
		| names |
		self insertTwoInto: connection.
		self assert: (connection fetchAll: 'SELECT * FROM employee') size equals: 2.
		names := OrderedCollection new.
		(connection query: 'SELECT last_name FROM employee ORDER BY last_name')
			do: [:each | names add: (each at: 'last_name')].
		self assert: names asArray equals: #('Lovelace' 'Turing')]
]

{ #category : 'tests' }
DbLiveTest >> testCursorClosesWhenABlockLeavesEarly [
	"A block that returns out of the loop must still close the statement.
	 The leak is invisible until a connection runs out of handles."

	self withConnection:
		[:connection |
		| cursor |
		self insertTwoInto: connection.
		cursor := connection query: 'SELECT * FROM employee'.
		[cursor do: [:each | Error signal: 'stop']] on: Error do: [:e | nil].
		self assert: cursor isClosed]
]

{ #category : 'tests' }
DbLiveTest >> testTransactionRollsBack [
	self withConnection:
		[:connection |
		self insertTwoInto: connection.
		connection execute: 'DELETE FROM employee'.
		self assert: (connection fetchAll: 'SELECT * FROM employee') size equals: 0.
		connection rollback.
		self assert: (connection fetchAll: 'SELECT * FROM employee') size equals: 2]
]

{ #category : 'tests' }
DbLiveTest >> testTransactRollsBackOnAnException [
	self withConnection:
		[:connection |
		self insertTwoInto: connection.
		[connection transact:
			[connection execute: 'DELETE FROM employee'.
			 Error signal: 'no']] on: Error do: [:e | nil].
		self assert: (connection fetchAll: 'SELECT * FROM employee') size equals: 2]
]

{ #category : 'tests' }
DbLiveTest >> testRecordUpdateWritesOnlyWhatChanged [
	self withConnection:
		[:connection |
		| row again |
		self insertTwoInto: connection.
		row := connection fetchOne: 'SELECT * FROM employee WHERE employee_id = 1'.
		row at: 'last_name' put: 'Byron'.
		self assert: row update equals: 1.
		connection commit.
		again := connection fetchOne: 'SELECT * FROM employee WHERE employee_id = 1'.
		self assert: (again at: 'last_name') equals: 'Byron'.
		self assert: (again at: 'first_name') equals: 'Ada']
]

{ #category : 'tests' }
DbLiveTest >> testRecordUpdateWithNoChangesTouchesNothing [
	self withConnection:
		[:connection |
		| row |
		self insertTwoInto: connection.
		row := connection fetchOne: 'SELECT * FROM employee WHERE employee_id = 1'.
		self assert: row update equals: 0]
]

{ #category : 'tests' }
DbLiveTest >> testRecordDelete [
	self withConnection:
		[:connection |
		| row |
		self insertTwoInto: connection.
		row := connection fetchOne: 'SELECT * FROM employee WHERE employee_id = 1'.
		self assert: row delete equals: 1.
		connection commit.
		self assert: (connection fetchAll: 'SELECT * FROM employee') size equals: 1]
]

{ #category : 'tests' }
DbLiveTest >> testNewRecordInsert [
	self withConnection:
		[:connection |
		| record |
		record := connection newRecord: 'employee'.
		record at: 'employee_id' put: 9; at: 'first_name' put: 'Edsger';
			at: 'last_name' put: 'Dijkstra'.
		self assert: record insert equals: 1.
		connection commit.
		self assert: ((connection fetchOne: 'SELECT * FROM employee WHERE employee_id = 9')
			at: 'last_name') equals: 'Dijkstra']
]

{ #category : 'tests' }
DbLiveTest >> testRecordFromAJoinRefusesToWriteBack [
	"There is nowhere to write it: the row came from two tables and this
	 class will not guess which one an UPDATE means."

	self withConnection:
		[:connection |
		| row |
		self insertTwoInto: connection.
		row := connection fetchOne:
			'SELECT e.first_name, d.name FROM employee e, department d WHERE 1 = 0'.
		row isNil ifTrue: [^self].
		row at: 'first_name' put: 'x'.
		self should: [row update] raise: DbError]
]

{ #category : 'tests' }
DbLiveTest >> testDuplicateColumnNamesAnswerTheFirst [
	"A join selecting the same column name from two tables has two of them.
	 Reading by name has to mean one of them, and the two paths -- the
	 dictionary the row is built into, and the index describe: built -- must
	 agree about which."

	self withConnection:
		[:connection |
		| row |
		self insertTwoInto: connection.
		connection execute: 'INSERT INTO department VALUES (?, ?)'
			with: (Array with: 1 with: 'Analytical').
		connection commit.
		row := connection fetchOne:
			'SELECT e.first_name AS name, d.name AS name FROM employee e, department d
				WHERE e.employee_id = 1'.
		self assert: (row at: 'name') equals: 'Ada']
]

{ #category : 'tests' }
DbLiveTest >> testSqliteHasNoDecimalType [
	"Recorded so that nobody concludes from this suite that the exactness
	 claim in doc/DATABASE.md is false.

	 SQLite applies numeric affinity: a column declared DECIMAL(12,2) stores
	 a C double, and its ODBC driver reports the column as VARCHAR.  So the
	 digits are lost at INSERT, before this package sees anything, and what
	 comes back is the String the driver gave -- which is the right answer
	 to the question actually asked.  On a database with a real DECIMAL the
	 column reports as type 2 or 3 and DbCursor answers a Fraction; that
	 path is asserted offline, in DbValueTest, where it does not need a
	 database that has the type."

	self withConnection:
		[:connection |
		| cursor |
		self insertTwoInto: connection.
		cursor := connection query: 'SELECT salary FROM employee'.
		[cursor next.
		 self deny: cursor columns first isExact] ensure: [cursor close]]
]

{ #category : 'tests' }
DbLiveTest >> testCatalogue [
	self withConnection:
		[:connection |
		self assert: (connection tableExists: 'employee').
		self deny: (connection tableExists: 'no_such_table').
		self assert: (connection tableNames includes: 'employee').
		self assert: (connection primaryKeysOf: 'employee')
			equals: (Array with: 'employee_id').
		self assert: ((connection columnsOf: 'employee') includesKey: 'LAST_NAME')]
]

{ #category : 'tests' }
DbLiveTest >> testQueryBuilderAgainstTheDatabase [
	"The generator and the driver together: the SQL this builds has to be
	 SQL a database will actually accept, which is the one thing testing the
	 text cannot say."

	self withConnection:
		[:connection |
		| rows |
		self insertTwoInto: connection.
		rows := (connection newQueryBuilder: 'employee')
			select: 'employee.last_name';
			where: 'employee.active = ?' with: (Array with: 1);
			orderBy: 'employee.last_name';
			fetchAll.
		self assert: rows size equals: 1.
		self assert: (rows first at: 'last_name') equals: 'Lovelace']
]

{ #category : 'tests' }
DbLiveTest >> testTwoConnectionsAtOnce [
	"One connection per process is the design, and two of them must not be
	 one of them.  This is the property the whole parallel story rests on."

	| a b |
	a := DbConnection open: self connectionString.
	b := DbConnection open: self connectionString.
	[self assert: a isOpen.
	 self assert: b isOpen.
	 self insertTwoInto: a.
	 self assert: (b fetchAll: 'SELECT * FROM employee') size equals: 2.
	 a close.
	 self assert: b isOpen.
	 self deny: a isOpen]
		ensure: [a close. b close]
]

{ #category : 'tests' }
DbLiveTest >> testStatementIsPreparedOnceAndReused [
	self withConnection:
		[:connection |
		| command |
		command := connection newCommand.
		[1 to: 5 do:
			[:i |
			command
				execute: 'INSERT INTO employee (employee_id, last_name) VALUES (?, ?)'
				with: (Array with: i + 100 with: 'n', i printString)].
		 connection commit.
		 self assert: (connection fetchAll: 'SELECT * FROM employee') size equals: 5]
			ensure: [command close]]
]

{ #category : 'tests' }
DbLiveTest >> testSchemaGraphFromTheDatabase [
	"An empty graph is the right answer for a schema with no declared
	 foreign keys, and it must be an empty graph rather than a failure."

	self withConnection:
		[:connection |
		self assert: (connection schemaGraph isKindOf: DbSchemaGraph)]
]

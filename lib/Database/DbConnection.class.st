"
A connection to an SQL database.

ONE PER PROCESS.  That is not a limitation of this implementation, it is the
design, and it is the design that makes this system's database access
genuinely parallel rather than merely concurrent.  Every other Smalltalk
schedules its processes green, so N processes share one connection and take
turns at one socket; here N workers hold N connections on N cores and the
database sees N clients.  Sharing one connection between two processes is a
program error -- the driver has one result set per statement and no notion of
whose turn it is -- and this class does not try to make it work.

WHAT IS SHARED, AND HOW IT IS PROTECTED.  The caches below -- primary keys,
column descriptions, whether a table exists -- are read far more often than
written and are pure functions of the schema.  They are guarded by a Mutex
anyway, because `one per process' is a convention a caller can break and a
corrupted Dictionary is a far worse way to find out than a slow one.

TRANSACTIONS ARE OFF BY DEFAULT, meaning autocommit is OFF: a connection
opens inside a transaction and nothing is durable until commit.  This is
Kiss's behaviour and it is the safe direction -- a program that forgets to
commit loses work it did not mean to keep, where autocommit loses the ability
to undo work it did not mean to do.
"
Class {
	#name : 'DbConnection',
	#superclass : 'Object',
	#instVars : [ 'handle', 'kind', 'lock', 'primaryKeyCache', 'columnCache',
	              'tableCache', 'commands', 'schemaGraph' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbConnection class >> open: connectionString [
	"Open a connection.  The string is ODBC's -- `DSN=name;UID=u;PWD=p', or
	 the driver-specific form for a driver that needs no DSN."

	^self new setHandle: (Odbc connect: connectionString)
]

{ #category : 'instance creation' }
DbConnection class >> open: connectionString do: aBlock [
	"Open, run the block with the connection, and close -- even if the block
	 raises.  The form to prefer: a connection left open by a method that
	 returned early is a server-side resource nothing in this image will
	 ever free."

	| connection |
	connection := self open: connectionString.
	^[aBlock value: connection] ensure: [connection close]
]

{ #category : 'private' }
DbConnection >> setHandle: aHandle [
	handle := aHandle.
	lock := Mutex new.
	primaryKeyCache := Dictionary new.
	columnCache := Dictionary new.
	tableCache := Dictionary new.
	commands := OrderedCollection new.
	Odbc connection: handle autoCommit: false
]

{ #category : 'accessing' }
DbConnection >> handle [
	^handle
]

{ #category : 'accessing' }
DbConnection >> isOpen [
	^handle notNil and: [Odbc isConnected: handle]
]

{ #category : 'accessing' }
DbConnection >> kind [
	"Which database this is, as a Symbol: #PostgreSQL, #MySQL, #SQLite,
	 #Oracle, #SQLServer, or #unknown.

	 Asked of the driver rather than declared by the caller, and cached: it
	 cannot change while the connection is open, and the SQL this package
	 generates differs by it in a handful of places -- LIMIT against
	 FETCH FIRST, and how a generated key is read back."

	| name |
	kind notNil ifTrue: [^kind].
	name := Odbc connection: handle info: 17.
	kind := name isNil
		ifTrue: [#unknown]
		ifFalse: [self kindFromName: name].
	^kind
]

{ #category : 'private' }
DbConnection >> kindFromName: aString [
	"Matched on a SUBSTRING, because the name a driver reports is not a
	 fixed string.  `PostgreSQL' arrives as itself from one driver and as
	 `PostgreSQL 16.2' from another, and MariaDB reports a MySQL-compatible
	 name that is not the word MySQL.  Equality here would answer #unknown
	 on a perfectly well identified database."

	| name |
	name := aString asUppercase.
	(self name: name includes: 'POSTGRE') ifTrue: [^#PostgreSQL].
	(self name: name includes: 'MYSQL') ifTrue: [^#MySQL].
	(self name: name includes: 'MARIA') ifTrue: [^#MySQL].
	(self name: name includes: 'SQLITE') ifTrue: [^#SQLite].
	(self name: name includes: 'ORACLE') ifTrue: [^#Oracle].
	(self name: name includes: 'SQL SERVER') ifTrue: [^#SQLServer].
	^#unknown
]

{ #category : 'private' }
DbConnection >> name: aString includes: aSubstring [
	^(aString indexOfSubCollection: aSubstring startingAt: 1) > 0
]

{ #category : 'transactions' }
DbConnection >> commit [
	Odbc commit: handle
]

{ #category : 'transactions' }
DbConnection >> rollback [
	Odbc rollback: handle
]

{ #category : 'transactions' }
DbConnection >> transact: aBlock [
	"Run the block and commit, or roll back if it raises.

	 The form to prefer over commit and rollback by hand.  An exception
	 through a half-finished transaction leaves the connection holding locks
	 the server will not release until somebody decides; this decides."

	| answer |
	answer := [aBlock value] ifCurtailed: [self rollback].
	self commit.
	^answer
]

{ #category : 'transactions' }
DbConnection >> autoCommit: aBoolean [
	Odbc connection: handle autoCommit: aBoolean
]

{ #category : 'transactions' }
DbConnection >> readOnly: aBoolean [
	Odbc connection: handle readOnly: aBoolean
]

{ #category : 'schema' }
DbConnection >> schema [
	^Odbc schemaOf: handle
]

{ #category : 'schema' }
DbConnection >> schema: aString [
	"Change the current schema, and forget everything cached about the old
	 one.  A cache keyed by table name means something different after this."

	Odbc connection: handle schema: aString.
	lock critical:
		[primaryKeyCache := Dictionary new.
		 columnCache := Dictionary new.
		 tableCache := Dictionary new.
		 schemaGraph := nil]
]

{ #category : 'commands' }
DbConnection >> newCommand [
	"A command of your own, for SQL you will run more than once."

	| command |
	command := DbCommand on: self.
	lock critical: [commands add: command].
	^command
]

{ #category : 'commands' }
DbConnection >> commandClosed: aCommand [
	lock critical: [commands remove: aCommand ifAbsent: [nil]]
]

{ #category : 'commands' }
DbConnection >> newQueryBuilder: tableName [
	^DbQueryBuilder on: self table: tableName
]

{ #category : 'executing' }
DbConnection >> execute: sqlString [
	^self execute: sqlString with: #()
]

{ #category : 'executing' }
DbConnection >> execute: sqlString with: anArrayOfValues [
	"Run SQL that answers no rows, and answer how many it changed.

	 A command made and closed around one statement.  Use newCommand when
	 the same SQL will run again: this prepares every time."

	| command |
	command := self newCommand.
	^[command execute: sqlString with: anArrayOfValues] ensure: [command close]
]

{ #category : 'executing' }
DbConnection >> executeImmediate: sqlString [
	"Run one statement with no prepare at all -- DDL, and anything the
	 driver will not let you prepare."

	^Odbc connection: handle executeDirect: sqlString
]

{ #category : 'querying' }
DbConnection >> query: sqlString [
	^self query: sqlString with: #()
]

{ #category : 'querying' }
DbConnection >> query: sqlString with: anArrayOfValues [
	"A cursor over the rows.  The cursor owns a command of its own and
	 closes it when the cursor closes."

	| command |
	command := self newCommand.
	^(command query: sqlString with: anArrayOfValues) ownCommand
]

{ #category : 'querying' }
DbConnection >> fetchOne: sqlString [
	^self fetchOne: sqlString with: #()
]

{ #category : 'querying' }
DbConnection >> fetchOne: sqlString with: anArrayOfValues [
	^(self query: sqlString with: anArrayOfValues) fetchOne
]

{ #category : 'querying' }
DbConnection >> fetchAll: sqlString [
	^self fetchAll: sqlString with: #()
]

{ #category : 'querying' }
DbConnection >> fetchAll: sqlString with: anArrayOfValues [
	^(self query: sqlString with: anArrayOfValues) fetchAll
]

{ #category : 'querying' }
DbConnection >> exists: sqlString with: anArrayOfValues [
	^(self fetchOne: sqlString with: anArrayOfValues) notNil
]

{ #category : 'records' }
DbConnection >> newRecord: tableName [
	"An empty record to fill in and add to a table."

	^DbRecord newOn: self table: tableName
]

{ #category : 'catalogue' }
DbConnection >> tableExists: tableName [
	| key answer |
	key := tableName asUppercase.
	lock critical: [answer := tableCache at: key ifAbsent: [nil]].
	answer notNil ifTrue: [^answer].
	answer := (DbCursor on: (Odbc connection: handle tablesIn: nil
			like: tableName types: nil)) fetchOne notNil.
	lock critical: [tableCache at: key put: answer].
	^answer
]

{ #category : 'catalogue' }
DbConnection >> tableNames [
	"Every ordinary table in the current schema, as an Array of Strings.

	 TABLE only, and not views, system tables or synonyms.  SQLTables
	 answers all of them and a schema graph built over a system catalogue is
	 hundreds of round trips producing edges nobody will ever join on."

	| names |
	names := OrderedCollection new.
	(DbCursor on: (Odbc connection: handle tablesIn: nil like: nil
			types: 'TABLE')) do:
		[:each | names add: (each at: 'TABLE_NAME')].
	^names asArray
]

{ #category : 'catalogue' }
DbConnection >> primaryKeysOf: tableName [
	"The primary key columns of a table, in key order, as an Array of Strings.

	 In KEY order and not in the order the driver answered them.  SQLPrimaryKeys
	 answers a KEY_SEQ column for exactly this reason, and a composite key
	 whose columns are used in the wrong order builds a WHERE clause that
	 matches the wrong row -- or, worse, the right one until the day two rows
	 share the first column."

	| key answer rows |
	key := tableName asUppercase.
	lock critical: [answer := primaryKeyCache at: key ifAbsent: [nil]].
	answer notNil ifTrue: [^answer].
	rows := (DbCursor on: (Odbc connection: handle primaryKeysIn: nil
			table: tableName)) fetchAll.
	answer := ((rows asSortedCollection:
			[:a :b | (a at: 'KEY_SEQ') <= (b at: 'KEY_SEQ')])
		collect: [:each | each at: 'COLUMN_NAME']) asArray.
	lock critical: [primaryKeyCache at: key put: answer].
	^answer
]

{ #category : 'catalogue' }
DbConnection >> columnsOf: tableName [
	"Every column of a table, as a Dictionary from upper-case name to
	 DbColumnInfo.  From the catalogue, so it works on a table nothing has
	 selected from yet."

	| key answer info |
	key := tableName asUppercase.
	lock critical: [answer := columnCache at: key ifAbsent: [nil]].
	answer notNil ifTrue: [^answer].
	answer := Dictionary new.
	(DbCursor on: (Odbc connection: handle columnsIn: nil table: tableName
			like: nil)) do:
		[:each |
		"SQLColumns names its own columns: COLUMN_NAME, DATA_TYPE,
		 COLUMN_SIZE, DECIMAL_DIGITS, NULLABLE -- the same five, and in the
		 same meanings, that describing a result set answers."
		info := DbColumnInfo fromArray: (Array
			with: (each at: 'COLUMN_NAME')
			with: (each at: 'DATA_TYPE')
			with: (each at: 'COLUMN_SIZE' ifAbsent: [0])
			with: ((each at: 'DECIMAL_DIGITS' ifAbsent: [0]) ifNil: [0])
			with: (each at: 'NULLABLE' ifAbsent: [1])).
		answer at: info name asUppercase put: info].
	lock critical: [columnCache at: key put: answer].
	^answer
]

{ #category : 'catalogue' }
DbConnection >> columnSizeOf: tableName column: columnName [
	| info |
	info := (self columnsOf: tableName) at: columnName asUppercase ifAbsent: [nil].
	^info isNil ifTrue: [nil] ifFalse: [info size]
]

{ #category : 'catalogue' }
DbConnection >> schemaGraph [
	"The foreign keys of the whole schema, as a graph.  Read once."

	schemaGraph isNil ifTrue:
		[schemaGraph := DbSchemaGraph fromConnection: self].
	^schemaGraph
]

{ #category : 'catalogue' }
DbConnection >> schemaGraph: aGraph [
	"Use a graph that was built elsewhere -- read from a file, or declared by
	 hand.  Reading a large schema's metadata is slow enough that an
	 application is entitled to do it once and keep the answer."

	schemaGraph := aGraph
]

{ #category : 'catalogue' }
DbConnection >> importedKeysOf: tableName [
	"A cursor over the foreign keys pointing out of one table."

	^DbCursor on: (Odbc connection: handle importedKeysIn: nil table: tableName)
]

{ #category : 'closing' }
DbConnection >> close [
	"Close every command this connection made, then the connection.

	 Closing the commands first rather than leaving them to the driver: a
	 driver handed a connection with live statements is entitled to refuse,
	 and the ones that do not refuse leak the handles instead."

	| open |
	handle isNil ifTrue: [^self].
	lock critical: [open := commands asArray. commands := OrderedCollection new].
	open do: [:each | each closeCursor. each closeStatement].
	Odbc disconnect: handle.
	handle := nil
]

{ #category : 'printing' }
DbConnection >> printOn: aStream [
	aStream nextPutAll: 'a DbConnection('.
	handle isNil
		ifTrue: [aStream nextPutAll: 'closed']
		ifFalse: [aStream nextPutAll: self kind].
	aStream nextPut: $)
]

"
One row, and the ability to write it back.

A record comes from a DbCursor, or empty from DbConnection>>newRecord: to be
filled in and inserted.  Values are read and written by column name, case
insensitively, because SQL identifiers are.

EVERY VALUE GOES AS A BOUND PARAMETER.  Not one value written through this
class is ever pasted into SQL text, so there is nothing for a quote in a
surname to break and nothing for an attacker to close early.  The only things
built into the text are table and column names, which SQL has no parameter
for, and before any statement is built every one of them is checked against
the catalogue: a column the table does not have is refused, and so is a table
name that is not a name.  A record's column names are whatever at:put: was
given, and a service that copies a request's field names into a record would
otherwise be handing its client the SQL -- see checkColumns:.

UPDATE WRITES ONLY WHAT CHANGED, and the Java this was ported from writes
every column.  Writing every column means two processes updating two
different fields of one row will each overwrite the other's -- last writer
wins on data it never touched, and neither of them ever sees a conflict.  In
a green-threaded Smalltalk that race needs two transactions to interleave; in
this one it needs two cores.  So the set of changed columns is remembered,
and the UPDATE names those.

THE KEY IS REMEMBERED AS IT WAS READ.  A record whose primary key is itself
changed must still be found by its OLD key, or the UPDATE matches nothing --
or, far worse, matches a different row that has since taken the new value.
Kiss keeps the same original-column map for exactly this reason.
"
Class {
	#name : 'DbRecord',
	#superclass : 'Object',
	#instVars : [ 'connection', 'tableName', 'values', 'originalValues',
	              'changed', 'cursor' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbRecord class >> on: aDictionary cursor: aCursor [
	^self new setValues: aDictionary cursor: aCursor
]

{ #category : 'instance creation' }
DbRecord class >> newOn: aConnection table: aTableName [
	"An empty record, to be filled in and inserted."

	^self new setConnection: aConnection table: aTableName
]

{ #category : 'private' }
DbRecord >> setValues: aDictionary cursor: aCursor [
	values := aDictionary.
	"A copy, because the whole point is to remember what the row was after
	 the caller has changed what it is."
	originalValues := aDictionary copy.
	changed := Set new.
	cursor := aCursor.
	tableName := aCursor tableName.
	connection := aCursor connection
]

{ #category : 'private' }
DbRecord >> setConnection: aConnection table: aTableName [
	connection := aConnection.
	tableName := aTableName.
	values := Dictionary new.
	originalValues := Dictionary new.
	changed := Set new
]

{ #category : 'accessing' }
DbRecord >> at: columnName [
	"The value of a column.  Raises if there is no such column, because a
	 misspelled name answering nil is indistinguishable from a NULL, and the
	 report that comes out is wrong rather than missing."

	^values at: columnName asUppercase ifAbsent:
		[DbError signal: 'no column named ', columnName printString,
			' in this row']
]

{ #category : 'accessing' }
DbRecord >> at: columnName ifAbsent: aBlock [
	^values at: columnName asUppercase ifAbsent: aBlock
]

{ #category : 'accessing' }
DbRecord >> at: columnName put: aValue [
	| key |
	key := columnName asUppercase.
	values at: key put: aValue.
	changed add: key.
	^aValue
]

{ #category : 'accessing' }
DbRecord >> includesColumn: columnName [
	^values includesKey: columnName asUppercase
]

{ #category : 'accessing' }
DbRecord >> columns [
	"The values, by upper-case column name.  The live Dictionary, which is
	 what a cursor hands in; do not hold on to it past the record."

	^values
]

{ #category : 'accessing' }
DbRecord >> columnNames [
	^values keys
]

{ #category : 'accessing' }
DbRecord >> tableName [
	^tableName
]

{ #category : 'accessing' }
DbRecord >> tableName: aString [
	tableName := aString
]

{ #category : 'accessing' }
DbRecord >> connection [
	^connection
]

{ #category : 'accessing' }
DbRecord >> isChanged [
	^changed notEmpty
]

{ #category : 'typed accessing' }
DbRecord >> integerAt: columnName [
	"The column as an Integer, however the database spelled it.

	 A DECIMAL column holding a whole number arrives as a Fraction that has
	 already reduced to an Integer, and a driver that answered a Float for a
	 count is truncated here rather than left to surprise an index."

	| value |
	value := self at: columnName.
	value isNil ifTrue: [^nil].
	value isString ifTrue: [^(Odbc numberFromDigits: value) truncated].
	^value truncated
]

{ #category : 'typed accessing' }
DbRecord >> numberAt: columnName [
	| value |
	value := self at: columnName.
	value isNil ifTrue: [^nil].
	value isString ifTrue: [^Odbc numberFromDigits: value].
	^value
]

{ #category : 'typed accessing' }
DbRecord >> floatAt: columnName [
	| value |
	value := self numberAt: columnName.
	^value isNil ifTrue: [nil] ifFalse: [value asFloat]
]

{ #category : 'typed accessing' }
DbRecord >> stringAt: columnName [
	| value |
	value := self at: columnName.
	value isNil ifTrue: [^nil].
	value isString ifTrue: [^value].
	^value printString
]

{ #category : 'typed accessing' }
DbRecord >> booleanAt: columnName [
	"true, false, or nil.

	 A database that has no boolean type stores one as a number or a letter,
	 so 0, 'N', 'F' and 'no' are false and anything else that is not null is
	 true.  Guessing is unavoidable here -- what is not unavoidable is
	 guessing differently in two places, so every caller comes through this."

	| value |
	value := self at: columnName.
	value isNil ifTrue: [^nil].
	(value == true or: [value == false]) ifTrue: [^value].
	value isString ifTrue:
		[value isEmpty ifTrue: [^false].
		 ^('NF0' includes: (value at: 1) asUppercase) not].
	^value ~= 0
]

{ #category : 'typed accessing' }
DbRecord >> dateAt: columnName [
	| value |
	value := self at: columnName.
	(value isKindOf: DbTimestamp) ifTrue: [^value asDate].
	^value
]

{ #category : 'typed accessing' }
DbRecord >> timeAt: columnName [
	| value |
	value := self at: columnName.
	(value isKindOf: DbTimestamp) ifTrue: [^value asTime].
	^value
]

{ #category : 'typed accessing' }
DbRecord >> timestampAt: columnName [
	^self at: columnName
]

{ #category : 'typed accessing' }
DbRecord >> bytesAt: columnName [
	| value |
	value := self at: columnName.
	value isNil ifTrue: [^nil].
	value isString ifTrue: [^value asByteArray].
	^value
]

{ #category : 'typed accessing' }
DbRecord >> dateIntegerAt: columnName [
	"A date as yyyymmdd, which is how a great deal of application code
	 stores and compares one.  Kiss calls this getDateAsInt."

	| value |
	value := self at: columnName.
	value isNil ifTrue: [^nil].
	(value isKindOf: DbTimestamp) ifTrue: [^value asInteger].
	^value year * 10000 + (value monthIndex * 100) + value dayOfMonth
]

{ #category : 'copying' }
DbRecord >> copyCorrespondingFrom: aRecord [
	"Take every column of aRecord that this record also has.

	 The join between two tables that share column names, which is what it
	 is for: copying a row from a staging table into its real one without
	 naming forty columns."

	aRecord columns keysAndValuesDo:
		[:key :value |
		(values includesKey: key) ifTrue: [self at: key put: value]]
]

{ #category : 'writing' }
DbRecord >> insert [
	"INSERT this record.  Answers how many rows were added, which is one."

	| names statement command |
	self mustHaveTable.
	names := values keys asSortedCollection asArray.
	names isEmpty ifTrue: [^DbError signal: 'nothing to insert'].
	self checkColumns: names.
	statement := WriteStream on: (String new: 128).
	statement nextPutAll: 'INSERT INTO '; nextPutAll: tableName;
		nextPutAll: ' ('.
	names doWithIndex:
		[:each :i | i > 1 ifTrue: [statement nextPutAll: ', '].
			statement nextPutAll: each].
	statement nextPutAll: ') VALUES ('.
	1 to: names size do:
		[:i | i > 1 ifTrue: [statement nextPutAll: ', '].
			statement nextPut: $?].
	statement nextPut: $).
	command := connection newCommand.
	^[command execute: statement contents
		with: (names collect: [:each | values at: each])]
			ensure: [command close]
]

{ #category : 'writing' }
DbRecord >> insertReturningKey: keyColumnName [
	"INSERT, and answer the key the database generated.

	 ODBC HAS NO getGeneratedKeys.  This is the one place in the whole port
	 where JDBC offers something ODBC does not, and every database solves it
	 differently -- so it is solved here, once, by asking the connection what
	 kind of database it is.  Kiss has the same switch, in the same method,
	 for the same reason."

	| kind |
	self mustHaveTable.
	kind := connection kind.
	kind == #PostgreSQL ifTrue: [^self insertReturning: keyColumnName].
	self insert.
	kind == #SQLite ifTrue:
		[^(connection fetchOne: 'SELECT last_insert_rowid() AS k') at: 'K'].
	kind == #MySQL ifTrue:
		[^(connection fetchOne: 'SELECT LAST_INSERT_ID() AS k') at: 'K'].
	kind == #SQLServer ifTrue:
		[^(connection fetchOne: 'SELECT SCOPE_IDENTITY() AS k') at: 'K'].
	^DbError signal: 'this database has no generated-key protocol here: ',
		kind
]

{ #category : 'private' }
DbRecord >> insertReturning: keyColumnName [
	"PostgreSQL's RETURNING, which answers the new row in the same round
	 trip -- so there is no window in which another connection's insert
	 could be the one a following SELECT finds."

	| names statement |
	names := values keys asSortedCollection asArray.
	names isEmpty ifTrue: [^DbError signal: 'nothing to insert'].
	self checkColumns: (names copyWith: keyColumnName).
	statement := WriteStream on: (String new: 128).
	statement nextPutAll: 'INSERT INTO '; nextPutAll: tableName;
		nextPutAll: ' ('.
	names doWithIndex:
		[:each :i | i > 1 ifTrue: [statement nextPutAll: ', '].
			statement nextPutAll: each].
	statement nextPutAll: ') VALUES ('.
	1 to: names size do:
		[:i | i > 1 ifTrue: [statement nextPutAll: ', '].
			statement nextPut: $?].
	statement nextPutAll: ') RETURNING '; nextPutAll: keyColumnName.
	^(connection fetchOne: statement contents
		with: (names collect: [:each | values at: each]))
			at: keyColumnName
]

{ #category : 'writing' }
DbRecord >> update [
	"UPDATE the columns that changed, matching on the key as it was READ.

	 Answers how many rows were changed.  Zero means the row is not there
	 any more -- somebody deleted it, or changed its key -- and that is
	 worth checking rather than assuming."

	| keys statement arguments command |
	self mustHaveTable.
	changed isEmpty ifTrue: [^0].
	self checkColumns: changed.
	keys := self keyColumns.
	statement := WriteStream on: (String new: 128).
	arguments := OrderedCollection new.
	statement nextPutAll: 'UPDATE '; nextPutAll: tableName; nextPutAll: ' SET '.
	changed asSortedCollection asArray doWithIndex:
		[:each :i |
		i > 1 ifTrue: [statement nextPutAll: ', '].
		statement nextPutAll: each; nextPutAll: ' = ?'.
		arguments add: (values at: each)].
	statement nextPutAll: ' WHERE '.
	keys doWithIndex:
		[:each :i |
		i > 1 ifTrue: [statement nextPutAll: ' AND '].
		statement nextPutAll: each; nextPutAll: ' = ?'.
		arguments add: (originalValues at: each asUppercase)].
	command := connection newCommand.
	^[| count |
	  count := command execute: statement contents with: arguments asArray.
	  "The row as it now stands is the row a further update should match."
	  changed do: [:each | originalValues at: each put: (values at: each)].
	  changed := Set new.
	  count] ensure: [command close]
]

{ #category : 'writing' }
DbRecord >> delete [
	"DELETE this row, matching on the key as it was read."

	| keys statement arguments command |
	self mustHaveTable.
	keys := self keyColumns.
	statement := WriteStream on: (String new: 128).
	arguments := OrderedCollection new.
	statement nextPutAll: 'DELETE FROM '; nextPutAll: tableName;
		nextPutAll: ' WHERE '.
	keys doWithIndex:
		[:each :i |
		i > 1 ifTrue: [statement nextPutAll: ' AND '].
		statement nextPutAll: each; nextPutAll: ' = ?'.
		arguments add: (originalValues at: each asUppercase)].
	command := connection newCommand.
	^[command execute: statement contents with: arguments asArray]
		ensure: [command close]
]

{ #category : 'private' }
DbRecord >> keyColumns [
	"The primary key of this record's table, refusing if there is not one.

	 A table with no primary key can be selected from and cannot be written
	 back to by this class, because there is no way to name one row.  Saying
	 so is the whole of the check: an UPDATE built without a key would match
	 every row in the table."

	| keys |
	keys := connection primaryKeysOf: tableName.
	keys isEmpty ifTrue:
		[^DbError signal: 'table ', tableName,
			' has no primary key, so a row of it cannot be written back'].
	keys do:
		[:each |
		(originalValues includesKey: each asUppercase) ifFalse:
			[^DbError signal: 'this row does not carry ', each,
				', which is part of ', tableName, '''s primary key']].
	^keys
]

{ #category : 'private' }
DbRecord >> mustHaveTable [
	tableName isNil ifTrue:
		[^DbError signal: 'this row did not come from a single table, so ',
			'there is nowhere to write it back to -- select from one table, ',
			'or set the table name'].
	"Written into the statement as it is, like the column names, and as
	 much the caller's as they are -- newRecord: and tableName: take any
	 String (Bugs07 FILES-5).  A name is letters, digits, `_', `$' and `#',
	 with dots between a schema and a table; nothing in that set can end
	 a name early in any SQL.  Characters past ASCII are letters to the
	 databases that allow them in a name and are let through."
	(tableName isEmpty or: [tableName anySatisfy: [:c |
			(c isLetter or: [c isDigit or: ['_$#.' includes: c]]) not
				and: [c asInteger < 128]]])
		ifTrue: [^DbError signal: 'not a table name: ', tableName printString]
]

{ #category : 'private' }
DbRecord >> checkColumns: names [
	"Refuse, before any statement is built, a name that is not a column of
	 this record's table (Bugs07 FILES-5).

	 SQL has no parameter for an identifier, so column names are written
	 into the statement's text -- and a record's names are whatever at:put:
	 was given.  A generic `save these fields' service, copying the names
	 of a request's fields into a record, handed its client the SQL: a name
	 of `name, admin) select ''mallory'', 1 union select ?, 0 --' turned an
	 INSERT into an INSERT ... SELECT that added an administrator, and
	 `name = (select 1), admin' set a column the client was never offered.
	 So every name written must be one the catalogue gave, which the
	 connection has cached.

	 A name the cache does not have is looked up again before it is
	 refused: the cache is filled once per table, and a column added by an
	 ALTER TABLE on this connection since is a column all the same."
	| columns |
	columns := connection columnsOf: tableName.
	(names allSatisfy: [:each | columns includesKey: each asUppercase]) ifTrue: [^self].
	connection forgetColumnsOf: tableName.
	columns := connection columnsOf: tableName.
	columns isEmpty ifTrue: [^DbError signal: 'there is no table named ', tableName printString].
	names do: [:each |
		(columns includesKey: each asUppercase) ifFalse: [
			^DbError signal: 'table ', tableName, ' has no column named ', each printString]]
]

{ #category : 'printing' }
DbRecord >> printOn: aStream [
	| names |
	aStream nextPutAll: 'a DbRecord('.
	tableName notNil ifTrue: [aStream nextPutAll: tableName; nextPutAll: ': '].
	names := values keys asSortedCollection asArray.
	names doWithIndex:
		[:each :i |
		i > 1 ifTrue: [aStream nextPutAll: ', '].
		aStream nextPutAll: each; nextPut: $=; print: (values at: each)].
	aStream nextPut: $)
]

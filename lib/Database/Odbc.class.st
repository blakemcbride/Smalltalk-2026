"
The boundary between Smalltalk and ODBC, and the only class in this package
that knows a primitive exists.

Everything above this -- DbConnection, DbCommand, DbCursor, DbRecord,
DbQueryBuilder, DbSchemaGraph -- is ordinary Smalltalk sending ordinary
messages here.  That is not tidiness for its own sake.  It means the whole
package can be read without knowing ODBC, it means a second driver could be
put underneath without touching anything above, and it means there is exactly
one file to look in when a database does something a database should not.

ONE PRIMITIVE, NOT THIRTY.  Primitive 129 takes a command number and three
operands, in the same way primitive 130 is the whole file system rather than
one number per file operation.  A subsystem that will grow should grow inside
its own primitive number: numbers are a space of 255 that the Blue Book,
Squeak, Pharo and this system are all already spending, and one given away is
never given back.

WHAT THE PRIMITIVE ANSWERS, AND WHAT THAT MEANS.  Two different failures
reach here by two different routes, deliberately:

    The primitive FAILS -- the Smalltalk fallback code runs -- when the
    ARGUMENTS were wrong.  A command number that is not an integer, a string
    where an integer belongs.  That is a bug in this file, and it raises.

    The primitive SUCCEEDS and answers nil when the DATABASE said no.  A
    connection string nothing recognises, SQL that will not parse, a server
    that has gone away.  That is not a bug in this file, it is news, and it
    comes with a message from the driver that lastError answers.

Getting those the same way round is what makes a diagnostic useful: the first
says a method here is wrong, the second says what the database told us.
"
Class {
	#name : 'Odbc',
	#superclass : 'Object',
	#category : 'Database'
}

{ #category : 'primitives' }
Odbc class >> primCommand: commandInteger with: a with: b with: c [
	"The one primitive.  See the class comment.

	 Failing here means the arguments were not what the command wanted,
	 which is a fault in this file rather than a fault in the database, so
	 it raises rather than answering nil -- which is what the database
	 saying no looks like, and the two must not be confused."

	<primitive: 129>
	^DbError signal: 'the database primitive was called wrongly: command ',
		commandInteger printString
]

{ #category : 'availability' }
Odbc class >> isAvailable [
	"Whether this build has a database at all.

	 A build made without ODBC has every method of this package present and
	 answering, and this one answering false.  Compiling the package out
	 instead would turn `no database on this machine' into a
	 doesNotUnderstand on a class that does not exist, which says nothing
	 about databases to whoever reads it."

	^self primCommand: 0 with: nil with: nil with: nil
]

{ #category : 'availability' }
Odbc class >> lastError [
	"What the driver said about the most recent failure, on THIS process.

	 Per process and not per connection, because the call that fails most
	 often is the one that was going to produce the connection."

	^self primCommand: 1 with: nil with: nil with: nil
]

{ #category : 'private' }
Odbc class >> check: answer [
	"Answer what the primitive answered, or raise with the driver's own words.

	 Every call that can be refused by the database goes through here, so
	 that the message a user sees is the message the driver wrote and not a
	 paraphrase of it invented in this image."

	answer isNil ifTrue: [^DbError signal: self lastError].
	^answer
]

{ #category : 'connections' }
Odbc class >> connect: connectionString [
	"Answer a connection handle.  Raises if the database refuses."

	^self check: (self primCommand: 2 with: connectionString with: nil with: nil)
]

{ #category : 'connections' }
Odbc class >> disconnect: handle [
	^self primCommand: 3 with: handle with: nil with: nil
]

{ #category : 'connections' }
Odbc class >> isConnected: handle [
	^self primCommand: 4 with: handle with: nil with: nil
]

{ #category : 'connections' }
Odbc class >> connection: handle autoCommit: aBoolean [
	^self primCommand: 5 with: handle with: aBoolean with: nil
]

{ #category : 'connections' }
Odbc class >> connection: handle readOnly: aBoolean [
	^self primCommand: 6 with: handle with: aBoolean with: nil
]

{ #category : 'connections' }
Odbc class >> commit: handle [
	^self primCommand: 7 with: handle with: nil with: nil
]

{ #category : 'connections' }
Odbc class >> rollback: handle [
	^self primCommand: 8 with: handle with: nil with: nil
]

{ #category : 'connections' }
Odbc class >> connection: handle info: infoInteger [
	"SQLGetInfo, for the string-valued types.  17 is the DBMS name, which is
	 what JDBC calls getDatabaseProductName and is how the type of database
	 is discovered rather than declared."

	^self primCommand: 9 with: handle with: infoInteger with: nil
]

{ #category : 'connections' }
Odbc class >> connection: handle schema: aString [
	^self primCommand: 10 with: handle with: aString with: nil
]

{ #category : 'connections' }
Odbc class >> schemaOf: handle [
	^self primCommand: 11 with: handle with: nil with: nil
]

{ #category : 'statements' }
Odbc class >> connection: handle prepare: sqlString [
	^self check: (self primCommand: 12 with: handle with: sqlString with: nil)
]

{ #category : 'statements' }
Odbc class >> closeStatement: statement [
	^self primCommand: 13 with: statement with: nil with: nil
]

{ #category : 'statements' }
Odbc class >> clearParameters: statement [
	^self primCommand: 14 with: statement with: nil with: nil
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index to: aValue [
	"Bind by the value's own class: nil, Boolean, Integer, Float, String,
	 Symbol and ByteArray all know what they are.

	 Dates and times are NOT bound here and have their own messages.  An
	 Array of three integers is a date to a reader and an array of three
	 integers to this method, and a guess that is usually right is the worst
	 kind -- it would fail only on the row that really did hold three
	 numbers."

	^self primCommand: 15 with: statement with: index with: aValue
]

{ #category : 'binding' }
Odbc class >> statement: statement bindNull: index type: sqlTypeOrNil [
	^self primCommand: 16 with: statement with: index with: sqlTypeOrNil
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index toDate: aDate [
	^self primCommand: 28 with: statement with: index
		with: (Array with: aDate year with: aDate monthIndex with: aDate dayOfMonth)
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index toTime: aTime [
	^self primCommand: 29 with: statement with: index
		with: (Array with: aTime hours with: aTime minutes with: aTime seconds)
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index toTimestamp: aDbTimestamp [
	^self primCommand: 30 with: statement with: index
		with: aDbTimestamp asArray
]

{ #category : 'executing' }
Odbc class >> execute: statement [
	^self check: (self primCommand: 17 with: statement with: nil with: nil)
]

{ #category : 'executing' }
Odbc class >> connection: handle executeDirect: sqlString [
	"Run one statement with no prepare, and answer the rows it affected.

	 This is the DDL path and Connection>>executeImmediate.  Preparing would
	 be a second round trip to say something that will be said once."

	^self check: (self primCommand: 18 with: handle with: sqlString with: nil)
]

{ #category : 'executing' }
Odbc class >> fetch: statement [
	"true a row was read, false the result set ended.

	 The primitive answers nil for a failure, and check: turns that into a
	 raise -- because a caller shown false for both would read a dropped
	 connection as an empty table, and produce a report that runs and is
	 wrong."

	^self check: (self primCommand: 19 with: statement with: nil with: nil)
]

{ #category : 'executing' }
Odbc class >> rowCount: statement [
	^self primCommand: 20 with: statement with: nil with: nil
]

{ #category : 'result sets' }
Odbc class >> columnCount: statement [
	^self check: (self primCommand: 21 with: statement with: nil with: nil)
]

{ #category : 'result sets' }
Odbc class >> statement: statement describeColumn: index [
	"An Array: name, SQL type, size, decimal digits, nullable."

	^self check: (self primCommand: 22 with: statement with: index with: nil)
]

{ #category : 'result sets' }
Odbc class >> statement: statement value: index [
	"The raw value of one column of the current row.

	 Raw because DECIMAL arrives as its digits in a String and dates arrive
	 as Arrays of integers; DbCursor knows the column's type and makes the
	 Smalltalk object.  See DbCursor>>valueAt:."

	^self primCommand: 23 with: statement with: index with: nil
]

{ #category : 'catalogue' }
Odbc class >> connection: handle tablesIn: schemaOrNil like: patternOrNil types: typesOrNil [
	^self check: (self primCommand: 24 with: handle with: schemaOrNil
		with: (Array with: patternOrNil with: typesOrNil))
]

{ #category : 'catalogue' }
Odbc class >> connection: handle columnsIn: schemaOrNil table: tableOrNil like: patternOrNil [
	^self check: (self primCommand: 25 with: handle with: schemaOrNil
		with: (Array with: tableOrNil with: patternOrNil))
]

{ #category : 'catalogue' }
Odbc class >> connection: handle primaryKeysIn: schemaOrNil table: aString [
	^self check: (self primCommand: 26 with: handle with: schemaOrNil
		with: (Array with: aString))
]

{ #category : 'catalogue' }
Odbc class >> connection: handle importedKeysIn: schemaOrNil table: aString [
	"The foreign keys pointing OUT of this table, which is what a join graph
	 is built from."

	^self check: (self primCommand: 27 with: handle with: schemaOrNil
		with: (Array with: aString))
]

{ #category : 'converting' }
Odbc class >> numberFromDigits: aString [
	"An EXACT number from the characters a database printed.

	 This exists because String>>asNumber is not exact and cannot be made so
	 without changing 1983 code that has every right to its own behaviour:
	 Number class>>readFrom: reads the fractional part, converts it with
	 asFloat and divides, so '0.1' asNumber is a Float and is already not
	 one tenth.  A money column read that way is wrong in exactly the cases
	 anybody cares about, and wrong quietly.

	 So the digits are read as an Integer and scaled by a power of ten,
	 which answers a Fraction -- exact, and the one respect in which this
	 port is better than the Java it comes from, which hands the same text
	 to BigDecimal and then has to remember never to ask it for a double."

	| stream negative whole fraction scale character |
	stream := ReadStream on: aString.
	negative := false.
	[stream atEnd not and: [stream peek = $ ]] whileTrue: [stream next].
	stream atEnd ifTrue: [^0].
	stream peek = $- ifTrue: [negative := true. stream next].
	stream peek = $+ ifTrue: [stream next].
	whole := 0.
	[stream atEnd not and: [stream peek isDigit]] whileTrue:
		[whole := whole * 10 + stream next digitValue].
	fraction := 0.
	scale := 1.
	(stream atEnd not and: [stream peek = $.]) ifTrue:
		[stream next.
		 [stream atEnd not and: [stream peek isDigit]] whileTrue:
			[character := stream next.
			 fraction := fraction * 10 + character digitValue.
			 scale := scale * 10]].
	whole := whole + (fraction / scale).
	^negative ifTrue: [whole negated] ifFalse: [whole]
]

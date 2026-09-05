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

AND A THIRD ANSWER, which is the one Bugs3 B57 found nobody was reading.  The
commands that do not produce a value -- every bind, execute, commit -- answer
TRUE or FALSE, not a value or nil, so check: (which raises on nil) passes
their failure straight through.  A bind that the driver refused therefore
reported success, the statement ran with the parameter unbound, and the row
was not there afterwards: a data loss with nothing anywhere saying so.  Those
commands go through checkStatus: instead, which raises on anything but true.
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
Odbc class >> checkStatus: answer saying: aString [
	"Answer true, or raise with the driver's own words and then ours.

	 The companion to check:, for the commands that answer a Boolean rather
	 than a value.  See the class comment: `self check:' on one of those is
	 a no-op, because false is not nil, and Bugs3 B57 is what that costs --
	 a bind refused by the driver, an INSERT that then ran without the
	 parameter, and a caller told the row went in.

	 The driver's message comes first when there is one; aString says which
	 call it was, since a bind failure names a parameter the driver has
	 never heard of."

	| said |
	answer == true ifTrue: [^answer].
	said := [self lastError] on: Error do: [:e | nil].
	^DbError signal: ((said isNil or: [said isEmpty])
		ifTrue: [aString]
		ifFalse: [aString, ': ', said])
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
Odbc class >> maxParameterInteger [
	"The largest Integer a parameter holds: 2^63 - 1.

	 SQL_C_SBIGINT is the widest integer type ODBC has, and ST_odbc_bind_int
	 takes an int64_t.  This is a property of the interface and not of any
	 one driver, which is why it is written here as a number rather than
	 asked of a connection."

	^9223372036854775807
]

{ #category : 'binding' }
Odbc class >> minParameterInteger [
	"The smallest Integer a parameter holds: -2^63."

	^0 - 9223372036854775808
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index to: aValue [
	"Bind by the value's own class: nil, Boolean, Integer, Float, String,
	 Symbol and ByteArray all know what they are.

	 Dates and times are NOT bound here and have their own messages.  An
	 Array of three integers is a date to a reader and an array of three
	 integers to this method, and a guess that is usually right is the worst
	 kind -- it would fail only on the row that really did hold three
	 numbers.

	 AN INTEGER WIDER THAN 64 BITS IS REFUSED HERE, before the driver is
	 asked.  Bugs3 B57: binding 2^63 answered success and inserted nothing,
	 because the primitive could not fit the value into the int64_t a
	 parameter is, answered false, and nothing looked at the answer.
	 Refusing is the honest end of that: there is no wider parameter to bind
	 it to, and a caller with a DECIMAL column wider than 64 bits can send
	 the number as a String or a Fraction, both of which go to the driver as
	 digits and arrive exact.  It is checked HERE rather than in the
	 primitive so that the message can name the value; the primitive's only
	 vocabulary is false.

	 A LARGE NEGATIVE INTEGER GOES AS ITS DIGITS, for a different reason.
	 The primitive reads a LargePositiveInteger's bytes but has no branch
	 for a LargeNegativeInteger -- the class is not one of the guaranteed
	 object pointers, and prim.c says why it will not look it up -- so such
	 a value fell through to the branch that binds any other byte object as
	 characters, and bound the raw bytes of its magnitude.  SQLite stored a
	 NULL and reported success.  On this build every integer below -2^62 is
	 a LargeNegativeInteger, so that is the whole bottom of the range a
	 BIGINT column has.  printString is what the primitive's own comment
	 says the caller does, and it is what DbCommand already does for a
	 Fraction: digits, which every database converts from exactly.

	 A BYTEARRAY GOES BY ITS OWN COMMAND, as the dates do, and for the same
	 reason: once inside the primitive a ByteArray and a String are both
	 byte objects and ByteArray is not one of the guaranteed object
	 pointers, so C cannot tell them apart.  Bound as characters it also
	 went to the driver at strlen of its bytes -- Bugs4 FILES-1:
	 #(1 2 0 3 4 255) asByteArray reached a SQLite BLOB column as two
	 bytes, and one beginning with zero reached it empty.  Command 31 binds
	 SQL_C_BINARY at the object's real length."

	| bound |
	bound := aValue.
	(aValue isKindOf: ByteArray) ifTrue: [
		^self checkStatus: (self primCommand: 31 with: statement with: index with: aValue)
			saying: 'parameter ', index printString, ' would not bind as ', aValue class name].
	(aValue isKindOf: Integer) ifTrue: [
		(aValue > self maxParameterInteger or: [aValue < self minParameterInteger])
			ifTrue: [^DbError signal: 'parameter ', index printString,
				' cannot hold ', aValue printString,
				': an ODBC parameter is 64 bits.  Send a wider number as a String or a Fraction'].
		(aValue isKindOf: LargeNegativeInteger) ifTrue: [bound := aValue printString]].
	^self checkStatus: (self primCommand: 15 with: statement with: index with: bound)
		saying: 'parameter ', index printString, ' would not bind as ', aValue class name
]

{ #category : 'binding' }
Odbc class >> statement: statement bindNull: index type: sqlTypeOrNil [
	^self checkStatus: (self primCommand: 16 with: statement with: index with: sqlTypeOrNil)
		saying: 'parameter ', index printString, ' would not bind as null'
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index toDate: aDate [
	^self checkStatus: (self primCommand: 28 with: statement with: index
			with: (Array with: aDate year with: aDate monthIndex with: aDate dayOfMonth))
		saying: 'parameter ', index printString, ' would not bind as a Date'
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index toTime: aTime [
	^self checkStatus: (self primCommand: 29 with: statement with: index
			with: (Array with: aTime hours with: aTime minutes with: aTime seconds))
		saying: 'parameter ', index printString, ' would not bind as a Time'
]

{ #category : 'binding' }
Odbc class >> statement: statement bind: index toTimestamp: aDbTimestamp [
	^self checkStatus: (self primCommand: 30 with: statement with: index
			with: aDbTimestamp asArray)
		saying: 'parameter ', index printString, ' would not bind as a DbTimestamp'
]

{ #category : 'executing' }
Odbc class >> execute: statement [
	"Run the prepared statement.  Raises with the driver's words if it will
	 not run.

	 checkStatus: and not check:, for the reason the class comment gives:
	 this command answers false for a failure, so `self check:' -- which is
	 what stood here -- raised on nothing.  An INSERT the database refused
	 answered false and DbCommand>>execute: then asked for the row count of
	 a statement that had not executed.  That is the second half of Bugs3
	 B57: the bind was the silent failure, and this was the silence after
	 it."

	^self checkStatus: (self primCommand: 17 with: statement with: nil with: nil)
		saying: 'the statement would not execute'
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

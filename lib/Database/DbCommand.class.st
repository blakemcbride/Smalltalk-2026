"
One prepared statement, and the cursors made from it.

A DbCommand is what you get from DbConnection>>newCommand, and it exists so
that SQL sent many times is PREPARED once.  Preparing is a round trip and a
parse on the server; a loop that inserts ten thousand rows through a command
pays for that once rather than ten thousand times, and the difference is not
subtle.

ONE CURSOR AT A TIME, because ODBC gives one result set per statement handle.
Opening a second closes the first, and the class says so rather than letting
the driver produce a diagnostic about a statement in the wrong state.  A
caller who wants two open at once wants two commands, which is what
newCommand is for.

THE SQL IS REMEMBERED so that running the same text again reuses the plan and
running different text re-prepares.  That is Kiss's `prevsql' check and it is
what makes a command usable as a general-purpose executor without the caller
having to know whether the statement it is holding matches what it is about
to ask.
"
Class {
	#name : 'DbCommand',
	#superclass : 'Object',
	#instVars : [ 'connection', 'statement', 'sql', 'cursor' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbCommand class >> on: aConnection [
	^self new setConnection: aConnection
]

{ #category : 'private' }
DbCommand >> setConnection: aConnection [
	connection := aConnection
]

{ #category : 'accessing' }
DbCommand >> connection [
	^connection
]

{ #category : 'accessing' }
DbCommand >> sql [
	^sql
]

{ #category : 'private' }
DbCommand >> prepare: sqlString [
	"Answer a statement for this SQL, preparing only if it is new.

	 Comparing the text rather than trusting the caller: a command is often
	 held by code that runs several different statements through it, and
	 reusing a plan for the wrong SQL is not an error the database can
	 catch -- the parameters would simply go to the wrong places."

	self closeCursor.
	(statement notNil and: [sql = sqlString]) ifTrue:
		[Odbc clearParameters: statement.
		 ^statement].
	self closeStatement.
	sql := sqlString.
	statement := Odbc connection: connection handle prepare: sqlString.
	^statement
]

{ #category : 'private' }
DbCommand >> bindAll: anArrayOfValues [
	"Bind the arguments, one-relative, in the order the ?s appear.

	 Dates and times are told apart HERE, by their class, rather than in the
	 primitive.  The primitive sees an Array of three integers and cannot
	 know whether it is a date; a Date knows perfectly well that it is one."

	anArrayOfValues keysAndValuesDo:
		[:i :value | self bind: i to: value].
	^self
]

{ #category : 'private' }
DbCommand >> bind: index to: value [
	(value isKindOf: Date) ifTrue:
		[^Odbc statement: statement bind: index toDate: value].
	(value isKindOf: Time) ifTrue:
		[^Odbc statement: statement bind: index toTime: value].
	(value isKindOf: DbTimestamp) ifTrue:
		[^Odbc statement: statement bind: index toTimestamp: value].
	"A Fraction is an exact number the database has no exact form for in a
	 parameter, so it goes as the digits it prints -- which every database
	 will read back into its own DECIMAL without going through a binary
	 float on the way."
	(value isKindOf: Fraction) ifTrue:
		[^Odbc statement: statement bind: index to: (self digitsOf: value)].
	^Odbc statement: statement bind: index to: value
]

{ #category : 'private' }
DbCommand >> digitsOf: aFraction [
	"An exact Fraction as the characters a DECIMAL column will accept.

	 Twelve places, then trailing zeros trimmed.  A Fraction whose
	 denominator is not a power of ten -- one third -- cannot be written
	 exactly in decimal at any length, so a limit has to exist; twelve is
	 past every scale a database column actually declares."

	| places scaled digits point |
	places := 12.
	scaled := (aFraction * (10 raisedTo: places)) rounded.
	digits := scaled abs printString.
	[digits size <= places] whileTrue: [digits := '0', digits].
	point := digits size - places.
	digits := (digits copyFrom: 1 to: point), '.',
		(digits copyFrom: point + 1 to: digits size).
	[digits last = $0] whileTrue: [digits := digits copyFrom: 1 to: digits size - 1].
	digits last = $. ifTrue: [digits := digits copyFrom: 1 to: digits size - 1].
	^scaled < 0 ifTrue: ['-', digits] ifFalse: [digits]
]

{ #category : 'executing' }
DbCommand >> execute: sqlString [
	"Run SQL that answers no rows, and answer how many it changed."

	^self execute: sqlString with: #()
]

{ #category : 'executing' }
DbCommand >> execute: sqlString with: anArrayOfValues [
	self prepare: sqlString.
	self bindAll: anArrayOfValues.
	Odbc execute: statement.
	^Odbc rowCount: statement
]

{ #category : 'querying' }
DbCommand >> query: sqlString [
	^self query: sqlString with: #()
]

{ #category : 'querying' }
DbCommand >> query: sqlString with: anArrayOfValues [
	"Run SQL that answers rows, and answer a cursor over them."

	self prepare: sqlString.
	self bindAll: anArrayOfValues.
	Odbc execute: statement.
	cursor := DbCursor on: statement command: self.
	cursor tableName: (self tableNameIn: sqlString).
	^cursor
]

{ #category : 'querying' }
DbCommand >> query: sqlString max: maxRows with: anArrayOfValues [
	| answer |
	answer := self query: sqlString with: anArrayOfValues.
	answer maxRows: maxRows.
	^answer
]

{ #category : 'querying' }
DbCommand >> fetchOne: sqlString with: anArrayOfValues [
	^(self query: sqlString with: anArrayOfValues) fetchOne
]

{ #category : 'querying' }
DbCommand >> fetchAll: sqlString with: anArrayOfValues [
	^(self query: sqlString with: anArrayOfValues) fetchAll
]

{ #category : 'private' }
DbCommand >> tableNameIn: sqlString [
	"The single table this SQL selects from, or nil.

	 Nil for anything with a join, a comma, or a subquery in the FROM.  That
	 is the point: a Record knows which table to write itself back to only
	 when there is exactly one candidate, and a guess here would produce an
	 UPDATE against the wrong one.  Answering nil makes DbRecord>>update
	 refuse with a sentence instead."

	| words fromIndex table |
	words := self wordsOf: sqlString.
	fromIndex := nil.
	words keysAndValuesDo:
		[:i :word |
		(fromIndex isNil and: [word asUppercase = 'FROM']) ifTrue: [fromIndex := i]].
	fromIndex isNil ifTrue: [^nil].
	fromIndex + 1 > words size ifTrue: [^nil].
	table := words at: fromIndex + 1.
	(table includes: $,) ifTrue: [^nil].
	(table includes: $() ifTrue: [^nil].
	"A word after the table that is not a clause keyword is an alias or a
	 join, and either way the plain name is no longer the only candidate."
	fromIndex + 2 <= words size ifTrue:
		[| next |
		 next := (words at: fromIndex + 2) asUppercase.
		 (#('WHERE' 'ORDER' 'GROUP' 'HAVING' 'LIMIT' 'FETCH' 'FOR' 'UNION' 'OFFSET')
			includes: next) ifFalse: [^nil]].
	^table
]

{ #category : 'private' }
DbCommand >> wordsOf: aString [
	"Split on white space, keeping commas and parentheses attached so that
	 tableNameIn: can see them."

	| words stream character |
	words := OrderedCollection new.
	stream := WriteStream on: (String new: 16).
	aString do:
		[:each |
		character := each.
		character isSeparator
			ifTrue: [stream contents isEmpty ifFalse:
					[words add: stream contents.
					 stream := WriteStream on: (String new: 16)]]
			ifFalse: [stream nextPut: character]].
	stream contents isEmpty ifFalse: [words add: stream contents].
	^words asArray
]

{ #category : 'closing' }
DbCommand >> cursorClosed [
	"Told by a cursor that it is done.  The statement stays prepared, which
	 is the whole reason a command exists."

	cursor := nil
]

{ #category : 'closing' }
DbCommand >> closeCursor [
	cursor isNil ifTrue: [^self].
	cursor close.
	cursor := nil
]

{ #category : 'closing' }
DbCommand >> closeStatement [
	statement isNil ifTrue: [^self].
	Odbc closeStatement: statement.
	statement := nil.
	sql := nil
]

{ #category : 'closing' }
DbCommand >> close [
	self closeCursor.
	self closeStatement.
	connection notNil ifTrue: [connection commandClosed: self].
	connection := nil
]

{ #category : 'printing' }
DbCommand >> printOn: aStream [
	aStream nextPutAll: 'a DbCommand('.
	aStream nextPutAll: (sql isNil ifTrue: ['unprepared'] ifFalse: [sql]).
	aStream nextPut: $)
]

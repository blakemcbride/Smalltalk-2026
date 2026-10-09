"
Open database connections, one handed to each request and taken back after.

lib/Database opens one connection per process and has no pool, because that
is the right shape for a program; a server that opened a connection per
request would spend more time connecting than querying.  So this opens a few
at start and keeps them in a SharedQueue: acquire takes one, blocking when
every connection is out -- which bounds the number of requests inside the
database at once to the pool's size -- and release: puts it back.  A
connection is in exactly one request at a time, which is the contract
DbConnection asks for.

A connection that is no longer a connection to anything is closed and
replaced rather than put back, so one broken connection does not poison the
requests after it: release: asks the driver whether the server is still
there (DbConnection>>isAlive), since a connection the server dropped
mid-request answers isOpen true for as long as the object lives, and before
it was asked only that (Bugs6 FILES-8).

The pool never shrinks.  When the replacement cannot be opened -- the
database is down -- what goes back into the queue is a PLACE: the pool
itself, standing for a connection to be opened by the next acquire that
draws it.  Before, the failure was logged and the pool was one connection
smaller for the rest of the server's life, and at zero every request waited
for ever on an empty queue, long after the database was back.  And acquire
waits for waitSeconds at most, thirty by default, then refuses the request
with a RestLogError -- a line in the log, not a request that never answers.
"
Class {
	#name : 'RestConnectionPool',
	#superclass : 'Object',
	#instVars : [ 'connectionString', 'queue', 'size', 'waitSeconds' ],
	#category : 'Rest-Server'
}

{ #category : 'instance creation' }
RestConnectionPool class >> on: aConnectionString size: anInteger [
	^self new setConnectionString: aConnectionString size: anInteger
]

{ #category : 'private' }
RestConnectionPool >> setConnectionString: aString size: anInteger [
	connectionString := aString.
	size := anInteger max: 1.
	waitSeconds := 30.
	queue := SharedQueue new
]

{ #category : 'opening' }
RestConnectionPool >> open [
	"Open every connection now, so a server that cannot reach its database
	 says so at start rather than at the first request."
	size timesRepeat: [queue nextPut: (DbConnection open: connectionString)]
]

{ #category : 'opening' }
RestConnectionPool >> closeAll [
	| c |
	[(c := queue nextOrNil) notNil] whileTrue: [
		c == self ifFalse: [[c close] on: Error do: [:e | nil]]]
]

{ #category : 'accessing' }
RestConnectionPool >> size [
	^size
]

{ #category : 'accessing' }
RestConnectionPool >> available [
	"How many connections are in, counting a place to be filled as one."
	^queue size
]

{ #category : 'accessing' }
RestConnectionPool >> waitSeconds [
	"How long acquire waits for a connection before it refuses."
	^waitSeconds
]

{ #category : 'accessing' }
RestConnectionPool >> waitSeconds: aNumber [
	waitSeconds := aNumber
]

{ #category : 'using' }
RestConnectionPool >> acquire [
	"A connection, waiting for one if every one is out -- for waitSeconds,
	 after which the request is refused rather than left waiting for ever.
	 A place in the queue that holds no connection (see replace:) is
	 filled here, so a database that was unreachable when a connection
	 broke is tried again by the next request, not never."
	| taken |
	taken := queue nextWithin: (waitSeconds * 1000) rounded ifNone: [
		^RestLogError new signal: 'every database connection was busy for ',
			waitSeconds printString, ' seconds'].
	^taken == self ifTrue: [self reopen] ifFalse: [taken]
]

{ #category : 'using' }
RestConnectionPool >> release: aConnection [
	"Put a connection back for the next request -- unless it is no longer
	 a connection to anything, whether closed or dropped by the server,
	 in which case it is closed and replaced."
	(aConnection isOpen and: [aConnection isAlive])
		ifTrue: [queue nextPut: aConnection]
		ifFalse: [self discard: aConnection]
]

{ #category : 'using' }
RestConnectionPool >> discard: aConnection [
	"The request broke this one: close it and open another in its place."
	[aConnection close] on: Error do: [:e | nil].
	self replace: aConnection
]

{ #category : 'private' }
RestConnectionPool >> replace: aConnection [
	"A fresh connection in aConnection's place -- or, when the database
	 cannot be reached now, a place in the queue for acquire to fill when
	 it can.  The pool keeps its size either way."
	[queue nextPut: (DbConnection open: connectionString)]
		on: Error
		do: [:e |
			RestLog error: 'cannot reopen a database connection: ', e messageText.
			queue nextPut: self]
]

{ #category : 'private' }
RestConnectionPool >> reopen [
	"A connection for a place drawn from the queue.  When it cannot be
	 opened the place goes back before the error goes out, so the pool is
	 not smaller for a database that is still down."
	^[DbConnection open: connectionString]
		on: Error
		do: [:e |
			queue nextPut: self.
			RestLogError new signal: 'cannot open a database connection: ', e messageText]
]

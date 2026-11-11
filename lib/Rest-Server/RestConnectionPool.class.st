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

CLOSING IS FOR GOOD, AND A CONNECTION OUT AT THE TIME IS CLOSED WHEN IT COMES
BACK.  closeAll closes what is in the queue; a connection lent to a request
still running is closed by release: when that request hands it back, and an
acquire after the close -- or one waiting when it came -- is refused with a
RestLogError rather than given a connection.  Before, closeAll drained the
queue and nothing else, and RestServer>>stop dropped the pool the moment the
listener closed: a request in flight committed, then its ensure: sent
release: to nil, and the MessageNotUnderstood that escaped turned a
committed request into an HTTP 500 -- the reply that tells a client to try
again what it has already done -- while its connection stayed open for the
life of the image (Bugs07 NET-10).  The pool knows the connections it has
lent, so release: also closes one this pool never lent: a request that
outlived a stop hands its connection to the server's NEXT pool after a
restart, which must not take it in.
"
Class {
	#name : 'RestConnectionPool',
	#superclass : 'Object',
	#instVars : [ 'connectionString', 'queue', 'size', 'waitSeconds', 'lock', 'lent', 'closed' ],
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
	queue := SharedQueue new.
	"Which connections are out, and whether the pool is closed: written
	 together, under lock, so that a connection is never put back into a
	 pool closeAll has already emptied."
	lock := Mutex new.
	lent := IdentitySet new.
	closed := false
]

{ #category : 'opening' }
RestConnectionPool >> open [
	"Open every connection now, so a server that cannot reach its database
	 says so at start rather than at the first request."
	size timesRepeat: [queue nextPut: (DbConnection open: connectionString)]
]

{ #category : 'opening' }
RestConnectionPool >> closeAll [
	"Close the connections that are in, and mark the pool closed for good:
	 each one still lent is closed when it is released, and every acquire
	 from now on is refused -- see the class comment.  The mark that
	 refuses is put in the queue as well, so that an acquire already
	 waiting there for a connection is woken and refused now, not after
	 waitSeconds.  The closes themselves happen after the lock is given
	 back: a driver's close can take as long as the server takes to
	 answer it."
	| idle |
	idle := OrderedCollection new.
	lock critical: [ | c |
		closed ifFalse: [
			closed := true.
			[(c := queue nextOrNil) notNil] whileTrue: [idle add: c].
			queue nextPut: self closedMark]].
	idle do: [:each | (each == self or: [each == self closedMark]) ifFalse: [self close: each]]
]

{ #category : 'accessing' }
RestConnectionPool >> isClosed [
	^closed
]

{ #category : 'accessing' }
RestConnectionPool >> lentCount [
	"How many connections are out in requests now."
	^lock critical: [lent size]
]

{ #category : 'accessing' }
RestConnectionPool >> size [
	^size
]

{ #category : 'accessing' }
RestConnectionPool >> available [
	"How many connections are in, counting a place to be filled as one;
	 none once the pool is closed."
	^closed ifTrue: [0] ifFalse: [queue size]
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
	 broke is tried again by the next request, not never.  A closed pool
	 refuses, and a connection taken as it closed is closed rather than
	 handed out: see the class comment."
	| taken connection handedOut |
	taken := queue nextWithin: (waitSeconds * 1000) rounded ifNone: [
		^RestLogError new signal: 'every database connection was busy for ',
			waitSeconds printString, ' seconds'].
	taken == self closedMark ifTrue: [
		"Back for whoever waits next, who is refused the same way."
		queue nextPut: taken.
		^self refuseClosed].
	connection := taken == self ifTrue: [self reopen] ifFalse: [taken].
	handedOut := lock critical: [closed ifFalse: [lent add: connection]. closed not].
	handedOut ifTrue: [^connection].
	self close: connection.
	^self refuseClosed
]

{ #category : 'using' }
RestConnectionPool >> release: aConnection [
	"Put a connection back for the next request -- unless it is no longer
	 a connection to anything, whether closed or dropped by the server,
	 in which case it is closed and replaced.  Into a closed pool nothing
	 goes back: the connection is closed.  Nor does one this pool did not
	 lend -- see the class comment."
	| mine |
	mine := lock critical: [(lent remove: aConnection ifAbsent: [nil]) notNil].
	mine ifFalse: [^self close: aConnection].
	(aConnection isOpen and: [aConnection isAlive])
		ifFalse: [^self discard: aConnection].
	(self putBack: aConnection) ifFalse: [self close: aConnection]
]

{ #category : 'using' }
RestConnectionPool >> discard: aConnection [
	"The request broke this one: close it and open another in its place."
	lock critical: [lent remove: aConnection ifAbsent: [nil]].
	self close: aConnection.
	self replace: aConnection
]

{ #category : 'private' }
RestConnectionPool >> replace: aConnection [
	"A fresh connection in aConnection's place -- or, when the database
	 cannot be reached now, a place in the queue for acquire to fill when
	 it can.  The pool keeps its size either way.  A closed pool is not
	 refilled."
	| fresh |
	closed ifTrue: [^self].
	fresh := [DbConnection open: connectionString]
		on: Error
		do: [:e |
			RestLog error: 'cannot reopen a database connection: ', e messageText.
			^self putBack: self].
	(self putBack: fresh) ifFalse: [self close: fresh]
]

{ #category : 'private' }
RestConnectionPool >> reopen [
	"A connection for a place drawn from the queue.  When it cannot be
	 opened the place goes back before the error goes out, so the pool is
	 not smaller for a database that is still down."
	^[DbConnection open: connectionString]
		on: Error
		do: [:e |
			self putBack: self.
			RestLogError new signal: 'cannot open a database connection: ', e messageText]
]

{ #category : 'private' }
RestConnectionPool >> putBack: aConnectionOrPlace [
	"Into the queue for the next acquire, unless the pool is closed.
	 Answer whether it went in."
	^lock critical: [closed ifFalse: [queue nextPut: aConnectionOrPlace]. closed not]
]

{ #category : 'private' }
RestConnectionPool >> close: aConnection [
	"A connection's last act, whatever the driver says to it."
	[aConnection close] on: Error do: [:e | nil]
]

{ #category : 'private' }
RestConnectionPool >> closedMark [
	"What closeAll leaves in the queue for every acquire to find."
	^#closed
]

{ #category : 'private' }
RestConnectionPool >> refuseClosed [
	^RestLogError new signal: 'the server has stopped: its database connections are closed'
]

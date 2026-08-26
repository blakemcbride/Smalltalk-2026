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

A connection that failed mid-request is closed and replaced rather than put
back, so one broken connection does not poison the requests after it.
"
Class {
	#name : 'RestConnectionPool',
	#superclass : 'Object',
	#instVars : [ 'connectionString', 'queue', 'size' ],
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
	[(c := queue nextOrNil) notNil] whileTrue: [[c close] on: Error do: [:e | nil]]
]

{ #category : 'accessing' }
RestConnectionPool >> size [
	^size
]

{ #category : 'accessing' }
RestConnectionPool >> available [
	^queue size
]

{ #category : 'using' }
RestConnectionPool >> acquire [
	"A connection, waiting for one if every one is out."
	^queue next
]

{ #category : 'using' }
RestConnectionPool >> release: aConnection [
	"Put a connection back for the next request."
	aConnection isOpen
		ifTrue: [queue nextPut: aConnection]
		ifFalse: [self replace: aConnection]
]

{ #category : 'using' }
RestConnectionPool >> discard: aConnection [
	"The request broke this one: close it and open another in its place."
	[aConnection close] on: Error do: [:e | nil].
	self replace: aConnection
]

{ #category : 'private' }
RestConnectionPool >> replace: aConnection [
	[queue nextPut: (DbConnection open: connectionString)]
		on: Error
		do: [:e | RestLog error: 'cannot reopen a database connection: ', e messageText]
]

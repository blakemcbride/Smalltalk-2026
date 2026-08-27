"
The REST server with a database under it: SQLite through ODBC, as
lib/Database-Live-Tests uses it, with one connection in the pool so that the
pool's give-and-take is exercised by every request.

What is checked is the one promise the dispatcher makes about a database: a
request that returns has its work committed, and a request that raises has
it rolled back -- seen from the next request, through the same connection.
"
Class {
	#name : 'RestLiveTest',
	#superclass : 'TestCase',
	#instVars : [ 'server' ],
	#category : 'Rest-Server-Live-Tests'
}

{ #category : 'fixture' }
RestLiveTest class >> databaseFile [
	^'st80-rest-live-test.db'
]

{ #category : 'running' }
RestLiveTest >> setUp [
	server := RestServer new
		port: 0;
		bindAddress: '127.0.0.1';
		backendDirectory: 'tests/rest-backend';
		database: 'DRIVER=SQLITE3;Database=', self class databaseFile, ';';
		connectionPoolSize: 1;
		requireAuthentication: false;
		name: 'live';
		yourself.
	server start.
	self call: 'reset'
]

{ #category : 'running' }
RestLiveTest >> tearDown [
	server notNil ifTrue: [server stop]
]

{ #category : 'private' }
RestLiveTest >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
RestLiveTest >> call: aMethod [
	^self call: aMethod with: [:j | ]
]

{ #category : 'private' }
RestLiveTest >> call: aMethod with: aBlock [
	"POST to services.Ledger on the fixture's server; answer the reply as a JSONObject."
	| injson |
	injson := JSONObject new at: '_class' put: 'services.Ledger'; at: '_method' put: aMethod; yourself.
	aBlock value: injson.
	^self post: injson to: server
]

{ #category : 'private' }
RestLiveTest >> post: injson to: aServer [
	"POST the document to aServer's /rest; answer the reply as a JSONObject."
	| body socket in status line length text |
	body := injson asJsonString.
	socket := Socket connectTo: '127.0.0.1' port: aServer port.
	^[socket send: 'POST /rest HTTP/1.1', self crlf, 'Host: live', self crlf,
		'Content-Type: application/json', self crlf,
		'Content-Length: ', body size printString, self crlf, 'Connection: close', self crlf, self crlf, body.
	  in := SocketStream on: socket.
	  in readTimeout: 10000.
	  status := in nextLine.
	  length := 0.
	  [(line := in nextLine) isEmpty] whileFalse: [
		(line asLowercase beginsWith: 'content-length:') ifTrue: [length := (line copyFrom: 16 to: line size) trimmed asInteger]].
	  text := in next: length.
	  self assert: status equals: 'HTTP/1.1 200 OK'.
	  JSONObject fromString: text] ensure: [socket close]
]

{ #category : 'tests' }
RestLiveTest >> testCommitOnSuccess [
	self assert: ((self call: 'count') integerAt: 'count') equals: 0.
	self assert: ((self call: 'deposit' with: [:j | j at: 'id' put: 1; at: 'amount' put: 10]) booleanAt: '_Success').
	self assert: ((self call: 'deposit' with: [:j | j at: 'id' put: 2; at: 'amount' put: 20]) booleanAt: '_Success').
	self assert: ((self call: 'count') integerAt: 'count') equals: 2
]

{ #category : 'tests' }
RestLiveTest >> testRollbackOnFailure [
	| reply |
	self call: 'deposit' with: [:j | j at: 'id' put: 1; at: 'amount' put: 10].
	reply := self call: 'depositThenFail' with: [:j | j at: 'id' put: 3; at: 'amount' put: 30].
	self deny: (reply booleanAt: '_Success').
	self assert: (reply integerAt: '_ErrorCode') equals: 9.
	self assert: ((self call: 'count') integerAt: 'count') equals: 1
]

{ #category : 'private' }
RestLiveTest >> initServerInMode: aStringOrNil [
	"A server on tests/rest-init-backend and this database, its Init told
	 what to do through the environment."
	| s |
	s := RestServer new
		port: 0;
		bindAddress: '127.0.0.1';
		backendDirectory: 'tests/rest-init-backend';
		database: 'DRIVER=SQLITE3;Database=', self class databaseFile, ';';
		connectionPoolSize: 1;
		requireAuthentication: false;
		name: 'init';
		yourself.
	aStringOrNil notNil ifTrue: [s environmentAt: 'init2Mode' put: aStringOrNil].
	^s
]

{ #category : 'private' }
RestLiveTest >> marksOn: aServer [
	^(self post: (JSONObject new at: '_class' put: 'services.Marks'; at: '_method' put: 'count'; yourself) to: aServer)
		integerAt: 'count'
]

{ #category : 'tests' }
RestLiveTest >> testInit2IsCommitted [
	"init2: gets a connection of its own and what it wrote is there for the
	 first request."
	| s |
	s := self initServerInMode: 'reset'.
	s start; stop.
	s := self initServerInMode: nil.
	[s start.
	 self assert: ((Smalltalk at: #RestInit2Seen) isKindOf: DbConnection).
	 self assert: (self marksOn: s) equals: 1]
		ensure: [s stop]
]

{ #category : 'tests' }
RestLiveTest >> testInit2FailureIsRolledBackAndStopsTheStart [
	"A row inserted before init2: raised is not there afterwards, and the
	 server that could not initialise is not listening."
	| s |
	s := self initServerInMode: 'reset'.
	s start; stop.
	s := self initServerInMode: 'fail'.
	self should: [s start] raise: Error.
	self deny: s isRunning.
	s := self initServerInMode: nil.
	[s start.
	 self assert: (self marksOn: s) equals: 1]
		ensure: [s stop]
]

{ #category : 'tests' }
RestLiveTest >> testLoginIsRequiredWhenThereIsADatabaseUnlessSaidOtherwise [
	| withDatabase |
	withDatabase := RestServer new database: 'DRIVER=SQLITE3;Database=x.db;'; yourself.
	self assert: withDatabase requiresAuthentication.
	self deny: server requiresAuthentication
]

"
The demo application with its database: SQLite through ODBC, the tables
made by the demo's own Init from Kiss's schema file, and its one user,
smalltalk, logging in with the hash Kiss stored.  Then everything the CRUD and Users
screens do, through the wire, with a session.

The fixture drops the two tables before every test so that each start is
a first start; the file stays beside the image, as the other live
fixtures' do.  Every login here is six hundred thousand PBKDF2 rounds --
a fifth of a second -- which is the cost of checking the real thing.
"
Class {
	#name : 'WebDemoLiveTest',
	#superclass : 'TestCase',
	#instVars : [ 'server' ],
	#category : 'Web-Demo-Live-Tests'
}

{ #category : 'fixture' }
WebDemoLiveTest class >> databaseFile [
	^'st80-web-demo-test.db'
]

{ #category : 'fixture' }
WebDemoLiveTest >> connectionString [
	^'DRIVER=SQLITE3;Database=', self class databaseFile, ';'
]

{ #category : 'fixture' }
WebDemoLiveTest >> newServer [
	| s |
	s := RestServer new
		port: 0;
		bindAddress: '127.0.0.1';
		backendDirectory: 'demo/backend';
		database: self connectionString;
		connectionPoolSize: 2;
		name: 'demo-live';
		yourself.
	s environmentAt: 'schema' put: 'demo/schema.sqlite'.
	^s
]

{ #category : 'running' }
WebDemoLiveTest >> setUp [
	"No tables, so that Init makes them: reset in SQL, as DbLiveTest is."
	DbConnection open: self connectionString do: [:c |
		#('phone' 'users') do: [:each |
			(c tableExists: each) ifTrue: [c executeImmediate: 'DROP TABLE ', each]].
		c commit].
	server := self newServer.
	server start
]

{ #category : 'running' }
WebDemoLiveTest >> tearDown [
	server notNil ifTrue: [server stop]
]

{ #category : 'private' }
WebDemoLiveTest >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
WebDemoLiveTest >> post: injson to: aServer [
	| body socket in status line length text |
	body := injson asJsonString.
	socket := Socket connectTo: '127.0.0.1' port: aServer port.
	^[socket send: 'POST /rest HTTP/1.1', self crlf, 'Host: demo', self crlf,
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

{ #category : 'private' }
WebDemoLiveTest >> call: aClass method: aMethod uuid: aUuidOrNil with: aBlock [
	| injson |
	injson := JSONObject new.
	injson at: '_class' put: aClass; at: '_method' put: aMethod.
	aUuidOrNil notNil ifTrue: [injson at: '_uuid' put: aUuidOrNil].
	aBlock value: injson.
	^self post: injson to: server
]

{ #category : 'private' }
WebDemoLiveTest >> call: aClass method: aMethod uuid: aUuid [
	^self call: aClass method: aMethod uuid: aUuid with: [:j | ]
]

{ #category : 'private' }
WebDemoLiveTest >> login: aName password: aPassword [
	^self call: '' method: 'Login' uuid: nil with: [:j | j at: 'username' put: aName; at: 'password' put: aPassword]
]

{ #category : 'private' }
WebDemoLiveTest >> loginAs: aName password: aPassword [
	"The uuid of a login that must succeed."
	| reply |
	reply := self login: aName password: aPassword.
	self assert: (reply booleanAt: '_Success').
	^reply stringAt: 'uuid'
]

{ #category : 'private' }
WebDemoLiveTest >> rowsOf: aClass uuid: aUuid [
	^(self call: aClass method: 'getRecords' uuid: aUuid) arrayAt: 'rows'
]

{ #category : 'tests' }
WebDemoLiveTest >> testSmalltalkLogsIn [
	"The user the schema inserts, smalltalk, with the hash Java made for
	 Kiss's user: the name changed, the hash did not."
	| reply |
	self assert: ((self call: '' method: 'LoginRequired' uuid: nil) booleanAt: 'LoginRequired').
	reply := self login: 'smalltalk' password: 'password'.
	self assert: (reply booleanAt: '_Success').
	self assert: (reply stringAt: 'uuid') size equals: 36.
	reply := self login: 'smalltalk' password: 'Password'.
	self deny: (reply booleanAt: '_Success').
	reply := self login: 'nobody' password: 'password'.
	self deny: (reply booleanAt: '_Success')
]

{ #category : 'tests' }
WebDemoLiveTest >> testACallWithoutALoginIsRefused [
	| reply |
	reply := self call: 'services/Crud' method: 'getRecords' uuid: nil.
	self deny: (reply booleanAt: '_Success').
	self assert: (reply integerAt: '_ErrorCode') equals: 2
]

{ #category : 'tests' }
WebDemoLiveTest >> testThePhoneList [
	"What CRUD.js does: list, add, edit, delete."
	| uuid rows id |
	uuid := self loginAs: 'smalltalk' password: 'password'.
	self assert: (self rowsOf: 'services/Crud' uuid: uuid) size equals: 0.
	self assert: ((self call: 'services/Crud' method: 'addRecord' uuid: uuid with: [:j |
		j at: 'firstName' put: 'Ada'; at: 'lastName' put: 'Lovelace'; at: 'phoneNumber' put: '555-0100'])
			booleanAt: '_Success').
	rows := self rowsOf: 'services/Crud' uuid: uuid.
	self assert: rows size equals: 1.
	self assert: ((rows at: 1) stringAt: 'lastName') equals: 'Lovelace'.
	self assert: ((rows at: 1) stringAt: 'phoneNumber') equals: '555-0100'.
	id := (rows at: 1) integerAt: 'id'.
	self assert: ((self call: 'services/Crud' method: 'updateRecord' uuid: uuid with: [:j |
		j at: 'id' put: id; at: 'firstName' put: 'Ada'; at: 'lastName' put: 'Lovelace'; at: 'phoneNumber' put: '555-0199'])
			booleanAt: '_Success').
	self assert: (((self rowsOf: 'services/Crud' uuid: uuid) at: 1) stringAt: 'phoneNumber') equals: '555-0199'.
	self assert: ((self call: 'services/Crud' method: 'deleteRecord' uuid: uuid with: [:j | j at: 'id' put: id])
		booleanAt: '_Success').
	self assert: (self rowsOf: 'services/Crud' uuid: uuid) size equals: 0
]

{ #category : 'private' }
WebDemoLiveTest >> addBobWith: aUuid [
	"The Users screen's Add, for bob with the password sesame; answer his id."
	self assert: ((self call: 'services/Users' method: 'addRecord' uuid: aUuid with: [:j |
		j at: 'userName' put: 'bob'; at: 'userPassword' put: 'sesame'; at: 'userActive' put: 'Y'])
			booleanAt: '_Success').
	^((self rowsOf: 'services/Users' uuid: aUuid)
		detect: [:r | (r stringAt: 'userName') = 'bob'] ifNone: [self error: 'bob was not added']) integerAt: 'id'
]

{ #category : 'private' }
WebDemoLiveTest >> editUser: anId password: aPassword active: aFlag uuid: aUuid [
	"The Users screen's Edit: a blank password means keep the old one."
	self assert: ((self call: 'services/Users' method: 'updateRecord' uuid: aUuid with: [:j |
		j at: 'id' put: anId; at: 'userName' put: 'bob'; at: 'userPassword' put: aPassword; at: 'userActive' put: aFlag])
			booleanAt: '_Success')
]

{ #category : 'tests' }
WebDemoLiveTest >> testTheUsersScreen [
	"What Users.js does: list, without the password column; add, and the
	 new user logs in; edit with a blank password, and the old one still
	 works."
	| uuid rows bob |
	uuid := self loginAs: 'smalltalk' password: 'password'.
	rows := self rowsOf: 'services/Users' uuid: uuid.
	self assert: rows size equals: 1.
	self assert: ((rows at: 1) stringAt: 'userName') equals: 'smalltalk'.
	self deny: ((rows at: 1) includesKey: 'userPassword').
	bob := self addBobWith: uuid.
	self loginAs: 'bob' password: 'sesame'.
	self assert: (self rowsOf: 'services/Users' uuid: uuid) size equals: 2.
	self editUser: bob password: '' active: 'Y' uuid: uuid.
	self loginAs: 'bob' password: 'sesame'
]

{ #category : 'tests' }
WebDemoLiveTest >> testAPasswordChangeAndADeletion [
	"A new password replaces the old at the login, and a deleted user
	 cannot log in."
	| uuid bob |
	uuid := self loginAs: 'smalltalk' password: 'password'.
	bob := self addBobWith: uuid.
	self editUser: bob password: 'open' active: 'Y' uuid: uuid.
	self loginAs: 'bob' password: 'open'.
	self deny: ((self login: 'bob' password: 'sesame') booleanAt: '_Success').
	self call: 'services/Users' method: 'deleteRecord' uuid: uuid with: [:j | j at: 'id' put: bob].
	self deny: ((self login: 'bob' password: 'open') booleanAt: '_Success').
	self assert: (self rowsOf: 'services/Users' uuid: uuid) size equals: 1
]

{ #category : 'tests' }
WebDemoLiveTest >> testAnInactiveUserCannotLogIn [
	| uuid id |
	uuid := self loginAs: 'smalltalk' password: 'password'.
	id := ((self rowsOf: 'services/Users' uuid: uuid) at: 1) integerAt: 'id'.
	self call: 'services/Users' method: 'updateRecord' uuid: uuid with: [:j |
		j at: 'id' put: id; at: 'userName' put: 'smalltalk'; at: 'userPassword' put: ''; at: 'userActive' put: 'N'].
	self deny: ((self login: 'smalltalk' password: 'password') booleanAt: '_Success')
]

{ #category : 'tests' }
WebDemoLiveTest >> testLegacyPasswordsStillLogIn [
	"Kiss accepted plain text and a bare SHA-256 hex from before hashing,
	 so that old accounts keep working until their password changes."
	DbConnection open: self connectionString do: [:c |
		c execute: 'insert into users (user_name, user_password, user_active) values (?, ?, ?)'
			with: (Array with: 'plain' with: 'pw' with: 'Y').
		c execute: 'insert into users (user_name, user_password, user_active) values (?, ?, ?)'
			with: (Array with: 'hexed' with: (Crypto hex: (Crypto sha256: 'pw')) with: 'Y').
		c commit].
	self loginAs: 'plain' password: 'pw'.
	self loginAs: 'hexed' password: 'pw'.
	self deny: ((self login: 'plain' password: 'pwd') booleanAt: '_Success').
	self deny: ((self login: 'hexed' password: 'PW') booleanAt: '_Success')
]

{ #category : 'tests' }
WebDemoLiveTest >> testTheTablesAreMadeOnceAndKept [
	"A second start finds the tables and leaves them: the row is still
	 there and Init does not run the schema again."
	| uuid |
	uuid := self loginAs: 'smalltalk' password: 'password'.
	self call: 'services/Crud' method: 'addRecord' uuid: uuid with: [:j |
		j at: 'firstName' put: 'Grace'; at: 'lastName' put: 'Hopper'; at: 'phoneNumber' put: '555-0101'].
	server stop.
	server := self newServer.
	server start.
	uuid := self loginAs: 'smalltalk' password: 'password'.
	self assert: (self rowsOf: 'services/Crud' uuid: uuid) size equals: 1.
	self assert: (self rowsOf: 'services/Users' uuid: uuid) size equals: 1
]

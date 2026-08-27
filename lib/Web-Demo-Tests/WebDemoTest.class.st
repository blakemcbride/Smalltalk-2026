"
The demo application without a database: the front end served from the
document root, the services that need no database -- addNumbers,
hasDatabase, an upload -- and what the ones that do need one say when there
is none.  The client is a request written on the raw socket, as
RestServerTest's is, so what is tested is what a browser gets -- and what a
browser sends: the services are named as the front end names them,
`services/MyService' with a slash, because the day these tests spelled it
`services.MyService' was the day a browser found that the server took only
the dots (RestServiceLoader's comment).  With a
database: WebDemoLiveTest, in the database-live profile.

Every test starts a server on demo/backend and demo/frontend on a port the
system picks, so the demo's own files are what is loaded, and the Init
class in that back end runs on every start with no database to fill.
"
Class {
	#name : 'WebDemoTest',
	#superclass : 'TestCase',
	#instVars : [ 'server' ],
	#category : 'Web-Demo-Tests'
}

{ #category : 'running' }
WebDemoTest >> setUp [
	server := RestServer new
		port: 0;
		bindAddress: '127.0.0.1';
		backendDirectory: 'demo/backend';
		documentRoot: 'demo/frontend';
		name: 'demo';
		yourself.
	server start
]

{ #category : 'running' }
WebDemoTest >> tearDown [
	server notNil ifTrue: [server stop]
]

{ #category : 'private' }
WebDemoTest >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
WebDemoTest >> text: aString includes: aSubstring [
	"A plain scan, so that nothing here rests on a String protocol the
	 profile might not have."
	1 to: aString size - aSubstring size + 1 do: [:i |
		((aString copyFrom: i to: i + aSubstring size - 1) = aSubstring) ifTrue: [^true]].
	^false
]

{ #category : 'private' }
WebDemoTest >> request: aString [
	"Send the request text; answer the status line, the headers by
	 lowercase name, and the body."
	| socket in status headers line length body |
	socket := Socket connectTo: '127.0.0.1' port: server port.
	^[socket send: aString.
	  in := SocketStream on: socket.
	  in readTimeout: 10000.
	  status := in nextLine.
	  headers := Dictionary new.
	  [(line := in nextLine) isEmpty] whileFalse: [ | colon |
		colon := line indexOf: $:.
		headers at: (line copyFrom: 1 to: colon - 1) asLowercase
			put: (line copyFrom: colon + 1 to: line size) trimmed].
	  length := (headers at: 'content-length') asInteger.
	  body := in next: length.
	  Array with: status with: headers with: body] ensure: [socket close]
]

{ #category : 'private' }
WebDemoTest >> get: aPath [
	^self request: 'GET ', aPath, ' HTTP/1.1', self crlf, 'Host: demo', self crlf,
		'Connection: close', self crlf, self crlf
]

{ #category : 'private' }
WebDemoTest >> postRaw: aBody contentType: aType [
	^self request: 'POST /rest HTTP/1.1', self crlf, 'Host: demo', self crlf,
		'Content-Type: ', aType, self crlf,
		'Content-Length: ', aBody size printString, self crlf,
		'Connection: close', self crlf, self crlf, aBody
]

{ #category : 'private' }
WebDemoTest >> call: aClass method: aMethod with: aBlock [
	| injson reply |
	injson := JSONObject new.
	injson at: '_class' put: aClass; at: '_method' put: aMethod.
	aBlock value: injson.
	reply := self postRaw: injson asJsonString contentType: 'application/json'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	^JSONObject fromString: (reply at: 3)
]

{ #category : 'private' }
WebDemoTest >> call: aClass method: aMethod [
	^self call: aClass method: aMethod with: [:j | ]
]

{ #category : 'tests - front end' }
WebDemoTest >> testTheFrontEndIsServed [
	"The pages, the framework, a screen, a library, by the paths the
	 browser asks for -- with the cache-busting query string it adds."
	| reply |
	reply := self get: '/'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: ((reply at: 2) at: 'content-type') equals: 'text/html'.
	self assert: (self text: (reply at: 3) includes: 'Smalltalk-2026').
	reply := self get: '/index.html?now=1756200000000'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: ((reply at: 2) at: 'content-type') equals: 'text/html'.
	reply := self get: '/kiss/Server.js?ver=1'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: ((reply at: 2) at: 'content-type') equals: 'application/javascript'.
	reply := self get: '/screens/CRUD/CRUD.html'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	reply := self get: '/lib/ag-grid-community.min.noStyle.js'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: (reply at: 3) size > 1000000.
	reply := self get: '/nothing.html'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 404 Not Found'
]

{ #category : 'tests - front end' }
WebDemoTest >> testTheFrontEndIsStampedForOneServer [
	"The stamp on Kiss's front end, which a careless re-copy would lose."
	| reply |
	reply := self get: '/SystemInfo.js'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: (self text: (reply at: 3) includes: 'SystemInfo.sameOriginBackend = true')
]

{ #category : 'tests - front end' }
WebDemoTest >> testTheRestServicesScreenHasOneButton [
	"Kiss's screen has three buttons -- Groovy, Java, Lisp -- and three
	 services; this one has one of each (MyService's comment says why).
	 A careless re-copy of the front end would bring the three back, and
	 the two that are gone would answer No such class."
	| reply |
	reply := self get: '/screens/RestServices/RestServices.js'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: (self text: (reply at: 3) includes: 'services/MyService').
	self deny: (self text: (reply at: 3) includes: 'MyGroovyService').
	reply := self get: '/screens/RestServices/RestServices.html'.
	self assert: (self text: (reply at: 3) includes: 'Call Service').
	self deny: (self text: (reply at: 3) includes: 'Call Groovy')
]

{ #category : 'tests - services' }
WebDemoTest >> testAddNumbers [
	| reply |
	reply := self call: 'services/MyService' method: 'addNumbers' with: [:j | j at: 'num1' put: 3; at: 'num2' put: 4].
	self assert: (reply booleanAt: '_Success').
	self assert: (reply integerAt: 'num3') equals: 7.
	"Kiss answers 0 when either number is missing."
	reply := self call: 'services/MyService' method: 'addNumbers' with: [:j | j at: 'num1' put: 3].
	self assert: (reply integerAt: 'num3') equals: 0.
	reply := self call: 'services/MyService' method: 'addNumbers'.
	self assert: (reply integerAt: 'num3') equals: 0
]

{ #category : 'tests - services' }
WebDemoTest >> testHasDatabaseIsFalse [
	| reply |
	reply := self call: 'services/MyService' method: 'hasDatabase'.
	self assert: (reply booleanAt: '_Success').
	self deny: (reply booleanAt: 'hasDatabase')
]

{ #category : 'tests - services' }
WebDemoTest >> testLoginIsNotRequiredWithoutADatabase [
	| reply |
	reply := self call: '' method: 'LoginRequired'.
	self deny: (reply booleanAt: 'LoginRequired')
]

{ #category : 'tests - services' }
WebDemoTest >> testCrudAndUsersWithoutADatabaseSaySo [
	"`nodb', which both screens read as `this needs a database'."
	self assert: ((self call: 'services/Crud' method: 'getRecords') booleanAt: 'nodb').
	self assert: ((self call: 'services/Users' method: 'getRecords') booleanAt: 'nodb')
]

{ #category : 'tests - services' }
WebDemoTest >> testAnUpload [
	"What FileUploadScreen.js sends: the two fields it adds and the file."
	| b body reply |
	b := '----KissBoundary'.
	body := '--', b, self crlf, 'Content-Disposition: form-data; name="_class"', self crlf, self crlf, 'services/FileUpload', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="_method"', self crlf, self crlf, 'upload', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="var1"', self crlf, self crlf, '22', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="var2"', self crlf, self crlf, '33', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="_file-0"; filename="report.txt"', self crlf, 'Content-Type: text/plain', self crlf, self crlf, 'hello upload', self crlf,
		'--', b, '--', self crlf.
	reply := JSONObject fromString: ((self postRaw: body contentType: 'multipart/form-data; boundary=', b) at: 3).
	self assert: (reply booleanAt: '_Success').
	self assert: (reply integerAt: 'count') equals: 1.
	self assert: (reply stringAt: 'name') equals: 'report.txt'.
	self assert: (reply integerAt: 'size') equals: 12
]

{ #category : 'tests - services' }
WebDemoTest >> testReportsAreNotServed [
	"The CRUD screen's Report and Export buttons, until that is built."
	| reply |
	reply := self call: 'services/Crud' method: 'runReport'.
	self deny: (reply booleanAt: '_Success').
	self assert: (self text: (reply stringAt: '_ErrorMessage') includes: 'No such method').
	reply := self call: 'services/Crud' method: 'runExport'.
	self deny: (reply booleanAt: '_Success')
]

{ #category : 'tests - services' }
WebDemoTest >> testOllamaIsAskedWhetherItIsUp [
	"True or false depending on the machine -- an Ollama may well be
	 running on the developer's -- and never an error either way."
	| reply |
	reply := self call: 'services/OllamaQuery' method: 'isOllamaUp'.
	self assert: (reply booleanAt: '_Success').
	self assert: ((reply at: 'isOllamaUp') isKindOf: Boolean)
]

{ #category : 'tests - scripts' }
WebDemoTest >> testAScriptIsLoadedByItsPath [
	"backend/scripts/MyScript.class.st, as a service would reach it."
	self assert: ((server loader classFor: 'scripts.MyScript') myMethod: 3 with: 4) equals: 107
]

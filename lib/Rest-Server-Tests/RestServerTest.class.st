"
The whole stack, over the loopback interface, against the back end in
tests/rest-backend: the envelope, the three framework methods, sessions,
service loading and reloading, uploads, binary replies, and the error codes.

The client is a POST written on the raw socket and the reply is read back as
JSON, so what is tested is the wire Kiss's Server.js speaks.  No database:
the half that needs one is in lib/Rest-Server-Live-Tests.
"
Class {
	#name : 'RestServerTest',
	#superclass : 'TestCase',
	#instVars : [ 'server', 'scratch' ],
	#category : 'Rest-Server-Tests'
}

{ #category : 'running' }
RestServerTest >> setUp [
	scratch := 'tests/rest-backend/services/Scratch.class.st'.
	server := RestServer new
		port: 0;
		bindAddress: '127.0.0.1';
		backendDirectory: 'tests/rest-backend';
		name: 'test';
		yourself
]

{ #category : 'running' }
RestServerTest >> tearDown [
	server notNil ifTrue: [server stop].
	(Smalltalk includesKey: #Scratch) ifTrue: [
		TonelSource forget: (Smalltalk at: #Scratch).
		(Smalltalk at: #Scratch) removeFromSystem].
	((Disk directoryNamed: 'tests/rest-backend/services') includesKey: 'Scratch.class.st')
		ifTrue: [(Disk directoryNamed: 'tests/rest-backend/services') removeKey: 'Scratch.class.st']
]

{ #category : 'private' }
RestServerTest >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
RestServerTest >> readResponseFrom: in [
	| status headers line length body |
	status := in nextLine.
	headers := Dictionary new.
	[(line := in nextLine) isEmpty] whileFalse: [ | colon |
		colon := line indexOf: $:.
		headers at: (line copyFrom: 1 to: colon - 1) asLowercase
			put: (line copyFrom: colon + 1 to: line size) trimmed].
	length := (headers at: 'content-length') asInteger.
	body := in next: length.
	^Array with: status with: headers with: body
]

{ #category : 'private' }
RestServerTest >> postRaw: aBody contentType: aType [
	"POST to /rest; answer the status line, headers and body."
	| socket in |
	socket := Socket connectTo: '127.0.0.1' port: server port.
	^[socket send: 'POST /rest HTTP/1.1', self crlf, 'Host: test', self crlf,
		'Content-Type: ', aType, self crlf,
		'Content-Length: ', aBody size printString, self crlf, 'Connection: close', self crlf, self crlf, aBody.
	  in := SocketStream on: socket.
	  in readTimeout: 10000.
	  self readResponseFrom: in] ensure: [socket close]
]

{ #category : 'private' }
RestServerTest >> post: aJsonObject [
	"POST the document; answer the reply as a JSONObject."
	| reply |
	reply := self postRaw: aJsonObject asJsonString contentType: 'application/json'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	^JSONObject fromString: (reply at: 3)
]

{ #category : 'private' }
RestServerTest >> call: aClass method: aMethod with: aBlock [
	"A request for aClass and aMethod, with aBlock filling the rest."
	| injson |
	injson := JSONObject new.
	injson at: '_class' put: aClass; at: '_method' put: aMethod.
	aBlock value: injson.
	^self post: injson
]

{ #category : 'private' }
RestServerTest >> call: aClass method: aMethod [
	^self call: aClass method: aMethod with: [:j | ]
]

{ #category : 'private' }
RestServerTest >> login [
	"Log ada in; answer the uuid."
	| reply |
	reply := self call: '' method: 'Login' with: [:j | j at: 'username' put: 'ada'; at: 'password' put: 'secret'].
	self assert: (reply booleanAt: '_Success').
	^reply stringAt: 'uuid'
]

{ #category : 'tests' }
RestServerTest >> testAServiceCall [
	| reply |
	server start.
	reply := self call: 'services.Adder' method: 'addNumbers' with: [:j | j at: 'num1' put: 3; at: 'num2' put: 4].
	self assert: (reply integerAt: 'num3') equals: 7.
	self assert: (reply booleanAt: '_Success').
	self assert: (reply integerAt: '_ErrorCode') equals: 0.
	self assert: (reply stringAt: '_BootId') size equals: 36.
	self assert: (reply stringAt: '_BootId') equals: server bootId.
	self deny: (reply includesKey: '_ErrorMessage')
]

{ #category : 'tests' }
RestServerTest >> testTheFrontEndsSpellingIsTheSameClass [
	"Kiss's front end sends `services/Adder' where every test here had
	 sent `services.Adder', and until a browser was pointed at the demo
	 nothing had sent the slash to this server: it was `not a class name'
	 and every screen but the login failed.  The two are one class, once
	 loaded; a slash-spelt name that has no file says so as the dotted one
	 does; and a separator with nothing after it is still refused."
	| reply |
	server start.
	reply := self call: 'services/Adder' method: 'addNumbers' with: [:j | j at: 'num1' put: 3; at: 'num2' put: 4].
	self assert: (reply booleanAt: '_Success').
	self assert: (reply integerAt: 'num3') equals: 7.
	reply := self call: 'services.Adder' method: 'addNumbers' with: [:j | j at: 'num1' put: 5; at: 'num2' put: 6].
	self assert: (reply integerAt: 'num3') equals: 11.
	self assert: (server loader classFor: 'services/Adder') == (server loader classFor: 'services.Adder').
	reply := self call: 'services/Nowhere' method: 'x'.
	self deny: (reply booleanAt: '_Success').
	self assert: ((reply stringAt: '_ErrorMessage') includesSubstring: 'No back-end code found').
	#('services//Adder' '/services.Adder' 'services.Adder/' 'services/../Adder' '.') do: [:each |
		reply := self call: each method: 'addNumbers'.
		self deny: (reply booleanAt: '_Success').
		self assert: ((reply stringAt: '_ErrorMessage') includesSubstring: 'not a class name')]
]

{ #category : 'tests' }
RestServerTest >> testLoginRequiredIsFalseWithNoDatabase [
	| reply |
	server start.
	reply := self call: '' method: 'LoginRequired'.
	self deny: (reply booleanAt: 'LoginRequired').
	self assert: (reply booleanAt: '_Success')
]

{ #category : 'tests' }
RestServerTest >> testRefusals [
	| reply |
	server start.
	reply := self call: 'services.Nowhere' method: 'x'.
	self deny: (reply booleanAt: '_Success').
	self assert: ((reply stringAt: '_ErrorMessage') includesSubstring: 'No back-end code found').
	reply := self call: 'services.Adder' method: 'noSuchMethod'.
	self assert: ((reply stringAt: '_ErrorMessage') includesSubstring: 'No such method').
	"Object's methods, the conveniences, and methods with arguments are out of reach."
	reply := self call: 'services.Adder' method: 'printString'.
	self assert: ((reply stringAt: '_ErrorMessage') includesSubstring: 'No such method').
	reply := self call: 'services.Adder' method: 'requireLogin'.
	self assert: ((reply stringAt: '_ErrorMessage') includesSubstring: 'No such method').
	reply := self call: 'services.Adder' method: 'helper:'.
	self assert: ((reply stringAt: '_ErrorMessage') includesSubstring: 'No such method').
	reply := self call: '../../../etc/passwd' method: 'x'.
	self deny: (reply booleanAt: '_Success').
	reply := self call: '' method: 'NotACoreMethod'.
	self assert: (reply stringAt: '_ErrorMessage') equals: 'Incorrect internal method call.'
]

{ #category : 'tests' }
RestServerTest >> testAMissingClassIsAnErrorNotAHang [
	| reply |
	server start.
	reply := self post: (JSONObject new at: '_method' put: 'x'; yourself).
	self deny: (reply booleanAt: '_Success').
	self assert: (reply stringAt: '_ErrorMessage') equals: 'Missing _class'.
	self assert: (reply integerAt: '_ErrorCode') equals: -1
]

{ #category : 'tests' }
RestServerTest >> testInvalidJson [
	| reply |
	server start.
	reply := JSONObject fromString: ((self postRaw: '{not json' contentType: 'application/json') at: 3).
	self deny: (reply booleanAt: '_Success').
	self assert: (reply stringAt: '_ErrorMessage') equals: 'Invalid JSON format'
]

{ #category : 'tests' }
RestServerTest >> testErrorCodes [
	| reply |
	server start.
	reply := self call: 'services.Failing' method: 'userError'.
	self deny: (reply booleanAt: '_Success').
	self assert: (reply integerAt: '_ErrorCode') equals: 7.
	self assert: (reply stringAt: '_ErrorMessage') equals: 'no such product'.
	reply := self call: 'services.Failing' method: 'logged'.
	self assert: (reply integerAt: '_ErrorCode') equals: -1.
	self assert: (reply stringAt: '_ErrorMessage') equals: 'noted'.
	reply := self call: 'services.Failing' method: 'crash'.
	self assert: (reply integerAt: '_ErrorCode') equals: -1.
	self deny: (reply booleanAt: '_Success')
]

{ #category : 'tests' }
RestServerTest >> testSessions [
	| uuid reply |
	server requireAuthentication: true.
	server start.
	self assert: ((self call: '' method: 'LoginRequired') booleanAt: 'LoginRequired').
	"No login: refused with code 2."
	reply := self call: 'services.Adder' method: 'addNumbers' with: [:j | j at: 'num1' put: 1; at: 'num2' put: 2].
	self deny: (reply booleanAt: '_Success').
	self assert: (reply integerAt: '_ErrorCode') equals: 2.
	self assert: (reply stringAt: '_BootId') equals: server bootId.
	"A wrong password."
	reply := self call: '' method: 'Login' with: [:j | j at: 'username' put: 'ada'; at: 'password' put: 'wrong'].
	self deny: (reply booleanAt: '_Success').
	self assert: (reply stringAt: '_ErrorMessage') equals: 'Invalid login.'.
	"The right one."
	uuid := self login.
	self assert: uuid size equals: 36.
	reply := self call: 'services.Adder' method: 'addNumbers' with: [:j | j at: '_uuid' put: uuid; at: 'num1' put: 1; at: 'num2' put: 2].
	self assert: (reply integerAt: 'num3') equals: 3.
	reply := self call: 'services.Secret' method: 'secret' with: [:j | j at: '_uuid' put: uuid].
	self assert: (reply stringAt: 'user') equals: 'ada'.
	self assert: (reply integerAt: 'userId') equals: 1.
	"State kept beside the user between requests."
	self call: 'services.Secret' method: 'remember' with: [:j | j at: '_uuid' put: uuid; at: 'note' put: 'lemons'].
	reply := self call: 'services.Secret' method: 'recall' with: [:j | j at: '_uuid' put: uuid].
	self assert: (reply stringAt: 'note') equals: 'lemons'.
	"Logout, and the session is gone."
	reply := self call: '' method: 'Logout' with: [:j | j at: '_uuid' put: uuid].
	self assert: (reply booleanAt: 'success').
	reply := self call: 'services.Secret' method: 'secret' with: [:j | j at: '_uuid' put: uuid].
	self assert: (reply integerAt: '_ErrorCode') equals: 2
]

{ #category : 'tests' }
RestServerTest >> testAllowedWithoutAuthentication [
	| reply |
	server requireAuthentication: true.
	server allowWithoutAuthentication: 'services.Adder' method: 'addNumbers'.
	server start.
	reply := self call: 'services.Adder' method: 'addNumbers' with: [:j | j at: 'num1' put: 5; at: 'num2' put: 6].
	self assert: (reply integerAt: 'num3') equals: 11.
	reply := self call: 'services.Adder' method: 'whoAmI' with: [:j | j at: 'num1' put: 5; at: 'num2' put: 6].
	self assert: (reply integerAt: '_ErrorCode') equals: 2
]

{ #category : 'tests' }
RestServerTest >> testRequireLoginInsideAService [
	"With no login required by the server, a service may still insist."
	| reply |
	server start.
	reply := self call: 'services.Secret' method: 'secret'.
	self assert: (reply integerAt: '_ErrorCode') equals: 2.
	self assert: (reply stringAt: '_ErrorMessage') equals: 'login required'
]

{ #category : 'tests' }
RestServerTest >> testAnUpload [
	| b body reply names |
	server start.
	b := '----KissBoundary'.
	body := '--', b, self crlf, 'Content-Disposition: form-data; name="_class"', self crlf, self crlf, 'services.Uploader', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="_method"', self crlf, self crlf, 'upload', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="note"', self crlf, self crlf, 'Sa note', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="flag"', self crlf, self crlf, 'true', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="n"', self crlf, self crlf, '42', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="_file-0"; filename="one.txt"', self crlf, 'Content-Type: text/plain', self crlf, self crlf, 'first', self crlf,
		'--', b, self crlf, 'Content-Disposition: form-data; name="_file-1"; filename="two.txt"', self crlf, 'Content-Type: text/plain', self crlf, self crlf, 'second one', self crlf,
		'--', b, '--', self crlf.
	reply := JSONObject fromString: ((self postRaw: body contentType: 'multipart/form-data; boundary=', b) at: 3).
	self assert: (reply booleanAt: '_Success').
	self assert: (reply integerAt: 'count') equals: 2.
	names := reply arrayAt: 'names'.
	self assert: (names at: 1) equals: 'one.txt'.
	self assert: (names at: 2) equals: 'two.txt'.
	self assert: ((reply arrayAt: 'sizes') at: 2) equals: 10.
	self assert: (reply stringAt: 'note') equals: 'a note'.
	self assert: (reply booleanAt: 'flag').
	self assert: (reply integerAt: 'n') equals: 42
]

{ #category : 'tests' }
RestServerTest >> testABinaryReply [
	| injson reply body cut json |
	server start.
	injson := JSONObject new at: '_class' put: 'services.Binary'; at: '_method' put: 'download'; yourself.
	reply := self postRaw: injson asJsonString contentType: 'application/json'.
	self assert: ((reply at: 2) at: 'content-type') equals: 'application/octet-stream'.
	body := reply at: 3.
	cut := body indexOf: (Character value: 3).
	self assert: cut > 0.
	json := JSONObject fromString: (body copyFrom: 1 to: cut - 1).
	self assert: (json stringAt: 'name') equals: 'x.bin'.
	self assert: (json booleanAt: '_Success').
	self assert: (body copyFrom: cut + 1 to: body size) equals: 'BINARY', (String with: (Character value: 0) with: (Character value: 255)), 'BYTES'
]

{ #category : 'tests' }
RestServerTest >> testEditingAServiceReloadsIt [
	| reply |
	self writeScratchAnswering: 1.
	server start.
	reply := self call: 'services.Scratch' method: 'answer'.
	self assert: (reply integerAt: 'answer') equals: 1.
	self writeScratchAnswering: 22.
	reply := self call: 'services.Scratch' method: 'answer'.
	self assert: (reply integerAt: 'answer') equals: 22
]

{ #category : 'private' }
RestServerTest >> writeScratchAnswering: anInteger [
	| stream cr |
	cr := String with: Character cr.
	stream := (Disk directoryNamed: 'tests/rest-backend/services') textFile: 'Scratch.class.st'.
	[stream nextPutAll: 'Class { #name : ''Scratch'', #superclass : ''RestService'', #category : ''services'' }', cr,
		'Scratch >> answer [', cr, '	outjson at: ''answer'' put: ', anInteger printString, cr, ']', cr; shorten]
		ensure: [stream close]
]

{ #category : 'tests' }
RestServerTest >> testSixteenCallsAtOnce [
	| promises |
	server start.
	promises := (1 to: 16) collect: [:i |
		Processor forkParallel: [
			(self call: 'services.Adder' method: 'addNumbers' with: [:j | j at: 'num1' put: i; at: 'num2' put: i * 100]) integerAt: 'num3']].
	promises doWithIndex: [:p :i | self assert: p value equals: i + (i * 100)]
]

{ #category : 'tests' }
RestServerTest >> testOnlyPostReachesRest [
	| socket in reply |
	server start.
	socket := Socket connectTo: '127.0.0.1' port: server port.
	[socket send: 'GET /rest HTTP/1.1', self crlf, 'Host: t', self crlf, 'Connection: close', self crlf, self crlf.
	 in := SocketStream on: socket.
	 in readTimeout: 5000.
	 reply := self readResponseFrom: in] ensure: [socket close].
	self assert: (reply at: 1) equals: 'HTTP/1.1 405 Method Not Allowed'
]

{ #category : 'tests' }
RestServerTest >> testInitRunsBeforeListening [
	"A back end with an Init class is told init: before the listener opens
	 and init2: -- with nil, there being no database -- before the first
	 request; a back end without one, which tests/rest-backend is, is told
	 nothing."
	| s |
	Smalltalk at: #RestInitSeen put: nil.
	server start.
	self assert: (Smalltalk at: #RestInitSeen) isNil.
	s := RestServer new
		port: 0; bindAddress: '127.0.0.1'; backendDirectory: 'tests/rest-init-backend'; name: 'init';
		yourself.
	[s start.
	 self assert: (Smalltalk at: #RestInitSeen) first == s.
	 self deny: (Smalltalk at: #RestInitSeen) last.
	 self assert: (Smalltalk at: #RestInit2Seen) isNil.
	 self assert: s isRunning]
		ensure: [s stop]
]

{ #category : 'tests' }
RestServerTest >> testInitFailureStopsTheStart [
	"An application that could not initialise is not listening."
	| s |
	s := RestServer new
		port: 0; bindAddress: '127.0.0.1'; backendDirectory: 'tests/rest-init-backend'; name: 'init';
		yourself.
	s environmentAt: 'initMode' put: 'fail'.
	self should: [s start] raise: Error.
	self deny: s isRunning.
	self assert: s httpServer isNil
]

{ #category : 'tests' }
RestServerTest >> testConfiguration [
	| config s |
	config := JSONObject fromString: '{"port": 0, "bindAddress": "127.0.0.1", "backendDirectory": "tests/rest-backend",
		"requireAuthentication": true, "userInactiveSeconds": 99, "keepAliveSeconds": 2,
		"allowWithoutAuthentication": ["services.Adder:addNumbers"], "custom": "value"}'.
	s := RestServer fromConfig: config.
	self assert: s requiresAuthentication.
	self assert: (s allowsWithoutAuthentication: 'services.Adder' method: 'addNumbers').
	self deny: (s allowsWithoutAuthentication: 'services.Adder' method: 'whoAmI').
	self assert: (s environmentAt: 'custom') equals: 'value'.
	self assert: s userCache inactiveSeconds equals: 99.
	self assert: s backendDirectory equals: 'tests/rest-backend'
]

{ #category : 'tests' }
RestServerTest >> testTheUserCache [
	| cache user gone |
	cache := RestUserCache new inactiveSeconds: 0; yourself.
	user := cache newUser: 'x' password: 'y' userId: 3.
	self assert: (cache findUser: user uuid) == user.
	self assert: (cache findUser: 'nope') isNil.
	self assert: (cache findUser: nil) isNil.
	gone := OrderedCollection new.
	cache logoutHandler: [:u | gone add: u].
	(Delay forMilliseconds: 1100) wait.
	self assert: cache purgeOld equals: 1.
	self assert: cache size equals: 0.
	self assert: gone size equals: 1.
	self assert: (RestUuid new) size equals: 36.
	self deny: RestUuid new equals: RestUuid new
]

"
The server, over the loopback interface: a client written on the raw socket
sends HTTP by hand and reads what comes back, so what is tested is the wire
and not a client library's idea of it.

Each test starts its own server on a port the system picks and stops it in an
ensure:.  The static files come from the directory the tests run in, through
a page this test writes and removes.
"
Class {
	#name : 'HttpServerTest',
	#superclass : 'TestCase',
	#instVars : [ 'server', 'page' ],
	#category : 'JSON-RPC-Server-Tests'
}

{ #category : 'running' }
HttpServerTest >> setUp [
	page := 'http-server-test-page.html'.
	server := HttpServer new port: 0; bindAddress: '127.0.0.1'; name: 'test'; yourself
]

{ #category : 'running' }
HttpServerTest >> tearDown [
	server notNil ifTrue: [server stop].
	(Disk includesKey: page) ifTrue: [Disk removeKey: page]
]

{ #category : 'private' }
HttpServerTest >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
HttpServerTest >> connect [
	^Socket connectTo: '127.0.0.1' port: server port
]

{ #category : 'private' }
HttpServerTest >> send: aString on: aSocket [
	"A request with its line ends, and the reply as text, whole."
	| in |
	aSocket send: aString.
	in := SocketStream on: aSocket.
	in readTimeout: 5000.
	^self readResponseFrom: in
]

{ #category : 'private' }
HttpServerTest >> readResponseFrom: in [
	"One response: the head up to the blank line, then Content-Length
	 bytes of body.  Answers an Array of the status line, the headers as
	 a Dictionary, and the body."
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
HttpServerTest >> get: aPath [
	| socket reply |
	socket := self connect.
	^[reply := self send: 'GET ', aPath, ' HTTP/1.1', self crlf, 'Host: test', self crlf, 'Connection: close', self crlf, self crlf on: socket.
	  reply] ensure: [socket close]
]

{ #category : 'private' }
HttpServerTest >> post: aPath body: aBody [
	| socket |
	socket := self connect.
	^[self send: 'POST ', aPath, ' HTTP/1.1', self crlf, 'Host: test', self crlf,
		'Content-Type: application/json', self crlf,
		'Content-Length: ', aBody size printString, self crlf, 'Connection: close', self crlf, self crlf, aBody
		on: socket] ensure: [socket close]
]

{ #category : 'tests' }
HttpServerTest >> testAHandlerAnswers [
	| reply |
	server at: '/echo' put: [:request | HttpResponse json: request body].
	server start.
	reply := self post: '/echo' body: '{"hello": "world"}'.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: ((reply at: 2) at: 'content-type') equals: 'application/json'.
	self assert: ((reply at: 2) at: 'x-content-type-options') equals: 'nosniff'.
	self assert: (reply at: 3) equals: '{"hello": "world"}'
]

{ #category : 'tests' }
HttpServerTest >> testAnObjectHandler [
	server at: '/static-answer' put: (HttpStaticFileHandler on: '.').
	server start.
	self assert: ((self get: '/static-answer') at: 1) equals: 'HTTP/1.1 404 Not Found'
]

{ #category : 'tests' }
HttpServerTest >> testNotFound [
	server start.
	self assert: ((self get: '/nothing') at: 1) equals: 'HTTP/1.1 404 Not Found'
]

{ #category : 'tests' }
HttpServerTest >> testAStaticFile [
	| stream reply |
	stream := Disk textFile: page.
	[stream nextPutAll: '<html><body>hello</body></html>'; shorten] ensure: [stream close].
	server documentRoot: '.'.
	server start.
	reply := self get: '/', page.
	self assert: (reply at: 1) equals: 'HTTP/1.1 200 OK'.
	self assert: ((reply at: 2) at: 'content-type') equals: 'text/html'.
	self assert: (reply at: 3) equals: '<html><body>hello</body></html>'.
	self assert: ((self get: '/no-such-file.html') at: 1) equals: 'HTTP/1.1 404 Not Found'.
	self assert: ((self get: '/../', page) at: 1) equals: 'HTTP/1.1 403 Forbidden'
]

{ #category : 'tests' }
HttpServerTest >> testAHandlerThatFailsIsA500 [
	server at: '/boom' put: [:request | 1 / 0].
	server start.
	self assert: ((self get: '/boom') at: 1) equals: 'HTTP/1.1 500 Internal Server Error'
]

{ #category : 'tests' }
HttpServerTest >> testAnHttpErrorFromAHandlerKeepsItsStatus [
	server at: '/secret' put: [:request | HttpError status: 403 reason: 'no'].
	server start.
	self assert: ((self get: '/secret') at: 1) equals: 'HTTP/1.1 403 Forbidden'
]

{ #category : 'tests' }
HttpServerTest >> testABadRequestIsAnswered [
	| socket reply |
	server start.
	socket := self connect.
	[reply := self send: 'GARBAGE', self crlf, self crlf on: socket] ensure: [socket close].
	self assert: (reply at: 1) equals: 'HTTP/1.1 400 Bad Request'
]

{ #category : 'tests' }
HttpServerTest >> testKeepAliveCarriesTwoRequests [
	| socket in first second |
	server at: '/n' put: [:request | HttpResponse text: (request parameterAt: 'v')].
	server start.
	socket := self connect.
	[socket send: 'GET /n?v=one HTTP/1.1', self crlf, 'Host: t', self crlf, self crlf.
	 in := SocketStream on: socket.
	 in readTimeout: 5000.
	 first := self readResponseFrom: in.
	 socket send: 'GET /n?v=two HTTP/1.1', self crlf, 'Host: t', self crlf, 'Connection: close', self crlf, self crlf.
	 second := self readResponseFrom: in]
		ensure: [socket close].
	self assert: (first at: 3) equals: 'one'.
	self assert: ((first at: 2) at: 'connection') equals: 'keep-alive'.
	self assert: (second at: 3) equals: 'two'.
	self assert: ((second at: 2) at: 'connection') equals: 'close'
]

{ #category : 'tests' }
HttpServerTest >> testAnIdleConnectionIsClosedAfterTheKeepAliveTime [
	| socket in |
	server keepAliveSeconds: 0.2.
	server start.
	socket := self connect.
	in := SocketStream on: socket.
	in readTimeout: 5000.
	"Nothing sent: the server gives up on us and closes; we read end of stream."
	[self assert: in next isNil] ensure: [socket close]
]

{ #category : 'tests' }
HttpServerTest >> testTenConnectionsAtOnce [
	"Ten clients, each on its own process, each getting its own answer."
	| promises |
	server at: '/who' put: [:request | HttpResponse text: (request parameterAt: 'i')].
	server start.
	promises := (1 to: 10) collect: [:i |
		Processor forkParallel: [(self get: '/who?i=', i printString) at: 3]].
	promises doWithIndex: [:p :i | self assert: p value equals: i printString].
	"Every connection closed and was counted out."
	(Delay forMilliseconds: 100) wait.
	self assert: server connectionCount equals: 0
]

{ #category : 'tests' }
HttpServerTest >> testABodyOverTheLimitIsRefusedNotRead [
	| reply |
	server maxRequestBytes: 100.
	server at: '/take' put: [:request | HttpResponse text: 'took'].
	server start.
	reply := self post: '/take' body: (String new: 500 withAll: $z).
	self assert: (reply at: 1) equals: 'HTTP/1.1 413 Payload Too Large'
]

{ #category : 'tests' }
HttpServerTest >> testStartAndStop [
	server start.
	self assert: server isRunning.
	self assert: server port > 0.
	server stop.
	self deny: server isRunning.
	self should: [self connect] raise: NetError
]

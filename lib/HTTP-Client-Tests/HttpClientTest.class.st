"
The client against a real server on the loopback interface -- HttpServer
for the ordinary replies, and a listener of the test's own answering one
canned reply for the kinds HttpServer never sends: chunked, no length,
HTTP/1.0 -- so that what is tested is the wire and not a stream of text.

TLS is tested as far as it can be without a TLS server, which this system
has not got: an https URL to a listener that answers in plain text is
refused with OpenSSL's reason, or by name on a build without OpenSSL.
The real thing, a certificate the system's store vouches for, is in
lib/HTTP-Client-Live-Tests, in the internet-live profile.
"
Class {
	#name : 'HttpClientTest',
	#superclass : 'TestCase',
	#instVars : [ 'server' ],
	#category : 'HTTP-Client-Tests'
}

{ #category : 'running' }
HttpClientTest >> tearDown [
	server notNil ifTrue: [server stop]
]

{ #category : 'private' }
HttpClientTest >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
HttpClientTest >> startServer [
	server := HttpServer new port: 0; bindAddress: '127.0.0.1'; name: 'client-test'; yourself.
	server at: '/hello' put: [:request | HttpResponse text: 'hello, ', request method].
	server at: '/echo' put: [:request | HttpResponse json: request body].
	server at: '/slow' put: [:request | (Delay forMilliseconds: 1500) wait. HttpResponse text: 'late'].
	server documentRoot: 'tests/rest-backend'.
	server start.
	^'http://127.0.0.1:', server port printString
]

{ #category : 'private' }
HttpClientTest >> serving: replyText do: aBlock [
	"A listener that answers the first connection's request with replyText
	 and closes; aBlock gets the URL to call."
	| listener |
	listener := ServerSocket listenOn: 0.
	[[:l | | c in |
		c := l accept.
		in := SocketStream on: c.
		[in nextLine isEmpty] whileFalse: [].
		c send: replyText.
		c close] value: listener] fork.
	^[aBlock value: 'http://127.0.0.1:', listener localPort printString] ensure: [listener close]
]

{ #category : 'tests - server' }
HttpClientTest >> testAGet [
	| base reply |
	base := self startServer.
	reply := HttpClient get: base, '/hello'.
	self assert: reply status equals: 200.
	self assert: reply isSuccess.
	self assert: reply reason equals: 'OK'.
	self assert: reply body equals: 'hello, GET'.
	self assert: ((reply headerAt: 'Content-Type') startsWith: 'text/plain')
]

{ #category : 'tests - server' }
HttpClientTest >> testAPost [
	| base reply |
	base := self startServer.
	reply := HttpClient post: base, '/echo' body: '{"a": 1}' contentType: 'application/json'.
	self assert: reply status equals: 200.
	self assert: (reply json integerAt: 'a') equals: 1
]

{ #category : 'tests - server' }
HttpClientTest >> testAStaticFile [
	| base reply |
	base := self startServer.
	reply := HttpClient get: base, '/README.md'.
	self assert: reply status equals: 200.
	self assert: reply body size > 0
]

{ #category : 'tests - server' }
HttpClientTest >> testNotFoundIsAReplyNotAnError [
	| base reply |
	base := self startServer.
	reply := HttpClient get: base, '/nothing'.
	self assert: reply status equals: 404.
	self deny: reply isSuccess.
	self assert: reply body size > 0
]

{ #category : 'tests - server' }
HttpClientTest >> testARefusedConnectionIsANetError [
	self should: [HttpClient get: 'http://127.0.0.1:1/'] raise: NetError
]

{ #category : 'tests - server' }
HttpClientTest >> testTlsToAPlainPeerIsRefused [
	"An https URL to a listener that speaks plain text.  The listener
	 answers without reading -- the client's first bytes are a TLS hello
	 with no line in it -- and the client refuses the answer: `wrong
	 version number' from OpenSSL, or `no TLS' from a build without it.
	 Either way a NetError, and never a connection that pretends."
	| listener error |
	listener := ServerSocket listenOn: 0.
	[[:l | | c |
		c := l accept.
		c send: 'HTTP/1.1 200 OK', self crlf, self crlf.
		c close] value: listener] fork.
	error := [[HttpClient new readTimeout: 5000; get: 'https://127.0.0.1:', listener localPort printString, '/'. nil]
		on: NetError do: [:e | e]] ensure: [listener close].
	self assert: error notNil.
	self assert: (error messageText includesSubstring: 'TLS').
	self should: [HttpClient get: 'https://127.0.0.1:1/'] raise: NetError
]

{ #category : 'tests - server' }
HttpClientTest >> testTheWaitIsBounded [
	| base |
	base := self startServer.
	self should: [HttpClient new readTimeout: 200; get: base, '/slow'] raise: TimedOut
]

{ #category : 'tests - server' }
HttpClientTest >> testAHeaderOfTheCallers [
	| base reply |
	base := self startServer.
	server at: '/auth' put: [:request | HttpResponse text: (request headerAt: 'authorization')].
	reply := HttpClient new headerAt: 'Authorization' put: 'Bearer x'; get: base, '/auth'.
	self assert: reply body equals: 'Bearer x'
]

{ #category : 'tests - bodies' }
HttpClientTest >> testAChunkedBody [
	"Hex sizes, an extension, the trailers: what Go's net/http sends."
	| reply |
	reply := self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Type: application/json', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'5', self crlf, 'hello', self crlf,
			'7;ext=1', self crlf, ', world', self crlf,
			'A', self crlf, ' and more!', self crlf,
			'0', self crlf, 'X-Trailer: yes', self crlf, self crlf
		do: [:url | HttpClient get: url].
	self assert: reply status equals: 200.
	self assert: reply body equals: 'hello, world and more!'.
	self assert: (reply headerAt: 'content-type') equals: 'application/json'
]

{ #category : 'tests - bodies' }
HttpClientTest >> testABodyThatEndsWithTheConnection [
	"HTTP/1.0, no length: everything up to the close."
	| reply |
	reply := self serving: 'HTTP/1.0 200 OK', self crlf, 'Content-Type: text/plain', self crlf, self crlf, 'all of it'
		do: [:url | HttpClient get: url].
	self assert: reply version equals: 'HTTP/1.0'.
	self assert: reply body equals: 'all of it'
]

{ #category : 'tests - bodies' }
HttpClientTest >> testAStreamedBodyLineByLine [
	"An event stream, chunked as a server chunks -- by its buffer and not
	 by the line, so a line crosses a chunk boundary -- read by the line
	 as it comes: the blank line between events is a line, the end is nil."
	| lines |
	lines := self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Type: text/event-stream', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'D', self crlf, 'data: one', (String with: (Character value: 10)), (String with: (Character value: 10)), 'da', self crlf,
			'9', self crlf, 'ta: two', (String with: (Character value: 10)), 'e', self crlf,
			'4', self crlf, 'nd', self crlf, self crlf,
			'0', self crlf, self crlf
		do: [:url | HttpClient new get: url do: [:reply | | got |
			got := OrderedCollection new.
			self assert: reply status equals: 200.
			self assert: reply body isNil not.
			got]].
	"The body was taken whole there; now by the line."
	lines := self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Type: text/event-stream', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'D', self crlf, 'data: one', (String with: (Character value: 10)), (String with: (Character value: 10)), 'da', self crlf,
			'9', self crlf, 'ta: two', (String with: (Character value: 10)), 'e', self crlf,
			'4', self crlf, 'nd', self crlf, self crlf,
			'0', self crlf, self crlf
		do: [:url | HttpClient new get: url do: [:reply | | got |
			got := OrderedCollection new.
			reply bodyStream linesDo: [:line | got add: line].
			self assert: reply bodyStream nextLine isNil.
			got asArray]].
	self assert: lines equals: #('data: one' '' 'data: two' 'end').
	"By the length, and to the close, the same way."
	lines := self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 8', self crlf, self crlf, 'ab', self crlf, 'cd', self crlf
		do: [:url | HttpClient new get: url do: [:reply | | got |
			got := OrderedCollection new.
			reply bodyStream linesDo: [:line | got add: line].
			got asArray]].
	self assert: lines equals: #('ab' 'cd').
	lines := self serving: 'HTTP/1.0 200 OK', self crlf, self crlf, 'to the', (String with: (Character value: 10)), 'close'
		do: [:url | HttpClient new get: url do: [:reply | | got |
			got := OrderedCollection new.
			reply bodyStream linesDo: [:line | got add: line].
			got asArray]].
	self assert: lines equals: #('to the' 'close')
]

{ #category : 'tests - bodies' }
HttpClientTest >> testTheBodyWaitsForTheBlock [
	"In the do: form the status and headers are read and the body is not:
	 a block that asks for the status alone never reads it, and one that
	 asks for the body gets it whole."
	| status body |
	status := self serving: 'HTTP/1.1 201 Created', self crlf, 'Content-Length: 5', self crlf, self crlf, 'hello'
		do: [:url | HttpClient new get: url do: [:reply | reply status]].
	self assert: status equals: 201.
	body := self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 5', self crlf, self crlf, 'hello'
		do: [:url | HttpClient new get: url do: [:reply | reply body]].
	self assert: body equals: 'hello'
]

{ #category : 'tests - bodies' }
HttpClientTest >> testNoContentHasNoBody [
	| reply |
	reply := self serving: 'HTTP/1.1 204 No Content', self crlf, self crlf
		do: [:url | HttpClient get: url].
	self assert: reply status equals: 204.
	self assert: reply body equals: ''
]

{ #category : 'tests - bodies' }
HttpClientTest >> testNotHttpIsANetError [
	self should: [self serving: 'SMTP is not HTTP', self crlf do: [:url | HttpClient get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf, 'Transfer-Encoding: chunked', self crlf, self crlf, 'zz', self crlf
		do: [:url | HttpClient get: url]] raise: NetError
]

{ #category : 'tests - urls' }
HttpClientTest >> testUrls [
	| url |
	url := HttpUrl fromString: 'http://localhost:11434/api/tags'.
	self assert: url scheme equals: 'http'.
	self assert: url host equals: 'localhost'.
	self assert: url port equals: 11434.
	self assert: url path equals: '/api/tags'.
	self assert: url hostHeader equals: 'localhost:11434'.
	self deny: url isSecure.
	url := HttpUrl fromString: 'HTTP://Example.com'.
	self assert: url host equals: 'Example.com'.
	self assert: url port equals: 80.
	self assert: url path equals: '/'.
	self assert: url hostHeader equals: 'Example.com'.
	url := HttpUrl fromString: 'https://example.com/x?y=1'.
	self assert: url isSecure.
	self assert: url port equals: 443.
	self assert: url path equals: '/x?y=1'.
	url := HttpUrl fromString: 'http://[::1]:8080/'.
	self assert: url host equals: '[::1]'.
	self assert: url port equals: 8080.
	self should: [HttpUrl fromString: 'localhost:8080'] raise: NetError.
	self should: [HttpUrl fromString: 'http:///x'] raise: NetError.
	self should: [HttpUrl fromString: 'http://host:notaport/'] raise: NetError
]

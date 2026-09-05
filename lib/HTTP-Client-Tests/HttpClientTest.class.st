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
	 and closes; aBlock gets the URL to call.

	 THE FORKED PROCESS CATCHES ITS OWN ERRORS, as LLMTestCase's does.  The
	 block need not send a request at all -- a client that refuses to write
	 one raises before any bytes go out (NET-3) -- and then this process is
	 waiting in accept when the ensure: below closes the listener under it.
	 An unhandled error in a forked process is REPORTED AND RESUMED here,
	 so accept answered a Socket with no handle, every read on it failed
	 and was resumed with nil, and the reader walked off the end of its own
	 buffer for ever: one test with no listener left the whole suite
	 spinning.  A listener that is taken away is this process's ordinary
	 end and is nothing to report."
	| listener |
	listener := ServerSocket listenOn: 0.
	[[[:l | | c in |
		c := l accept.
		in := SocketStream on: c.
		[in nextLine isEmpty] whileFalse: [].
		c send: replyText.
		c close] value: listener]
			on: Error do: [:e | nil]] fork.
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

{ #category : 'tests - bodies' }
HttpClientTest >> testABogusChunkSizeIsANetError [
	"Bugs3 B57, lower.  `ffffffffffffffff' is a valid hexadecimal number
	 and 2^64-1, and it reached `String new:' -- so a peer that mis-framed
	 its reply produced `a primitive has failed', a message about this
	 image rather than about the other end.  A size that cannot be a size
	 is a NetError, as one that is not a number already was."
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'ffffffffffffffff', self crlf, 'hello', self crlf
		do: [:url | HttpClient get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'fffffffffffffffff0', self crlf, 'hello', self crlf
		do: [:url | HttpClient get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'zz', self crlf, 'hello', self crlf
		do: [:url | HttpClient get: url]] raise: NetError
]

{ #category : 'tests - bodies' }
HttpClientTest >> testABodyOverTheLimitIsANetError [
	"The reply comes from a server this program did not write, and until
	 Bugs3 B57 (lower) there was no size at which it stopped being read.
	 The declared length is refused before the bytes are read; a chunked
	 body, which declares nothing, is refused as its total passes the
	 limit; and so is one that ends with the connection."
	| reply |
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Content-Length: 100000000000', self crlf, self crlf
		do: [:url | HttpClient get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 10', self crlf, self crlf, 'helloworld'
		do: [:url | HttpClient new maxBodyBytes: 5; get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'5', self crlf, 'hello', self crlf, '5', self crlf, 'world', self crlf, '0', self crlf, self crlf
		do: [:url | HttpClient new maxBodyBytes: 6; get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.0 200 OK', self crlf, self crlf, 'far too much of it'
		do: [:url | HttpClient new maxBodyBytes: 5; get: url]] raise: NetError.
	"a Content-Length that is not a number is not a length"
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 12x', self crlf, self crlf, 'hello'
		do: [:url | HttpClient get: url]] raise: NetError.
	"and the ordinary reply is unaffected, at the limit and under it"
	reply := self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 5', self crlf, self crlf, 'hello'
		do: [:url | HttpClient new maxBodyBytes: 5; get: url].
	self assert: reply body equals: 'hello'.
	self assert: HttpClient new maxBodyBytes equals: 64 * 1024 * 1024
]

{ #category : 'private' }
HttpClientTest >> drippingDo: aBlock [
	"A listener that promises a long body and then sends one byte every
	 fiftieth of a second for as long as anybody is listening.  Nothing about
	 it is idle, which is the point: every individual read succeeds."
	| listener |
	listener := ServerSocket listenOn: 0.
	[[[:l | | c in |
		c := l accept.
		in := SocketStream on: c.
		[in nextLine isEmpty] whileFalse: [].
		c send: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 1000000', self crlf, self crlf.
		[c isOpen] whileTrue: [
			c send: 'x'.
			(Delay forMilliseconds: 20) wait].
		c close] value: listener]
			on: Error do: [:e | nil]] fork.
	^[aBlock value: 'http://127.0.0.1:', listener localPort printString] ensure: [listener close]
]

{ #category : 'tests - refusals' }
HttpClientTest >> testAHeaderWithALineEndingIsRefused [
	"Bugs4 NET-3.  Bugs3 B54 fixed this on the server side; the client
	 wrote `key, ': ', value' with nothing looked at, so a caller putting
	 anything a stranger chose into a header value sent two headers where
	 it wrote one -- and, past a blank line, a body of the stranger's too.
	 Refused at the door and again at the wire, since `headers' is a
	 collection a caller can reach."
	| crlf client |
	crlf := self crlf.
	client := HttpClient new.
	self should: [client headerAt: 'X-Evil' put: 'a', crlf, 'Injected: yes'] raise: Error.
	self should: [client headerAt: 'X-Evil' put: 'a', (String with: (Character value: 10))] raise: Error.
	self should: [client headerAt: 'X-Evil' put: 'a', (String with: (Character value: 0))] raise: Error.
	self should: [client headerAt: 'X Evil' put: 'a'] raise: Error.
	self should: [client headerAt: 'X:Evil' put: 'a'] raise: Error.
	self should: [client headerAt: '' put: 'a'] raise: Error.
	"The ordinary header is unaffected, and is the one that goes out."
	client headerAt: 'X-Fine' put: 'a value'.
	self assert: client headers size equals: 1.
	"A header put straight into the collection is caught at the wire, since
	 a check only at the front door is a check somebody walks around."
	client headers add: 'X-Late' -> ('b', crlf, 'Injected: yes').
	self should: [self serving: 'HTTP/1.1 200 OK', crlf, crlf do: [:url | client get: url]] raise: Error
]

{ #category : 'tests - refusals' }
HttpClientTest >> testARequestLineWithALineEndingIsRefused [
	"The other half of NET-3: the method and the target go out ahead of
	 every header, so a line ending in either injects headers just as one
	 in a header value does, and a space injects a request line.  The URL
	 refuses it first -- see HttpUrl -- and the client checks again."
	| crlf |
	crlf := self crlf.
	self should: [HttpUrl fromString: 'http://h/a', crlf, 'X: y'] raise: NetError.
	self should: [HttpUrl fromString: 'http://h/a b'] raise: NetError.
	self should: [HttpUrl fromString: 'http://h', crlf, '/a'] raise: NetError.
	self should: [HttpClient new
		request: 'GET', crlf, 'X: y' url: 'http://127.0.0.1:1/' body: nil contentType: nil]
		raise: Error.
	self should: [HttpClient new
		request: 'GET /x HTTP/1.1' url: 'http://127.0.0.1:1/' body: nil contentType: nil]
		raise: Error
]

{ #category : 'tests - bodies' }
HttpClientTest >> testAShortBodyIsANetError [
	"Bugs4 NET-4.  A peer that declares ten bytes and closes after five
	 answered 200 with five bytes and no sign that anything was missing:
	 a truncated reply that reads exactly like a complete short one.
	 Chunked truncation was already caught; this is the same rule on the
	 other framing, whole and by the line."
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 10', self crlf, self crlf, 'hello'
		do: [:url | HttpClient get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 10', self crlf, self crlf, 'hello'
		do: [:url | HttpClient new get: url do: [:reply | reply bodyStream linesDo: [:line | ]]]] raise: NetError.
	"A chunked body cut off mid-chunk says the connection closed, and not
	 that the chunk size was malformed -- which is what it used to say."
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Transfer-Encoding: chunked', self crlf, self crlf,
			'5', self crlf, 'hello', self crlf
		do: [:url | HttpClient get: url]] raise: NetError.
	"and the whole body of the declared length is still not an error"
	self assert: (self serving: 'HTTP/1.1 200 OK', self crlf, 'Content-Length: 5', self crlf, self crlf, 'hello'
		do: [:url | HttpClient get: url]) body equals: 'hello'
]

{ #category : 'tests - bodies' }
HttpClientTest >> testInterimResponsesAreSkipped [
	"Bugs4 NET-5.  A 1xx says the answer is coming; this took the first
	 status line it saw as final, so `103 Early Hints' -- which real
	 content delivery networks send unasked -- was answered as a 103 with
	 an empty body and the 200 behind it was never read."
	| reply |
	reply := self serving: 'HTTP/1.1 103 Early Hints', self crlf,
			'Link: </s.css>; rel=preload', self crlf, self crlf,
			'HTTP/1.1 200 OK', self crlf, 'Content-Length: 5', self crlf, self crlf, 'hello'
		do: [:url | HttpClient get: url].
	self assert: reply status equals: 200.
	self assert: reply body equals: 'hello'.
	self assert: (reply headerAt: 'link') isNil.
	"100 Continue, and two of them, the same way."
	reply := self serving: 'HTTP/1.1 100 Continue', self crlf, self crlf,
			'HTTP/1.1 100 Continue', self crlf, self crlf,
			'HTTP/1.1 201 Created', self crlf, 'Content-Length: 2', self crlf, self crlf, 'ok'
		do: [:url | HttpClient post: url body: 'x' contentType: 'text/plain'].
	self assert: reply status equals: 201.
	self assert: reply body equals: 'ok'.
	"A peer that sends nothing but interim responses is bounded: a client
	 that skipped them without counting would be held open by one."
	self should: [self serving: ((1 to: 12) inject: '' into: [:text :each |
			text, 'HTTP/1.1 100 Continue', self crlf, self crlf])
		do: [:url | HttpClient get: url]] raise: NetError
]

{ #category : 'tests - bodies' }
HttpClientTest >> testAmbiguousFramingIsRefused [
	"Bugs4 NET-9.  RFC 7230 requires a message framed two ways to be
	 refused rather than resolved in favour of one, because whatever is
	 between this client and the origin may resolve it in favour of the
	 other -- which is the whole of response smuggling.  Two conflicting
	 Content-Lengths were kept last-wins, and chunked with a
	 Content-Length preferred chunked.  The server side has been strict
	 since Bugs3 B56."
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Content-Length: 5', self crlf, 'Content-Length: 3', self crlf, self crlf, 'hello'
		do: [:url | HttpClient get: url]] raise: NetError.
	self should: [self serving: 'HTTP/1.1 200 OK', self crlf,
			'Transfer-Encoding: chunked', self crlf, 'Content-Length: 5', self crlf, self crlf,
			'5', self crlf, 'hello', self crlf, '0', self crlf, self crlf
		do: [:url | HttpClient get: url]] raise: NetError.
	"The same length written twice is one framing, and is read."
	self assert: (self serving: 'HTTP/1.1 200 OK', self crlf,
			'Content-Length: 5', self crlf, 'Content-Length: 5', self crlf, self crlf, 'hello'
		do: [:url | HttpClient get: url]) body equals: 'hello'.
	"A repeated header that says nothing about framing is still last-wins:
	 the rule is about framing and not about repetition."
	self assert: ((self serving: 'HTTP/1.1 200 OK', self crlf,
			'X-Note: one', self crlf, 'X-Note: two', self crlf,
			'Content-Length: 5', self crlf, self crlf, 'hello'
		do: [:url | HttpClient get: url]) headerAt: 'x-note') equals: 'two'
]

{ #category : 'tests - timeouts' }
HttpClientTest >> testASlowDripMeetsTheOverallDeadline [
	"Bugs4.  readTimeout: bounds ONE read, and a peer sending a byte every
	 fifth of a second is never idle: every read met a five-second timeout
	 and the exchange had no bound at all but the 64 MB body cap, which at
	 that rate is about ten years of one process.  totalTimeout: is the
	 bound on the whole exchange, and defaults to ten times readTimeout:."
	self assert: (HttpClient new readTimeout: 1000; yourself) totalTimeout equals: 10000.
	self assert: (HttpClient new totalTimeout: 42; yourself) totalTimeout equals: 42.
	self should: [self drippingDo: [:url |
		HttpClient new readTimeout: 5000; totalTimeout: 200; get: url]] raise: TimedOut
]

{ #category : 'tests - timeouts' }
HttpClientTest >> testTheConnectHasItsOwnDeadline [
	"Bugs4 NET-8.  readTimeout: covered only reads, so a connection to an
	 address that is routed nowhere parked the process for the operating
	 system's own patience -- about two minutes -- with nothing read and
	 no timeout in sight.  The connect is non-blocking already and was
	 finished by an unbounded wait for writable; the wait is bounded now.
	 Thirty seconds unless the caller says otherwise."
	| listener |
	self assert: HttpClient new connectTimeout equals: 30000.
	self assert: (HttpClient new connectTimeout: 250; yourself) connectTimeout equals: 250.
	"A refusal is still a NetError and still immediate, deadline or not."
	self should: [Socket connectTo: '127.0.0.1' port: 1 timeout: 10000] raise: NetError.
	"and an ordinary connection is made well inside one"
	listener := ServerSocket listenOn: 0 address: '127.0.0.1' backlog: 4.
	[ | socket |
	  socket := Socket connectTo: '127.0.0.1' port: listener localPort timeout: 10000.
	  self assert: socket isOpen.
	  socket close] ensure: [listener close]
]

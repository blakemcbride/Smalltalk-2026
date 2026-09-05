"
An HTTP client with what a program calling a service needs and no more: a
GET or a POST to a URL, the reply as an HttpClientResponse, and the same
two with a block, for a reply to be read as it arrives.

    (HttpClient get: 'http://localhost:11434/api/tags') json
    (HttpClient post: url body: aJsonString contentType: 'application/json') body

    HttpClient new readTimeout: 60000; headerAt: 'Authorization' put: '...'; get: url

    HttpClient new post: url body: json contentType: 'application/json' do: [:reply |
        reply bodyStream linesDo: [:line | ...]]

WHAT IT IS FOR.  Kiss's RestClient, as far as lib/LLM needs it: a local
Ollama, and the https APIs of the language-model services, which stream
their answers a token at a time.  It is the other half of
lib/JSON-RPC-Server -- the same HttpCodec, the same SocketStream -- and no
part of the server.

TLS.  An https URL connects and then sends startTls: to the socket, which
checks the certificate against the system's store and its name against
the host, and raises a NetError with OpenSSL's reason when either is
wrong.  A build without OpenSSL refuses https by name, in the same
NetError.  The server side of TLS is not here and is not going to be: a
server on this system sits behind a reverse proxy that speaks it.

ONE REQUEST PER CONNECTION.  Every request opens a connection, sends
`Connection: close', reads the whole reply and closes.  That costs a
handshake per call and buys the simplest correct reader there is: a reply
with no Content-Length, which HTTP/1.0 servers send and an HTTP/1.1 one
may, ends when the connection does, and nothing has to be kept alive or
recovered.  A caller making thousands of calls a second wants the other
design; nobody here does.

THE BODY IS READ WHOLE by get: and post:, three ways: Content-Length
bytes, or the chunks of a chunked transfer -- Go's net/http, which Ollama
is, chunks anything past its buffer, so a model's answer always arrives
that way -- or up to the close.  A reply that cannot have a body (1xx,
204, 304) is not read for one.  The do: forms read the status and the
headers and then hand the reply to the block with its body still on the
wire, as `reply bodyStream'; the block reads what it wants, by the line,
and the connection is closed when the block is done, read or not.

WHAT BLOCKS.  Nothing, on a worker: the socket layer's rule (doc/NETWORK.md)
holds here as it does on the server, so a request waiting a minute for a
model to think parks its process and costs the pool no thread.  The wait
is bounded by readTimeout: -- thirty seconds unless told otherwise --
and runs out as a TimedOut.  A host that cannot be reached or refuses is a
NetError with the system's words; DNS is the one call that does block the
worker, inside getaddrinfo, as everywhere on this system.

THERE ARE THREE TIMEOUTS, because one is not enough.  connectTimeout:
bounds the connection itself: without it a route that black-holes packets
parked the process for the operating system's own patience, which is about
two minutes, and readTimeout: never got a chance to apply because nothing
had been read (Bugs4 NET-8).  readTimeout: bounds one read.  totalTimeout:
bounds the whole exchange, and exists because a server that answers is
never idle: one byte every five seconds meets a thirty-second read timeout
for ever, and the only other limit is the 64 MB body cap.  Its default is
ten times readTimeout:.

WHAT IS SENT IS WHAT THE CALLER WROTE.  A CR, an LF or a NUL in a header
value, in a header name, in the method or in the request target is an
Error rather than a request: it would be two headers where the caller
wrote one, or two requests where it wrote one.  Bugs3 B54 made that rule
on the server side; NET-3 found this side without it.

THE BODY IS BOUNDED, at 64 MB unless maxBodyBytes: says otherwise.  The
reply comes from a server this program did not write, and until Bugs3 B57
(lower) there was no size at which it stopped being read: a
`Content-Length: 100000000000' was a hundred gigabytes of String, and a
chunked reply had no total at all.  A reply over the limit is a NetError
naming it, raised before the bytes are read where the length is declared.

NOT HERE.  Redirects, cookies, authentication: a caller who needs one
sets the header.
"
Class {
	#name : 'HttpClient',
	#superclass : 'Object',
	#instVars : [ 'readTimeout', 'headers', 'maxBodyBytes', 'connectTimeout', 'totalTimeout' ],
	#category : 'HTTP-Client'
}

{ #category : 'convenience' }
HttpClient class >> get: aUrlString [
	^self new get: aUrlString
]

{ #category : 'convenience' }
HttpClient class >> post: aUrlString body: aString contentType: aType [
	^self new post: aUrlString body: aString contentType: aType
]

{ #category : 'convenience' }
HttpClient class >> put: aUrlString body: aString contentType: aType [
	^self new put: aUrlString body: aString contentType: aType
]

{ #category : 'convenience' }
HttpClient class >> delete: aUrlString [
	^self new delete: aUrlString
]

{ #category : 'initialization' }
HttpClient >> initialize [
	readTimeout := 30000.
	connectTimeout := 30000.
	totalTimeout := nil.
	headers := OrderedCollection new.
	maxBodyBytes := HttpBodyStream defaultMaxBodyBytes
]

{ #category : 'configuration' }
HttpClient >> readTimeout: milliseconds [
	"How long to wait for any part of the reply before raising TimedOut."
	readTimeout := milliseconds
]

{ #category : 'configuration' }
HttpClient >> readTimeout [
	^readTimeout
]

{ #category : 'configuration' }
HttpClient >> connectTimeout: milliseconds [
	"How long to wait for the connection itself, before any bytes.  Thirty
	 seconds unless told otherwise; nil for the operating system's own,
	 which is minutes."
	connectTimeout := milliseconds
]

{ #category : 'configuration' }
HttpClient >> connectTimeout [
	^connectTimeout
]

{ #category : 'configuration' }
HttpClient >> totalTimeout: millisecondsOrNil [
	"How long the whole reply may take, handshake to last byte.  nil, the
	 default, means ten times readTimeout: -- see totalTimeout."
	totalTimeout := millisecondsOrNil
]

{ #category : 'configuration' }
HttpClient >> totalTimeout [
	"The deadline for the whole exchange.

	 readTimeout: alone bounds ONE READ, and a server that answers is never
	 idle: Bugs4 found that a peer dribbling one byte every five seconds
	 defeated a thirty-second read timeout completely and went on being
	 served until it had sent the 64 MB body cap, which at that rate is
	 about ten years of one process.  Every read met its deadline; the
	 exchange had none.

	 Ten times readTimeout: rather than a number of its own, so that a
	 caller who has already said how patient it is does not have to say it
	 twice -- lib/LLM waits five minutes for a token and so gets fifty for
	 a whole answer, which is far past any real one and far short of for
	 ever."
	^totalTimeout ifNil: [readTimeout isNil ifTrue: [nil] ifFalse: [readTimeout * 10]]
]

{ #category : 'configuration' }
HttpClient >> maxBodyBytes: anInteger [
	"How much of a reply's body this client will read before it raises.
	 64 MB unless told otherwise; see the class comment."
	maxBodyBytes := anInteger
]

{ #category : 'configuration' }
HttpClient >> maxBodyBytes [
	^maxBodyBytes
]

{ #category : 'configuration' }
HttpClient >> headerAt: aName put: aValue [
	"A header sent with every request this client makes.

	 A CR, an LF or a NUL in the value RAISES, and so does a name with
	 whitespace, a colon or one of those three in it.  Bugs3 B54 fixed this
	 on the SERVER side, in HttpResponse>>headerAt:put:, and gave the whole
	 argument for raising rather than stripping; Bugs4 NET-3 found the
	 client side unguarded, where the same shape is worse.  A caller putting
	 anything a stranger chose into a header value -- a search term into a
	 custom header, a user name into an Authorization -- wrote

	     X-Evil: a
	     Injected: yes

	 to the wire, two headers where the caller wrote one, and past a blank
	 line the client's own request BODY is the stranger's too.  There is no
	 reading under which a header value holding a line ending is the value
	 the caller meant."
	self checkHeaderName: aName value: aValue.
	headers := headers reject: [:pair | pair key asLowercase = aName asLowercase].
	headers add: aName -> aValue
]

{ #category : 'configuration' }
HttpClient >> headers [
	"The headers this client sends, as Associations.  Readable so that a
	 caller can see what it has set; a header added straight to this
	 collection is checked at the wire rather than at headerAt:put:."
	^headers
]

{ #category : 'private' }
HttpClient >> checkHeaderName: aName value: aValue [
	"Raise unless the two can be written as one header line.  The server's
	 rule, in HttpResponse>>checkHeaderName:value:, and for the same reasons
	 -- a field name with a space or a colon in it parses as something else
	 at the other end, and there is no legitimate one."
	| text |
	(aName isNil or: [aName isEmpty]) ifTrue: [^self error: 'a header has no name'].
	(aName anySatisfy: [:c | c asInteger <= 32 or: [c asInteger = 127 or: [c = $:]]])
		ifTrue: [^self error: 'not a header name: ', aName printString].
	text := aValue asString.
	(text anySatisfy: [:c | c asInteger = 13 or: [c asInteger = 10 or: [c asInteger = 0]]])
		ifTrue: [^self error: 'the value of the header ', aName,
			' holds a line ending or a NUL, which would write a second header']
]

{ #category : 'private' }
HttpClient >> checkRequestLine: aMethod target: aTarget [
	"Raise unless the two can be written as one request line.

	 The METHOD and the TARGET go out ahead of every header, so a line
	 ending in either injects headers exactly as one in a header value
	 does -- and a space injects a request line, since the line is three
	 space-separated words.  A target arrives from HttpUrl, which now
	 refuses a URL with a control character in it, so this is the check at
	 the wire that catches a caller who built an HttpUrl some other way;
	 the method arrives straight from request:url:body:contentType:, where
	 nothing looked at it at all."
	(aMethod isNil or: [aMethod isEmpty]) ifTrue: [^self error: 'a request has no method'].
	(aMethod anySatisfy: [:c | c asInteger <= 32 or: [c asInteger >= 127]])
		ifTrue: [^self error: 'not an HTTP method: ', aMethod printString].
	(aTarget isNil or: [aTarget isEmpty]) ifTrue: [^self error: 'a request has no target'].
	(aTarget anySatisfy: [:c | c asInteger <= 32 or: [c asInteger = 127]])
		ifTrue: [^self error: 'not a request target: ', aTarget printString]
]

{ #category : 'requests' }
HttpClient >> get: aUrlString [
	^self request: 'GET' url: aUrlString body: nil contentType: nil
]

{ #category : 'requests' }
HttpClient >> post: aUrlString body: aString contentType: aType [
	^self request: 'POST' url: aUrlString body: aString contentType: aType
]

{ #category : 'requests' }
HttpClient >> put: aUrlString body: aString contentType: aType [
	^self request: 'PUT' url: aUrlString body: aString contentType: aType
]

{ #category : 'requests' }
HttpClient >> delete: aUrlString [
	^self request: 'DELETE' url: aUrlString body: nil contentType: nil
]

{ #category : 'requests' }
HttpClient >> get: aUrlString do: aBlock [
	"The reply, with its body unread, to aBlock; the connection is closed
	 when the block is done.  Answers the block's value."
	^self request: 'GET' url: aUrlString body: nil contentType: nil do: aBlock
]

{ #category : 'requests' }
HttpClient >> post: aUrlString body: aString contentType: aType do: aBlock [
	^self request: 'POST' url: aUrlString body: aString contentType: aType do: aBlock
]

{ #category : 'requests' }
HttpClient >> request: aMethod url: aUrlString body: aStringOrNil contentType: aTypeOrNil [
	"The reply to one request, whole; the connection is closed after."
	^self request: aMethod url: aUrlString body: aStringOrNil contentType: aTypeOrNil
		do: [:reply | reply body. reply]
]

{ #category : 'requests' }
HttpClient >> request: aMethod url: aUrlString body: aStringOrNil contentType: aTypeOrNil do: aBlock [
	"One request; the reply -- status and headers read, body on the wire
	 -- to aBlock; the connection closed after, whatever the block did."
	| url socket stream total |
	url := HttpUrl fromString: aUrlString.
	(url isSecure and: [Socket isTlsAvailable not]) ifTrue: [
		^NetError new signal: 'this build has no TLS, so it cannot fetch ', aUrlString,
			' -- build with OpenSSL, or put a reverse proxy in front'].
	self checkRequestLine: aMethod target: url path.
	socket := connectTimeout isNil
		ifTrue: [Socket connectTo: url host port: url port]
		ifFalse: [Socket connectTo: url host port: url port timeout: connectTimeout].
	total := self totalTimeout.
	^[url isSecure ifTrue: [socket startTls: url host timeout: readTimeout].
	  stream := SocketStream on: socket.
	  stream readTimeout: readTimeout.
	  total isNil ifFalse: [stream deadlineIn: total].
	  self write: aMethod url: url body: aStringOrNil contentType: aTypeOrNil on: stream.
	  aBlock value: (HttpClientResponse readFrom: stream maxBodyBytes: maxBodyBytes)]
		ensure: [socket close]
]

{ #category : 'private' }
HttpClient >> write: aMethod url: aUrl body: aStringOrNil contentType: aTypeOrNil on: aStream [
	"The request on the wire.  Every part of it that a caller chose is
	 checked HERE as well as where it was set: headerAt:put: is the front
	 door and `headers' is an OrderedCollection a caller can reach, and a
	 check only at the front door is a check somebody walks around.  See
	 checkHeaderName:value: -- Bugs4 NET-3."
	| crlf |
	self checkRequestLine: aMethod target: aUrl path.
	headers do: [:pair | self checkHeaderName: pair key value: pair value].
	aTypeOrNil isNil ifFalse: [self checkHeaderName: 'Content-Type' value: aTypeOrNil].
	crlf := String with: (Character value: 13) with: (Character value: 10).
	aStream nextPutAll: aMethod; space; nextPutAll: aUrl path; nextPutAll: ' HTTP/1.1'; nextPutAll: crlf;
		nextPutAll: 'Host: '; nextPutAll: aUrl hostHeader; nextPutAll: crlf;
		nextPutAll: 'User-Agent: Smalltalk-2026'; nextPutAll: crlf;
		nextPutAll: 'Accept: */*'; nextPutAll: crlf;
		nextPutAll: 'Connection: close'; nextPutAll: crlf.
	headers do: [:pair | aStream nextPutAll: pair key; nextPutAll: ': '; nextPutAll: pair value; nextPutAll: crlf].
	aStringOrNil notNil ifTrue: [
		aTypeOrNil notNil ifTrue: [aStream nextPutAll: 'Content-Type: '; nextPutAll: aTypeOrNil; nextPutAll: crlf].
		aStream nextPutAll: 'Content-Length: '; nextPutAll: aStringOrNil size printString; nextPutAll: crlf].
	aStream nextPutAll: crlf.
	aStringOrNil notNil ifTrue: [aStream nextPutAll: aStringOrNil].
	aStream flush
]

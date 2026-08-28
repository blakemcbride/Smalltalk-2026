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
	#instVars : [ 'readTimeout', 'headers', 'maxBodyBytes' ],
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

{ #category : 'initialization' }
HttpClient >> initialize [
	readTimeout := 30000.
	headers := OrderedCollection new.
	maxBodyBytes := HttpBodyStream defaultMaxBodyBytes
]

{ #category : 'configuration' }
HttpClient >> readTimeout: milliseconds [
	"How long to wait for any part of the reply before raising TimedOut."
	readTimeout := milliseconds
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
	"A header sent with every request this client makes."
	headers := headers reject: [:pair | pair key asLowercase = aName asLowercase].
	headers add: aName -> aValue
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
	| url socket stream |
	url := HttpUrl fromString: aUrlString.
	(url isSecure and: [Socket isTlsAvailable not]) ifTrue: [
		^NetError new signal: 'this build has no TLS, so it cannot fetch ', aUrlString,
			' -- build with OpenSSL, or put a reverse proxy in front'].
	socket := Socket connectTo: url host port: url port.
	^[url isSecure ifTrue: [socket startTls: url host timeout: readTimeout].
	  stream := SocketStream on: socket.
	  stream readTimeout: readTimeout.
	  self write: aMethod url: url body: aStringOrNil contentType: aTypeOrNil on: stream.
	  aBlock value: (HttpClientResponse readFrom: stream maxBodyBytes: maxBodyBytes)]
		ensure: [socket close]
]

{ #category : 'private' }
HttpClient >> write: aMethod url: aUrl body: aStringOrNil contentType: aTypeOrNil on: aStream [
	| crlf |
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

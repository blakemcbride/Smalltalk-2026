"
An HTTP response: a status, headers, and a body, written to a stream in one
piece.

Content-Length is always sent and the body is always whole, so a client knows
where the response ends and the connection can carry the next request.  Date
is sent because HTTP/1.1 says a server with a clock must.  The server adds its
own standing headers -- the security ones Kiss's filter added -- before
writing; a handler's own headers win over those.

The body is a String, because a String in this image is bytes: text, a JSON
document, and the bytes of a PNG are all the same thing here.

A HEADER IS ONE HEADER.  A name and a value are checked when they are set
and again when they are written, and a value holding a carriage return, a
line feed or a NUL is an Error rather than a header -- see headerAt:put:.

A HEAD ANSWERS THE LENGTH A GET WOULD HAVE.  The response is built exactly as
it would be for a GET and the connection then sends everything but the body,
by omitBody; Content-Length still says how long the body is.  RFC 7231 asks
for precisely that, and it is the whole use a HEAD has -- a client asking how
big a thing is without fetching it.  Answering `Content-Length: 0' told it
the file was empty (Bugs3 B57, lower).
"
Class {
	#name : 'HttpResponse',
	#superclass : 'Object',
	#instVars : [ 'status', 'reason', 'headers', 'body', 'bodyOmitted' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'instance creation' }
HttpResponse class >> status: anInteger [
	^self new status: anInteger
]

{ #category : 'instance creation' }
HttpResponse class >> status: anInteger body: aString contentType: aType [
	^(self status: anInteger) body: aString; contentType: aType; yourself
]

{ #category : 'instance creation' }
HttpResponse class >> ok: aString [
	"200, as HTML."
	^self status: 200 body: aString contentType: 'text/html; charset=utf-8'
]

{ #category : 'instance creation' }
HttpResponse class >> text: aString [
	^self status: 200 body: aString contentType: 'text/plain; charset=utf-8'
]

{ #category : 'instance creation' }
HttpResponse class >> json: aString [
	"200, as JSON.  The argument is the document's text."
	^self status: 200 body: aString contentType: 'application/json'
]

{ #category : 'instance creation' }
HttpResponse class >> bytes: aString contentType: aType [
	^self status: 200 body: aString contentType: aType
]

{ #category : 'instance creation' }
HttpResponse class >> notFound: aString [
	^self status: 404 body: aString contentType: 'text/plain; charset=utf-8'
]

{ #category : 'instance creation' }
HttpResponse class >> error: status reason: aString [
	"A refusal, with the reason as the body."
	^self status: status body: aString contentType: 'text/plain; charset=utf-8'
]

{ #category : 'initialization' }
HttpResponse >> initialize [
	status := 200.
	headers := OrderedCollection new.
	body := ''.
	bodyOmitted := false
]

{ #category : 'accessing' }
HttpResponse >> status [
	^status
]

{ #category : 'accessing' }
HttpResponse >> status: anInteger [
	status := anInteger
]

{ #category : 'accessing' }
HttpResponse >> reason [
	^reason ifNil: [HttpCodec reasonFor: status]
]

{ #category : 'accessing' }
HttpResponse >> reason: aString [
	reason := aString
]

{ #category : 'accessing' }
HttpResponse >> body [
	^body
]

{ #category : 'accessing' }
HttpResponse >> body: aString [
	body := aString
]

{ #category : 'accessing' }
HttpResponse >> contentType: aString [
	self headerAt: 'Content-Type' put: aString
]

{ #category : 'accessing' }
HttpResponse >> headerAt: aName [
	"The header of that name, whatever its case, or nil."
	headers do: [:pair | (pair key asLowercase = aName asLowercase) ifTrue: [^pair value]].
	^nil
]

{ #category : 'accessing' }
HttpResponse >> headerAt: aName put: aValue [
	"Set the header, replacing one of the same name in any case.

	 A CR, an LF or a NUL in the value RAISES, and so does a name with
	 whitespace, a colon or one of those three in it.  Bugs3 B54: nothing
	 filtered them, and a handler doing the ordinary thing -- reflecting a
	 request parameter into a Location for a redirect, which is a shape
	 doc/REST-SERVER.md describes -- let a client that asked for
	 `?to=http://x/%0D%0ASet-Cookie:%20s=1' put a second header of its own
	 choosing into the reply.  Every header after the injected one is the
	 client's too, and so is the body once it injects a blank line.

	 RAISING rather than stripping, and the choice matters.  A stripped
	 Location is still a redirect, to a URL the handler did not write, and
	 the handler is never told; the client follows it.  A stripped
	 Set-Cookie sets a cookie with somebody else's value in it.  There is no
	 reading under which a header value holding a line ending is the value
	 the caller meant, so this is a fault in the handler and it is reported
	 as one: a plain Error, which HttpServer turns into a 500 and writes to
	 standard error where the person who wrote the handler can read it.
	 Filtering silently would leave that person with a redirect that goes
	 somewhere else and no line anywhere saying why.

	 The name is held to RFC 7230's rule more tightly than the value: a
	 space or a colon in a field name makes a line that parses as something
	 else at the other end, and there is no legitimate one."
	self checkHeaderName: aName value: aValue.
	headers := headers reject: [:pair | pair key asLowercase = aName asLowercase].
	headers add: aName -> aValue
]

{ #category : 'private' }
HttpResponse >> checkHeaderName: aName value: aValue [
	"Raise unless the two can be written as one header line.  See
	 headerAt:put: for why this raises rather than repairs."
	| text |
	(aName isNil or: [aName isEmpty]) ifTrue: [^self error: 'a header has no name'].
	(aName anySatisfy: [:c | c asInteger <= 32 or: [c asInteger = 127 or: [c = $:]]])
		ifTrue: [^self error: 'not a header name: ', aName printString].
	text := aValue asString.
	(text anySatisfy: [:c | c asInteger = 13 or: [c asInteger = 10 or: [c asInteger = 0]]])
		ifTrue: [^self error: 'the value of the header ', aName,
			' holds a line ending or a NUL, which would write a second header']
]

{ #category : 'writing' }
HttpResponse >> omitBody [
	"Write the headers and no body: the answer to a HEAD.  Content-Length
	 still says how long the body is, because a HEAD is a GET without the
	 bytes and a client asks one exactly to learn that number."
	bodyOmitted := true
]

{ #category : 'testing' }
HttpResponse >> isBodyOmitted [
	^bodyOmitted == true
]

{ #category : 'accessing' }
HttpResponse >> headers [
	^headers
]

{ #category : 'writing' }
HttpResponse >> writeOn: aStream keepAlive: aBoolean [
	"The whole response, status line to body.  The stream is not flushed:
	 the connection flushes, so a response never crosses the wire in two
	 pieces."
	| crlf |
	"Every header is checked BEFORE the first byte goes out.  headerAt:put:
	 is the way a header is meant to be set and checks there, but `headers'
	 is readable and a handler can add an Association to it directly; this
	 is the last place before the wire, and a check only at the front door
	 is a check somebody walks around.  Checking them all first rather than
	 one at a time in the loop below means a refusal leaves nothing
	 half-written on the connection."
	headers do: [:pair | self checkHeaderName: pair key value: pair value].
	crlf := String with: (Character value: 13) with: (Character value: 10).
	aStream nextPutAll: 'HTTP/1.1 '; nextPutAll: status printString; space; nextPutAll: self reason; nextPutAll: crlf.
	(self headerAt: 'Date') isNil ifTrue: [aStream nextPutAll: 'Date: '; nextPutAll: HttpCodec httpDate; nextPutAll: crlf].
	aStream nextPutAll: 'Content-Length: '; nextPutAll: body size printString; nextPutAll: crlf.
	aStream nextPutAll: 'Connection: '; nextPutAll: (aBoolean ifTrue: ['keep-alive'] ifFalse: ['close']); nextPutAll: crlf.
	headers do: [:pair |
		aStream nextPutAll: pair key; nextPutAll: ': '; nextPutAll: pair value asString; nextPutAll: crlf].
	aStream nextPutAll: crlf.
	self isBodyOmitted ifFalse: [aStream nextPutAll: body]
]

{ #category : 'printing' }
HttpResponse >> printOn: aStream [
	aStream nextPutAll: 'HttpResponse '; print: status; space; nextPutAll: self reason; nextPutAll: ' ('; print: body size; nextPutAll: ' bytes)'
]

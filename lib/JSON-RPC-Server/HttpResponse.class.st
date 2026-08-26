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
"
Class {
	#name : 'HttpResponse',
	#superclass : 'Object',
	#instVars : [ 'status', 'reason', 'headers', 'body' ],
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
	body := ''
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
	"Set the header, replacing one of the same name in any case."
	headers := headers reject: [:pair | pair key asLowercase = aName asLowercase].
	headers add: aName -> aValue
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
	crlf := String with: (Character value: 13) with: (Character value: 10).
	aStream nextPutAll: 'HTTP/1.1 '; nextPutAll: status printString; space; nextPutAll: self reason; nextPutAll: crlf.
	(self headerAt: 'Date') isNil ifTrue: [aStream nextPutAll: 'Date: '; nextPutAll: HttpCodec httpDate; nextPutAll: crlf].
	aStream nextPutAll: 'Content-Length: '; nextPutAll: body size printString; nextPutAll: crlf.
	aStream nextPutAll: 'Connection: '; nextPutAll: (aBoolean ifTrue: ['keep-alive'] ifFalse: ['close']); nextPutAll: crlf.
	headers do: [:pair |
		aStream nextPutAll: pair key; nextPutAll: ': '; nextPutAll: pair value asString; nextPutAll: crlf].
	aStream nextPutAll: crlf.
	aStream nextPutAll: body
]

{ #category : 'printing' }
HttpResponse >> printOn: aStream [
	aStream nextPutAll: 'HttpResponse '; print: status; space; nextPutAll: self reason; nextPutAll: ' ('; print: body size; nextPutAll: ' bytes)'
]

"
The response writer, on a stream that is a String.
"
Class {
	#name : 'HttpResponseTest',
	#superclass : 'TestCase',
	#category : 'JSON-RPC-Server-Tests'
}

{ #category : 'private' }
HttpResponseTest >> write: aResponse keepAlive: aBoolean [
	| out |
	out := WriteStream on: (String new: 256).
	aResponse writeOn: out keepAlive: aBoolean.
	^out contents
]

{ #category : 'tests' }
HttpResponseTest >> testAJsonResponse [
	| text crlf |
	crlf := String with: (Character value: 13) with: (Character value: 10).
	text := self write: (HttpResponse json: '{"a":1}') keepAlive: true.
	self assert: (text beginsWith: 'HTTP/1.1 200 OK', crlf).
	self assert: (text includesSubstring: 'Content-Length: 7', crlf).
	self assert: (text includesSubstring: 'Content-Type: application/json', crlf).
	self assert: (text includesSubstring: 'Connection: keep-alive', crlf).
	self assert: (text includesSubstring: 'Date: ').
	self assert: (text endsWith: crlf, crlf, '{"a":1}')
]

{ #category : 'tests' }
HttpResponseTest >> testAnErrorResponse [
	| text |
	text := self write: (HttpResponse error: 404 reason: 'no such thing') keepAlive: false.
	self assert: (text beginsWith: 'HTTP/1.1 404 Not Found').
	self assert: (text includesSubstring: 'Connection: close').
	self assert: (text endsWith: 'no such thing')
]

{ #category : 'tests' }
HttpResponseTest >> testHeadersReplaceByNameInAnyCase [
	| r |
	r := HttpResponse ok: 'x'.
	r headerAt: 'X-Thing' put: 'one'.
	r headerAt: 'x-thing' put: 'two'.
	self assert: (r headerAt: 'X-THING') equals: 'two'.
	self assert: (r headers select: [:p | p key asLowercase = 'x-thing']) size equals: 1
]

{ #category : 'tests' }
HttpResponseTest >> testAStatusWithNoKnownReason [
	self assert: (HttpResponse status: 299) reason equals: 'Unknown'.
	self assert: ((HttpResponse status: 299) reason: 'Whatever'; reason) equals: 'Whatever'
]

{ #category : 'tests - header injection' }
HttpResponseTest >> testAHeaderValueWithALineEndingIsRefused [
	"Bugs3 B54.  Nothing filtered CR or LF out of a header value, so a
	 handler reflecting a request parameter into a Location -- the ordinary
	 shape of a redirect -- let the client write headers of its own, and
	 everything after the injected one was the client's too.  Refused
	 rather than stripped: a stripped Location is still a redirect, to a URL
	 the handler did not write, with nothing anywhere saying so."
	| r crlf |
	crlf := String with: (Character value: 13) with: (Character value: 10).
	r := HttpResponse status: 302.
	self should: [r headerAt: 'Location' put: 'http://x/', crlf, 'Set-Cookie: s=1'] raise: Error.
	self should: [r headerAt: 'Location' put: 'http://x/', (String with: (Character value: 10))] raise: Error.
	self should: [r headerAt: 'Location' put: 'http://x/', (String with: (Character value: 13))] raise: Error.
	self should: [r headerAt: 'X-Thing' put: 'a', (String with: (Character value: 0)), 'b'] raise: Error.
	"nothing of the sort was stored"
	self assert: (r headerAt: 'Location') isNil.
	self deny: ((self write: r keepAlive: false) includesSubstring: 'Set-Cookie')
]

{ #category : 'tests - header injection' }
HttpResponseTest >> testAHeaderNameWithWhitespaceOrAColonIsRefused [
	| r |
	r := HttpResponse ok: 'x'.
	self should: [r headerAt: 'X Thing' put: 'v'] raise: Error.
	self should: [r headerAt: 'X:Thing' put: 'v'] raise: Error.
	self should: [r headerAt: '' put: 'v'] raise: Error.
	self should: [r headerAt: 'X-Thing', (String with: (Character value: 13)) put: 'v'] raise: Error.
	self assert: (r headerAt: 'X-Thing' put: 'v'; headerAt: 'x-thing') equals: 'v'
]

{ #category : 'tests - header injection' }
HttpResponseTest >> testAHeaderAddedBehindTheBackOfHeaderAtPutIsStillRefused [
	"`headers' is readable, so a handler can add an Association to it
	 directly and never go through headerAt:put:.  The writer checks again,
	 and checks every header before it writes the first byte, so a refusal
	 leaves nothing half-written on the connection."
	| r crlf |
	crlf := String with: (Character value: 13) with: (Character value: 10).
	r := HttpResponse ok: 'x'.
	r headers add: 'Location' -> ('http://x/', crlf, 'Set-Cookie: s=1').
	self should: [self write: r keepAlive: false] raise: Error
]

{ #category : 'tests - head' }
HttpResponseTest >> testAHeadKeepsTheContentLengthAndDropsTheBody [
	"Bugs3 B57, lower: a HEAD answered `Content-Length: 0', which is a lie
	 to the one client that asks a HEAD to learn how big something is.  The
	 response is built as the GET's and written without its body."
	| crlf full head |
	crlf := String with: (Character value: 13) with: (Character value: 10).
	full := self write: (HttpResponse text: 'twelve bytes') keepAlive: false.
	head := self write: ((HttpResponse text: 'twelve bytes') omitBody; yourself) keepAlive: false.
	self assert: (full includesSubstring: 'Content-Length: 12', crlf).
	self assert: (head includesSubstring: 'Content-Length: 12', crlf).
	self assert: (full endsWith: crlf, crlf, 'twelve bytes').
	self assert: (head endsWith: crlf, crlf).
	self assert: head size equals: full size - 12
]

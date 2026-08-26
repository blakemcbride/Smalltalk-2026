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

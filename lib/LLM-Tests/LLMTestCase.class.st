"
What every LLM test stands on: a listener of the test's own that answers
canned replies -- one per connection, in order -- and keeps every request
it was sent, parsed: the request line, the headers by lowercase name, the
body.  So what is tested is the wire: the header a service wants, the body
a request carries, and a reply as it would come, chunked as Go chunks and
cut into lines as an event stream is.

No service is reached.  The real ones are reached, with real keys, by
lib/LLM-Live-Tests in the llm-live profile.
"
Class {
	#name : 'LLMTestCase',
	#superclass : 'TestCase',
	#instVars : [ 'requests' ],
	#category : 'LLM-Tests'
}

{ #category : 'testing' }
LLMTestCase class >> isAbstract [
	^self name = #LLMTestCase
]

{ #category : 'private' }
LLMTestCase >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
LLMTestCase >> lf [
	^String with: (Character value: 10)
]

{ #category : 'private' }
LLMTestCase >> hex: anInteger [
	| digits hex n |
	digits := '0123456789abcdef'.
	hex := ''.
	n := anInteger.
	n = 0 ifTrue: [^'0'].
	[n > 0] whileTrue: [hex := (String with: (digits at: n \\ 16 + 1)), hex. n := n // 16].
	^hex
]

{ #category : 'replies' }
LLMTestCase >> chunked: aBody [
	"A 200 with aBody as one chunk and the end, as Go's net/http sends it."
	^'HTTP/1.1 200 OK', self crlf, 'Content-Type: application/json; charset=utf-8', self crlf,
		'Transfer-Encoding: chunked', self crlf, self crlf,
		(self hex: aBody size), self crlf, aBody, self crlf, '0', self crlf, self crlf
]

{ #category : 'replies' }
LLMTestCase >> status: anInteger body: aBody [
	"A reply with that status and a Content-Length body: what an error is."
	^'HTTP/1.1 ', anInteger printString, ' ', (HttpCodec reasonFor: anInteger), self crlf,
		'Content-Type: application/json', self crlf,
		'Content-Length: ', aBody size printString, self crlf, self crlf, aBody
]

{ #category : 'replies' }
LLMTestCase >> events: anArrayOfData [
	"An event stream: each element a `data:' line followed by a blank
	 line, the whole chunked in two so that a boundary falls inside an
	 event, as it does."
	| body half |
	body := WriteStream on: (String new: 512).
	anArrayOfData do: [:each |
		(each startsWith: ':')
			ifTrue: [body nextPutAll: each; nextPutAll: self lf]
			ifFalse: [body nextPutAll: 'data: '; nextPutAll: each; nextPutAll: self lf; nextPutAll: self lf]].
	body := body contents.
	half := body size // 2 max: 1.
	^'HTTP/1.1 200 OK', self crlf, 'Content-Type: text/event-stream', self crlf,
		'Transfer-Encoding: chunked', self crlf, self crlf,
		(self hex: half), self crlf, (body copyFrom: 1 to: half), self crlf,
		(self hex: body size - half), self crlf, (body copyFrom: half + 1 to: body size), self crlf,
		'0', self crlf, self crlf
]

{ #category : 'replies' }
LLMTestCase >> lines: anArrayOfLines [
	"One JSON document a line, LF-ended, chunked: what Ollama streams."
	| body |
	body := WriteStream on: (String new: 512).
	anArrayOfLines do: [:each | body nextPutAll: each; nextPutAll: self lf].
	^self chunked: body contents
]

{ #category : 'the listener' }
LLMTestCase >> serving: replyOrReplies do: aBlock [
	"A listener answering each connection with the next reply, keeping
	 each request; aBlock gets the base URL.  Answers the block's value."
	| replies listener |
	replies := replyOrReplies isString ifTrue: [Array with: replyOrReplies] ifFalse: [replyOrReplies].
	requests := OrderedCollection new.
	listener := ServerSocket listenOn: 0.
	[[replies do: [:reply | self answer: reply on: listener]] on: NetError do: [:e | nil]] fork.
	^[aBlock value: 'http://127.0.0.1:', listener localPort printString] ensure: [listener close]
]

{ #category : 'the listener' }
LLMTestCase >> answer: replyText on: aListener [
	| c in line request colon length |
	c := aListener accept.
	in := SocketStream on: c.
	request := Dictionary new.
	request at: 'line' put: in nextLine.
	[(line := in nextLine) isEmpty] whileFalse: [
		colon := line indexOf: $:.
		colon > 0 ifTrue: [
			request at: (line copyFrom: 1 to: colon - 1) trimmed asLowercase
				put: (line copyFrom: colon + 1 to: line size) trimmed]].
	length := (request at: 'content-length' ifAbsent: ['0']) asNumber.
	request at: 'body' put: (length > 0 ifTrue: [in next: length] ifFalse: ['']).
	requests add: request.
	c send: replyText.
	c close
]

{ #category : 'the requests' }
LLMTestCase >> requestAt: anIndex [
	^requests at: anIndex
]

{ #category : 'the requests' }
LLMTestCase >> lineAt: anIndex [
	^(requests at: anIndex) at: 'line'
]

{ #category : 'the requests' }
LLMTestCase >> header: aName at: anIndex [
	^(requests at: anIndex) at: aName asLowercase ifAbsent: [nil]
]

{ #category : 'the requests' }
LLMTestCase >> bodyAt: anIndex [
	^JSONObject fromString: ((requests at: anIndex) at: 'body')
]

{ #category : 'the requests' }
LLMTestCase >> requestCount [
	^requests size
]

"
One reply: the status, the headers by lowercase name, and the body.

    reply status        200
    reply isSuccess     true for 200 to 299
    reply headerAt: 'content-type'
    reply body          a String of bytes, the whole of it
    reply json          the body as a JSONObject
    reply bodyStream    the body as it comes, by the line

The status and the headers are read when the reply is; the body is not,
until somebody asks.  `body' reads the whole of it and keeps it, which is
what HttpClient's get: and post: ask for before they close the connection.
`bodyStream' is an HttpBodyStream over the connection, for a caller who
would rather read as the bytes arrive -- a model's answer a token at a
time -- and is what HttpClient's do: forms leave for the block.  The two do
not mix: a body read whole has no stream left, and a stream read from has
no whole body.

The three ways a body ends, and a status that has none, are the stream's
business (HttpBodyStream), and so is the limit on how much of one this
client will read.  A reply that is not HTTP -- no status line -- is a
NetError here: the other end did not speak it.
"
Class {
	#name : 'HttpClientResponse',
	#superclass : 'Object',
	#instVars : [ 'version', 'status', 'reason', 'headers', 'body', 'bodyStream' ],
	#category : 'HTTP-Client'
}

{ #category : 'instance creation' }
HttpClientResponse class >> readFrom: aStream [
	^self readFrom: aStream maxBodyBytes: HttpBodyStream defaultMaxBodyBytes
]

{ #category : 'instance creation' }
HttpClientResponse class >> readFrom: aStream maxBodyBytes: anInteger [
	^self new readFrom: aStream maxBodyBytes: anInteger
]

{ #category : 'reading' }
HttpClientResponse >> readFrom: aStream [
	^self readFrom: aStream maxBodyBytes: HttpBodyStream defaultMaxBodyBytes
]

{ #category : 'reading' }
HttpClientResponse >> readFrom: aStream maxBodyBytes: anInteger [
	| line space rest |
	line := aStream nextLine.
	(line startsWith: 'HTTP/') ifFalse: [^NetError new signal: 'not an HTTP reply: ', line printString].
	space := line indexOf: Character space.
	space = 0 ifTrue: [^NetError new signal: 'not an HTTP reply: ', line printString].
	version := line copyFrom: 1 to: space - 1.
	rest := (line copyFrom: space + 1 to: line size) trimmed.
	space := rest indexOf: Character space.
	status := (space = 0 ifTrue: [rest] ifFalse: [rest copyFrom: 1 to: space - 1]) asNumber.
	(status isKindOf: Integer) ifFalse: [^NetError new signal: 'not an HTTP status: ', line printString].
	reason := space = 0 ifTrue: [''] ifFalse: [(rest copyFrom: space + 1 to: rest size) trimmed].
	self readHeadersFrom: aStream.
	bodyStream := HttpBodyStream on: aStream status: status headers: headers
		maxBodyBytes: anInteger
]

{ #category : 'private' }
HttpClientResponse >> readHeadersFrom: aStream [
	| line colon |
	headers := Dictionary new.
	[(line := aStream nextLine) isEmpty] whileFalse: [
		colon := line indexOf: $:.
		colon > 1 ifTrue: [
			headers at: (line copyFrom: 1 to: colon - 1) trimmed asLowercase
				put: (line copyFrom: colon + 1 to: line size) trimmed]]
]

{ #category : 'accessing' }
HttpClientResponse >> version [
	^version
]

{ #category : 'accessing' }
HttpClientResponse >> status [
	^status
]

{ #category : 'accessing' }
HttpClientResponse >> reason [
	^reason
]

{ #category : 'accessing' }
HttpClientResponse >> headers [
	^headers
]

{ #category : 'accessing' }
HttpClientResponse >> headerAt: aName [
	"The header of that name, in any case, or nil."
	^headers at: aName asLowercase ifAbsent: [nil]
]

{ #category : 'accessing' }
HttpClientResponse >> body [
	"The whole body, read now if it has not been."
	body isNil ifTrue: [
		body := bodyStream upToEnd.
		bodyStream := nil].
	^body
]

{ #category : 'accessing' }
HttpClientResponse >> bodyStream [
	"The body as it arrives, or nil once it has been read whole."
	^bodyStream
]

{ #category : 'accessing' }
HttpClientResponse >> json [
	"The body as a JSONObject; raises JSONError when it is not one."
	^JSONObject fromString: body
]

{ #category : 'testing' }
HttpClientResponse >> isSuccess [
	^status >= 200 and: [status <= 299]
]

{ #category : 'printing' }
HttpClientResponse >> printOn: aStream [
	aStream nextPutAll: 'HttpClientResponse '; print: status; space; nextPutAll: reason
]

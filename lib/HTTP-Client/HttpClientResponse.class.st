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
	"The reply, past any interim ones.

	 A 1xx IS NOT AN ANSWER, it is a note that the answer is coming, and
	 this read used to take the first status line it saw as final.  `103
	 Early Hints' is sent unsolicited by real content delivery networks
	 ahead of the 200, and `100 Continue' by any server that saw an Expect
	 header, so Bugs4 NET-5 got status 103 and an empty body from an
	 ordinary fetch and never read the reply at all.  RFC 7230 says a
	 client must be able to parse and discard any number of them, so the
	 loop is here; the bound is on how many, because a peer that sends
	 nothing but interim responses is a peer holding this process open."
	| interim |
	interim := 0.
	[self readOneFrom: aStream maxBodyBytes: anInteger.
	 status < 100 or: [status > 199]] whileFalse: [
		(interim := interim + 1) > 8 ifTrue: [
			^NetError new signal: 'the reply is nothing but interim responses']].
	^self
]

{ #category : 'private' }
HttpClientResponse >> readOneFrom: aStream maxBodyBytes: anInteger [
	"One status line, its headers, and the stream its body would be read
	 from.  An interim reply has no body, which HttpBodyStream already
	 knows from the status."
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
	"The headers, by lowercase name.

	 A SECOND HEADER OF THE SAME NAME IS REFUSED where it changes where the
	 body ends -- Content-Length and Transfer-Encoding -- rather than
	 quietly kept last-wins as every header was.  RFC 7230 requires exactly
	 that, and the reason is request smuggling: two Content-Lengths mean
	 this client and whatever is between it and the origin can disagree
	 about where one reply stops and the next begins, and the disagreement
	 is the attack.  Bugs4 NET-9.  A repeated Content-Length of the SAME
	 value is allowed, because that is one framing written twice and there
	 is nothing to disagree about; anything else is a peer this client
	 cannot safely read.  Other repeated headers are still last-wins: they
	 say nothing about framing."
	| line colon name value old |
	headers := Dictionary new.
	[(line := aStream nextLine) isEmpty] whileFalse: [
		colon := line indexOf: $:.
		colon > 1 ifTrue: [
			name := (line copyFrom: 1 to: colon - 1) trimmed asLowercase.
			value := (line copyFrom: colon + 1 to: line size) trimmed.
			old := headers at: name ifAbsent: [nil].
			(old notNil
			 and: [(name = 'content-length' or: [name = 'transfer-encoding'])
			 and: [old ~= value]])
				ifTrue: [^NetError new signal: 'the reply frames its body two ways: ',
					name, ' is both ', old printString, ' and ', value printString].
			headers at: name put: value]]
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

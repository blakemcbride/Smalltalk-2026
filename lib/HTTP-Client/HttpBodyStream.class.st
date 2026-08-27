"
The body of a reply, read as it comes: by the character, by the line, or
whole.  What HttpClientResponse hands out as bodyStream, and what a caller
reading a model's answer token by token reads from.

The three ways a body ends, decided from the headers before the first
byte is read: `Transfer-Encoding: chunked' means hex-sized chunks until a
chunk of size zero, then the trailers, which are read and dropped; a
Content-Length means that many bytes; and neither means everything up to
the close of the connection, which HttpClient's `Connection: close' makes
safe.  A status that cannot have a body -- 1xx, 204, 304 -- is a stream
that is at its end already.  A chunk's size line may carry extensions
after a semicolon, which are ignored; a size that is not a number is a
NetError, because the other end did not speak HTTP.

BY THE LINE, because that is how the streaming shapes come: an event
stream is `data: {...}' lines with a blank line between events, and
Ollama's is one JSON document a line.  nextLine answers a line without its
ending, CR-LF or LF, and NIL AT THE END -- not an empty String, which is a
blank line and means something in an event stream.  The chunk boundaries
are invisible to it: a line may span two chunks and does, since a server
chunks by its buffer and not by the line.

upToEnd is what HttpClientResponse>>body asks for, and reads by the chunk
or the length rather than the character.
"
Class {
	#name : 'HttpBodyStream',
	#superclass : 'Object',
	#instVars : [ 'in', 'mode', 'remaining', 'first' ],
	#category : 'HTTP-Client'
}

{ #category : 'instance creation' }
HttpBodyStream class >> on: aSocketStream status: statusInteger headers: aDictionary [
	^self new setStream: aSocketStream status: statusInteger headers: aDictionary
]

{ #category : 'instance creation' }
HttpBodyStream class >> empty [
	^self new setStream: nil status: 204 headers: Dictionary new
]

{ #category : 'private' }
HttpBodyStream >> setStream: aSocketStream status: statusInteger headers: aDictionary [
	| encoding length |
	in := aSocketStream.
	first := true.
	(statusInteger < 200 or: [statusInteger = 204 or: [statusInteger = 304]]) ifTrue: [mode := #none. ^self].
	encoding := aDictionary at: 'transfer-encoding' ifAbsent: [nil].
	(encoding notNil and: [encoding asLowercase includesSubstring: 'chunked']) ifTrue: [
		mode := #chunked.
		remaining := 0.
		^self].
	length := aDictionary at: 'content-length' ifAbsent: [nil].
	length notNil ifTrue: [
		mode := #length.
		remaining := length trimmed asNumber.
		^self].
	mode := #close
]

{ #category : 'private' }
HttpBodyStream >> nextChunkSize [
	"The size of the next chunk, from its size line; 0 at the last, after
	 which the trailers are read and dropped and the stream is at its end.
	 Every chunk but the first is preceded by the CR-LF that ended the last
	 one's data."
	| line semicolon size |
	first ifTrue: [first := false] ifFalse: [in nextLine].
	line := in nextLine trimmed.
	semicolon := line indexOf: $;.
	semicolon > 0 ifTrue: [line := (line copyFrom: 1 to: semicolon - 1) trimmed].
	size := self hexValueOf: line.
	size = 0 ifTrue: [
		[in nextLine isEmpty] whileFalse: [].
		mode := #none].
	^size
]

{ #category : 'private' }
HttpBodyStream >> hexValueOf: aString [
	| value digit |
	aString isEmpty ifTrue: [^NetError new signal: 'a chunk with no size'].
	value := 0.
	aString do: [:c |
		digit := c asInteger.
		(digit >= 48 and: [digit <= 57]) ifTrue: [digit := digit - 48] ifFalse: [
		(digit >= 97 and: [digit <= 102]) ifTrue: [digit := digit - 87] ifFalse: [
		(digit >= 65 and: [digit <= 70]) ifTrue: [digit := digit - 55] ifFalse: [
			^NetError new signal: 'not a chunk size: ', aString printString]]].
		value := value * 16 + digit].
	^value
]

{ #category : 'reading' }
HttpBodyStream >> next [
	"The next Character of the body, or nil at its end."
	| c |
	mode == #none ifTrue: [^nil].
	mode == #close ifTrue: [^in next].
	mode == #chunked ifTrue: [
		[remaining = 0] whileTrue: [
			remaining := self nextChunkSize.
			mode == #none ifTrue: [^nil]]].
	remaining = 0 ifTrue: [^nil].
	c := in next.
	c isNil ifTrue: [mode := #none. ^nil].
	remaining := remaining - 1.
	^c
]

{ #category : 'reading' }
HttpBodyStream >> nextLine [
	"The next line without its ending, or nil at the end of the body.  A
	 blank line is an empty String, which is not nil: an event stream
	 separates its events with one."
	| out c |
	c := self next.
	c isNil ifTrue: [^nil].
	out := WriteStream on: (String new: 128).
	[c notNil and: [c ~~ (Character value: 10)]] whileTrue: [
		out nextPut: c.
		c := self next].
	out := out contents.
	(out size > 0 and: [out last = (Character value: 13)]) ifTrue: [^out copyFrom: 1 to: out size - 1].
	^out
]

{ #category : 'reading' }
HttpBodyStream >> upToEnd [
	"The rest of the body as one String: by the length, by the chunk, or
	 up to the close, and not by the character."
	| out size |
	mode == #none ifTrue: [^''].
	mode == #close ifTrue: [mode := #none. ^in contents].
	mode == #length ifTrue: [
		out := in next: remaining.
		remaining := 0.
		mode := #none.
		^out].
	out := WriteStream on: (String new: 4096).
	remaining > 0 ifTrue: [out nextPutAll: (in next: remaining). remaining := 0].
	[size := self nextChunkSize.
	 size > 0] whileTrue: [out nextPutAll: (in next: size)].
	^out contents
]

{ #category : 'reading' }
HttpBodyStream >> linesDo: aBlock [
	"Every remaining line, in turn, to aBlock."
	| line |
	[(line := self nextLine) notNil] whileTrue: [aBlock value: line]
]

{ #category : 'testing' }
HttpBodyStream >> atEnd [
	"Whether the body is over, as far as is known without reading: after
	 the last chunk, the last byte of the length, or the close.  A body
	 that ends with the close cannot say until a read finds it."
	mode == #none ifTrue: [^true].
	mode == #length ifTrue: [^remaining = 0].
	^false
]

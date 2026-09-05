"
The body of a reply, read as it comes: by the character, by the line, or
whole.  What HttpClientResponse hands out as bodyStream, and what a caller
reading a model's answer token by token reads from.

The three ways a body ends, decided from the headers before the first
byte is read: `Transfer-Encoding: chunked' means hex-sized chunks until a
chunk of size zero, then the trailers, which are read and dropped; a
Content-Length means that many bytes; and neither means everything up to
the close of the connection, which HttpClient's `Connection: close' makes
safe.  BOTH is neither: two framings can be read two ways, and a reply
carrying both is refused rather than resolved in favour of one, since
whatever is between this client and the origin may resolve it the other
way.  A status that cannot have a body -- 1xx, 204, 304 -- is a stream
that is at its end already.  A chunk's size line may carry extensions
after a semicolon, which are ignored; a size that is not a number is a
NetError, because the other end did not speak HTTP.

A BODY THAT STOPS EARLY IS AN ERROR AND NOT A SHORT BODY, in all three
shapes.  Chunked truncation was always caught, by the terminating chunk
that never came -- though it was reported as a malformed chunk size rather
than as the closed connection it is.  A Content-Length short of its own
count was not caught at all, so a reply cut off in the middle answered 200
with a piece of a document and nothing said which piece (Bugs4 NET-4).  A
truncated JSON document usually fails to parse; a truncated anything else
does not, since half a CSV is a valid CSV.

BY THE LINE, because that is how the streaming shapes come: an event
stream is `data: {...}' lines with a blank line between events, and
Ollama's is one JSON document a line.  nextLine answers a line without its
ending, CR-LF or LF, and NIL AT THE END -- not an empty String, which is a
blank line and means something in an event stream.  The chunk boundaries
are invisible to it: a line may span two chunks and does, since a server
chunks by its buffer and not by the line.

upToEnd is what HttpClientResponse>>body asks for, and reads by the chunk
or the length rather than the character.

THE BODY IS BOUNDED, at 64 MB unless the client says otherwise, in all three
of those shapes.  The reply comes from somewhere else, and Bugs3 B57 (lower)
found nothing here that stopped it being any size at all: a Content-Length
of a hundred gigabytes was a hundred gigabytes of String, and a chunked body
had no total at all.  A body over the limit is a NetError naming it, raised
before the bytes are read rather than after.

A CHUNK SIZE THAT IS NOT ONE IS A NetError, and that includes one that is
too large to be a size.  `ffffffffffffffff' -- sixteen f's, which is a valid
hexadecimal number and 2^64-1 -- was read as a number and passed to
`String new:', which came back as `a primitive has failed': a message about
this image, from a fault in the other end's framing.  A size line longer
than sixteen digits is refused before it is even converted, because a
sixty-four-bit count is the most a chunk size can be and a longer one is a
peer that is not speaking HTTP.
"
Class {
	#name : 'HttpBodyStream',
	#superclass : 'Object',
	#instVars : [ 'in', 'mode', 'remaining', 'first', 'maxBody', 'read' ],
	#category : 'HTTP-Client'
}

{ #category : 'instance creation' }
HttpBodyStream class >> defaultMaxBodyBytes [
	"64 MB, the same number HttpServer holds a request to.  Generous
	 rather than tight on purpose: a limit that a legitimate reply meets is
	 a limit somebody turns off, and then there is none."
	^64 * 1024 * 1024
]

{ #category : 'instance creation' }
HttpBodyStream class >> on: aSocketStream status: statusInteger headers: aDictionary [
	^self on: aSocketStream status: statusInteger headers: aDictionary
		maxBodyBytes: self defaultMaxBodyBytes
]

{ #category : 'instance creation' }
HttpBodyStream class >> on: aSocketStream status: statusInteger headers: aDictionary maxBodyBytes: anInteger [
	^self new setStream: aSocketStream status: statusInteger headers: aDictionary
		maxBodyBytes: anInteger
]

{ #category : 'instance creation' }
HttpBodyStream class >> empty [
	^self new setStream: nil status: 204 headers: Dictionary new
		maxBodyBytes: self defaultMaxBodyBytes
]

{ #category : 'accessing' }
HttpBodyStream >> maxBodyBytes [
	^maxBody
]

{ #category : 'private' }
HttpBodyStream >> setStream: aSocketStream status: statusInteger headers: aDictionary maxBodyBytes: anInteger [
	| encoding length |
	in := aSocketStream.
	first := true.
	maxBody := anInteger.
	read := 0.
	(statusInteger < 200 or: [statusInteger = 204 or: [statusInteger = 304]]) ifTrue: [mode := #none. ^self].
	encoding := aDictionary at: 'transfer-encoding' ifAbsent: [nil].
	length := aDictionary at: 'content-length' ifAbsent: [nil].
	(encoding notNil and: [length notNil]) ifTrue: [
		"TWO FRAMINGS ARE NO FRAMING.  Either header alone says where the
		 body ends; the two together say it twice and may say it
		 differently, and RFC 7230 requires the message to be REJECTED
		 rather than resolved in favour of one -- precisely because
		 whatever parser is in front may resolve it in favour of the
		 other, which is the whole of response smuggling.  This preferred
		 chunked and read the Content-Length not at all (Bugs4 NET-9).
		 HttpRequest has answered 400 to the same shape since Bugs3 B56;
		 the client side is a NetError, which is what it says about every
		 other reply it cannot read."
		^NetError new signal:
			'the reply frames its body two ways: Transfer-Encoding and Content-Length'].
	(encoding notNil and: [encoding asLowercase includesSubstring: 'chunked']) ifTrue: [
		mode := #chunked.
		remaining := 0.
		^self].
	length notNil ifTrue: [
		"A Content-Length is digits or it is not a length.  asNumber read as
		 far as it understood and answered what it had, so a header of
		 `abc' became 0 and one of `12x' became 12 -- either of which is a
		 body boundary in the wrong place, on a connection whose other end
		 chose the header."
		length := length trimmed.
		(length isEmpty or: [length anySatisfy: [:c | c isDigit not]])
			ifTrue: [^NetError new signal: 'not a Content-Length: ', length printString].
		mode := #length.
		remaining := length asInteger.
		remaining > maxBody ifTrue: [
			^NetError new signal: 'the reply declares ', remaining printString,
				' bytes, over the ', maxBody printString, ' this client reads'].
		^self].
	mode := #close
]

{ #category : 'private' }
HttpBodyStream >> countBytes: anInteger [
	"Add to the running total and raise past the limit.  See the class
	 comment: the three shapes a body has are three ways to be handed more
	 of it than was asked for, and this is the one place all three pass."
	read := read + anInteger.
	read > maxBody ifTrue: [
		^NetError new signal: 'the reply is longer than the ',
			maxBody printString, ' bytes this client reads'].
	^anInteger
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
	"THE PEER WENT AWAY, and that is not a malformed chunk size.  A
	 connection closed in the middle of a chunked body reaches here with
	 nothing left to read, and the message was `a chunk with no size' --
	 which reads as `the other end sent nonsense' rather than `the other
	 end stopped', and sent whoever was streaming a model's answer looking
	 for a framing bug that was not there (Bugs4).  It is still a NetError:
	 a body that stops early is a truncated answer, and answering the piece
	 of it that arrived as though it were the whole is the fault NET-4
	 names on the other framing."
	(line isEmpty and: [in atEnd]) ifTrue: [
		^NetError new signal: 'the connection closed in the middle of a chunked body'].
	semicolon := line indexOf: $;.
	semicolon > 0 ifTrue: [line := (line copyFrom: 1 to: semicolon - 1) trimmed].
	line size > 16 ifTrue: [
		^NetError new signal: 'not a chunk size: ', line printString].
	size := self hexValueOf: line.
	size > maxBody ifTrue: [
		^NetError new signal: 'a chunk of ', size printString,
			' bytes is over the ', maxBody printString, ' this client reads'].
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
	mode == #close ifTrue: [
		c := in next.
		c notNil ifTrue: [self countBytes: 1].
		^c].
	mode == #chunked ifTrue: [
		[remaining = 0] whileTrue: [
			remaining := self nextChunkSize.
			mode == #none ifTrue: [^nil]]].
	remaining = 0 ifTrue: [^nil].
	c := in next.
	c isNil ifTrue: [
		"Declared length, and the connection closed before it: the same
		 truncation upToEnd refuses, refused the same way.  See NET-4."
		mode := #none.
		^NetError new signal: 'the reply declared ', remaining printString,
			' more bytes and the connection closed'].
	remaining := remaining - 1.
	self countBytes: 1.
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
	| out size piece |
	mode == #none ifTrue: [^''].
	mode == #close ifTrue: [
		"By the block rather than `in contents', which reads until the peer
		 closes however long that takes and however much it sends.  The
		 total is counted, so a peer that never stops meets the limit
		 instead of the end of memory."
		mode := #none.
		out := WriteStream on: (String new: 4096).
		[piece := in next: 65536.
		 piece isEmpty] whileFalse: [
			self countBytes: piece size.
			out nextPutAll: piece].
		^out contents].
	mode == #length ifTrue: [ | wanted |
		wanted := remaining.
		out := in next: wanted.
		self countBytes: out size.
		remaining := 0.
		mode := #none.
		"SHORT IS NOT DONE.  in next: answers fewer bytes only at end of
		 stream, so a peer that declared a length and then closed early
		 handed back a piece of a document and nothing said so: Bugs4
		 NET-4 got status 200 and a truncated body, indistinguishable from
		 the whole of a short one.  A truncated JSON document usually
		 fails to parse, and a truncated anything else does not -- half a
		 CSV is a valid CSV.  Chunked truncation was already caught, by
		 the missing terminating chunk; this is the same rule on the other
		 framing."
		out size < wanted ifTrue: [
			^NetError new signal: 'the reply declared ', wanted printString,
				' bytes and the connection closed after ', out size printString].
		^out].
	out := WriteStream on: (String new: 4096).
	remaining > 0 ifTrue: [
		piece := in next: remaining.
		self countBytes: piece size.
		out nextPutAll: piece.
		piece size < remaining ifTrue: [
			^NetError new signal: 'the connection closed inside a chunk of ',
				remaining printString, ' bytes'].
		remaining := 0].
	[size := self nextChunkSize.
	 size > 0] whileTrue: [
		piece := in next: size.
		self countBytes: piece size.
		out nextPutAll: piece.
		piece size < size ifTrue: [
			^NetError new signal: 'the connection closed inside a chunk of ',
				size printString, ' bytes']].
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

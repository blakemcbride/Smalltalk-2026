"
A Stream over a Socket: read by the character or the line, write and flush.

This is what a protocol parser reads from.  A Socket answers whatever has
arrived, which is a fragment of a line as often as a line; this buffers what
came so that nextLine and upTo: read what they say and leave the rest for the
next call.  Writes are buffered too and go out on flush, or when the buffer
fills, so a response built from many small nextPutAll:s crosses in one send.

BYTES ARE BYTES.  A String in this image is a sequence of bytes and so is
what a socket carries; nothing here translates a line ending, because the
protocol on the other side said what its line ending is and it was not the
Alto's.  nextLine strips the CR before an LF because that is what HTTP and
every text protocol since 1969 sends, and answers a String without either.

A read past the end of stream answers nil from next and an empty String from
nextLine, which is how the caller finds out the peer has gone; nothing raises
for that, since a peer closing is the ordinary end of every conversation.

readTimeout: bounds every wait for data.  A server keeping a connection open
between requests is waiting on a client that may never speak again, and a
process that waits for ever holds the socket and its buffers for ever.  When
the time runs out the read raises TimedOut, and the connection is the caller's
to close.
"
Class {
	#name : 'SocketStream',
	#superclass : 'Stream',
	#instVars : [ 'socket', 'inBuffer', 'inPosition', 'inLimit', 'outBuffer', 'outPosition', 'atEnd', 'readTimeout' ],
	#category : 'Network'
}

{ #category : 'instance creation' }
SocketStream class >> on: aSocket [
	^self basicNew setSocket: aSocket
]

{ #category : 'private' }
SocketStream >> setSocket: aSocket [
	socket := aSocket.
	inBuffer := String new: 65536.
	inPosition := 1.
	inLimit := 0.
	outBuffer := String new: 65536.
	outPosition := 0.
	atEnd := false.
	readTimeout := nil
]

{ #category : 'accessing' }
SocketStream >> socket [
	^socket
]

{ #category : 'accessing' }
SocketStream >> readTimeout: millisecondsOrNil [
	"How long a read may wait for data before raising TimedOut; nil for as
	 long as it takes."
	readTimeout := millisecondsOrNil
]

{ #category : 'private' }
SocketStream >> fill [
	"Read more from the socket into the buffer.  Answer whether anything
	 came: false means end of stream, and atEnd is set."

	| count |
	atEnd ifTrue: [^false].
	count := readTimeout isNil
		ifTrue: [socket receiveInto: inBuffer]
		ifFalse: [socket receiveInto: inBuffer timeout: readTimeout].
	count isNil ifTrue: [
		^(TimedOut new) tag: socket; signal: 'no data arrived in ', readTimeout printString, ' ms'].
	count = 0 ifTrue: [atEnd := true. ^false].
	inPosition := 1.
	inLimit := count.
	^true
]

{ #category : 'private' }
SocketStream >> available [
	"Bytes buffered and not yet read."
	^inLimit - inPosition + 1
]

{ #category : 'testing' }
SocketStream >> atEnd [
	"Whether nothing more will ever come.  Asking may have to read."
	self available > 0 ifTrue: [^false].
	^self fill not
]

{ #category : 'reading' }
SocketStream >> next [
	"The next Character, or nil at end of stream."
	| c |
	self available = 0 ifTrue: [self fill ifFalse: [^nil]].
	c := inBuffer at: inPosition.
	inPosition := inPosition + 1.
	^c
]

{ #category : 'reading' }
SocketStream >> peek [
	self available = 0 ifTrue: [self fill ifFalse: [^nil]].
	^inBuffer at: inPosition
]

{ #category : 'reading' }
SocketStream >> next: anInteger [
	"Exactly anInteger characters as a String, or fewer only at end of
	 stream."

	| out got n |
	out := String new: anInteger.
	got := 0.
	[got < anInteger] whileTrue: [
		self available = 0 ifTrue: [
			self fill ifFalse: [^out copyFrom: 1 to: got]].
		n := (anInteger - got) min: self available.
		out replaceFrom: got + 1 to: got + n with: inBuffer startingAt: inPosition.
		inPosition := inPosition + n.
		got := got + n].
	^out
]

{ #category : 'reading' }
SocketStream >> upTo: aCharacter [
	"Everything up to aCharacter, which is consumed and not answered; at end
	 of stream, everything that was left."

	| out c |
	out := WriteStream on: (String new: 128).
	[(c := self next) isNil or: [c == aCharacter]] whileFalse: [out nextPut: c].
	^out contents
]

{ #category : 'reading' }
SocketStream >> nextLine [
	"A line without its ending: up to LF, with a CR before the LF dropped.
	 An empty String at end of stream."

	| line |
	line := self upTo: (Character value: 10).
	(line size > 0 and: [line last = (Character value: 13)])
		ifTrue: [^line copyFrom: 1 to: line size - 1].
	^line
]

{ #category : 'writing' }
SocketStream >> nextPut: aCharacter [
	outPosition = outBuffer size ifTrue: [self flush].
	outPosition := outPosition + 1.
	outBuffer at: outPosition put: aCharacter.
	^aCharacter
]

{ #category : 'writing' }
SocketStream >> nextPutAll: aString [
	"Buffer aString; a String larger than the buffer goes straight out."

	| n |
	aString size > outBuffer size ifTrue: [
		self flush.
		socket send: aString.
		^aString].
	aString size > (outBuffer size - outPosition) ifTrue: [self flush].
	n := aString size.
	outBuffer replaceFrom: outPosition + 1 to: outPosition + n with: aString startingAt: 1.
	outPosition := outPosition + n.
	^aString
]

{ #category : 'writing' }
SocketStream >> space [
	self nextPut: Character space
]

{ #category : 'writing' }
SocketStream >> tab [
	self nextPut: Character tab
]

{ #category : 'writing' }
SocketStream >> cr [
	"A carriage return, and nothing else: no protocol here wants a line
	 feed added behind it, and one that wants CRLF says so."
	self nextPut: Character cr
]

{ #category : 'writing' }
SocketStream >> flush [
	"Send what is buffered."
	outPosition = 0 ifTrue: [^self].
	socket send: (outBuffer copyFrom: 1 to: outPosition).
	outPosition := 0
]

{ #category : 'closing' }
SocketStream >> close [
	"Flush what is waiting and close the socket.  A socket that has failed
	 cannot take the flush; it is closed regardless."
	[self flush] ensure: [socket close]
]

{ #category : 'accessing' }
SocketStream >> contents [
	"Everything up to end of stream.  What a client reading a whole reply
	 wants; a server does not send this."
	| out buffer count |
	out := WriteStream on: (String new: 1024).
	self available > 0 ifTrue: [
		out nextPutAll: (inBuffer copyFrom: inPosition to: inLimit).
		inPosition := inLimit + 1].
	buffer := String new: 65536.
	[atEnd] whileFalse: [
		count := socket receiveInto: buffer.
		count = 0
			ifTrue: [atEnd := true]
			ifFalse: [out nextPutAll: (buffer copyFrom: 1 to: count)]].
	^out contents
]

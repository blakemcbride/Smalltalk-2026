"
The stream over a socket: lines, characters, counted reads, buffered writes,
and the timeout.

The producer is another process wherever the shape of the data matters --
a line split across two sends, a body larger than a buffer -- because a
stream that only ever sees whole lines in one fragment is a stream that has
not been tested.
"
Class {
	#name : 'SocketStreamTest',
	#superclass : 'TestCase',
	#instVars : [ 'server', 'client', 'accepted', 'in', 'out' ],
	#category : 'Network-Tests'
}

{ #category : 'running' }
SocketStreamTest >> setUp [
	Socket isAvailable ifFalse: [^self].
	server := ServerSocket listenOn: 0 address: '127.0.0.1' backlog: 8.
	client := Socket connectTo: '127.0.0.1' port: server localPort.
	accepted := server accept.
	out := SocketStream on: client.
	in := SocketStream on: accepted
]

{ #category : 'running' }
SocketStreamTest >> tearDown [
	accepted notNil ifTrue: [accepted close].
	client notNil ifTrue: [client close].
	server notNil ifTrue: [server close]
]

{ #category : 'tests' }
SocketStreamTest >> testLines [
	out nextPutAll: 'GET / HTTP/1.1'; nextPutAll: (String with: (Character value: 13) with: (Character value: 10)).
	out nextPutAll: 'Host: here'; nextPut: (Character value: 10).
	out nextPutAll: (String with: (Character value: 13) with: (Character value: 10)).
	out flush.
	self assert: in nextLine equals: 'GET / HTTP/1.1'.
	self assert: in nextLine equals: 'Host: here'.
	self assert: in nextLine equals: ''
]

{ #category : 'tests' }
SocketStreamTest >> testALineSplitAcrossTwoSends [
	"The two halves arrive separately; the reader waits for the second."
	| promise |
	promise := Processor forkParallel: [
		client send: 'first half, '.
		(Delay forMilliseconds: 30) wait.
		client send: 'second half'.
		client send: (String with: (Character value: 10)).
		true].
	self assert: in nextLine equals: 'first half, second half'.
	self assert: promise value
]

{ #category : 'tests' }
SocketStreamTest >> testCharacters [
	out nextPutAll: 'abc'; flush.
	self assert: in peek equals: $a.
	self assert: in next equals: $a.
	self assert: in next equals: $b.
	self assert: in next equals: $c.
	client shutdownWrite.
	self assert: in next isNil.
	self assert: in atEnd
]

{ #category : 'tests' }
SocketStreamTest >> testCountedReadsAcrossFragments [
	"next: takes exactly what it asked for, however the bytes were split,
	 and only at end of stream takes less."
	| promise body |
	body := String new: 100000.
	1 to: body size do: [:i | body at: i put: (Character value: 65 + (i \\ 26))].
	promise := Processor forkParallel: [
		client send: 'Content-Length: 100000'.
		client send: (String with: (Character value: 10)).
		1 to: 100000 by: 7000 do: [:start |
			client send: (body copyFrom: start to: (start + 6999 min: 100000))].
		client shutdownWrite.
		true].
	self assert: in nextLine equals: 'Content-Length: 100000'.
	self assert: (in next: 100000) equals: body.
	self assert: (in next: 10) equals: ''.
	self assert: promise value
]

{ #category : 'tests' }
SocketStreamTest >> testBufferedWritesCrossOnFlush [
	"Small writes stay in the buffer until flush; the reader sees nothing
	 before and everything after."
	out nextPutAll: 'one '; nextPutAll: 'two '; nextPutAll: 'three'.
	self deny: (accepted waitReadableTimeout: 40).
	out flush.
	self assert: (in next: 13) equals: 'one two three'
]

{ #category : 'tests' }
SocketStreamTest >> testAWriteLargerThanTheBufferGoesStraightOut [
	| big |
	big := String new: 70000 withAll: $z.
	out nextPutAll: big; flush.
	self assert: (in next: 70000) equals: big
]

{ #category : 'tests' }
SocketStreamTest >> testUpTo [
	out nextPutAll: 'name=value&other'; flush.
	self assert: (in upTo: $=) equals: 'name'.
	self assert: (in upTo: $&) equals: 'value'.
	client shutdownWrite.
	self assert: (in upTo: $&) equals: 'other'
]

{ #category : 'tests' }
SocketStreamTest >> testContents [
	out nextPutAll: 'the whole reply'; flush.
	client shutdownWrite.
	self assert: in contents equals: 'the whole reply'
]

{ #category : 'tests' }
SocketStreamTest >> testReadTimeout [
	"A reader that has been told how long to wait raises TimedOut when
	 nothing comes, and reads what does come afterwards."
	in readTimeout: 60.
	self should: [in next] raise: TimedOut.
	out nextPutAll: 'x'; flush.
	self assert: in next equals: $x
]

{ #category : 'tests' }
SocketStreamTest >> testCloseFlushes [
	out nextPutAll: 'last words'.
	out close.
	self assert: in contents equals: 'last words'
]

{ #category : 'tests' }
SocketStreamTest >> testTheWholeReadHasADeadlineOfItsOwn [
	"Bugs4.  readTimeout: bounds ONE read, which bounds a peer that says
	 nothing -- and a peer that says one byte at a time, slowly, is never
	 silent: every read meets its timeout and the conversation has no
	 bound at all.  deadlineIn: is the bound on what is left to read, and
	 the two are compared in fill, where every read here goes."
	| listener stop |
	stop := false.
	listener := ServerSocket listenOn: 0 address: '127.0.0.1' backlog: 4.
	[[[:l | | c |
		c := l accept.
		[[stop] whileFalse: [c send: 'x'. (Delay forMilliseconds: 20) wait]]
			on: Error do: [:e | nil].
		c close] value: listener] on: Error do: [:e | nil]] fork.
	[ | socket in |
	  socket := Socket connectTo: '127.0.0.1' port: listener localPort.
	  [in := SocketStream on: socket.
	   in readTimeout: 5000.
	   in deadlineIn: 200.
	   "Ten thousand bytes at ten a second is a quarter of an hour; the
	    deadline ends it in four hundred milliseconds."
	   self should: [in next: 10000] raise: TimedOut.
	   "A deadline of nil is no deadline, which is what every other user
	    of this class still has."
	   in deadlineIn: nil] ensure: [stop := true. socket close]]
		ensure: [listener close]
]

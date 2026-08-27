"
A TCP connection, and the only class in this package that knows a primitive
exists.

Everything above this -- ServerSocket, SocketStream, the HTTP server, the REST
dispatcher -- is ordinary Smalltalk sending ordinary messages here.  That is the
arrangement Odbc has with primitive 129, for the same reasons: the package can
be read without knowing sockets, and there is exactly one file to look in when
the network does something the network should not.

ONE PRIMITIVE, NOT TWENTY.  Primitive 208 takes a command number and three
operands, the way 129 is the whole database and 130 the whole file system.  A
subsystem that grows should grow inside its own number.

WHAT THE PRIMITIVE ANSWERS, AND WHAT THAT MEANS.  Three answers, and the
difference between them is the whole of how this class works:

    The primitive FAILS -- the fallback code runs -- when the ARGUMENTS were
    wrong.  That is a bug in this file, and it raises saying so.

    The primitive answers NIL when the OPERATING SYSTEM said no: the port is
    in use, the peer reset the connection, the handle names a socket that is
    gone.  That is news, and lastError carries the system's own words.

    The primitive answers FALSE when the call could not complete YET.  Every
    socket here is non-blocking, so a receive with nothing arrived and an
    accept with nobody connecting both answer false, and false is the ordinary
    case rather than an error.

HOW WAITING WORKS, because it is the point of the design.  A worker never
blocks in the primitive.  When a call answers false the socket is ARMED -- the
VM's network thread is asked to say when the socket is next readable, or
writable -- and the process waits on a Semaphore that the thread signals.
Waiting on a Semaphore costs the process its turn and costs the worker
nothing: it runs somebody else.  So a hundred idle connections are a hundred
parked processes and no thread at all, which is what lets the pool be spent on
work.  The contract with the VM is one-shot -- an arm is answered by exactly
one signal -- and every wait here is the same loop:

    call -> false -> arm -> wait -> call again

which tolerates a spurious wake: the call answers false again, the arm is
already in place, and the wait resumes.  That tolerance is what makes a
timeout possible with no VM support, see waitReadableTimeout:.

ONE READER AND ONE WRITER at a time.  Two processes receiving from one socket
would both arm and both wait, and one signal would wake one of them; the other
waits for a byte the first took.  Sharing a socket between processes is a
program error, and this class does not try to make it work.

Closing a socket somebody is waiting on wakes them: close signals both
semaphores after the handle is gone, so the waiter's next call finds no socket
and raises rather than sleeping on for ever.

A handle survives a snapshot as a number and nothing else.  The VM tags every
handle with a generation and finds a resumed image's handles in no slot, so a
Socket read back from an image is closed, and says so.

TLS, on the client side.  startTls: turns a connected socket into the client
end of a TLS connection, through OpenSSL when the build found it and refused
by name when it did not; the bytes then go through the same receiveInto: and
send:, encrypted underneath.  The certificate is checked against the system's
store and its name against the host asked for, and neither check can be
turned off, on purpose.  What TLS adds to the waiting is one thing: a read
may have to WRITE first and a write may have to READ first -- the handshake,
a key update -- so the primitive has a fourth answer beside a count, nil and
false: -1 means wait readable and -2 means wait writable, whichever call was
made.  A plain socket never answers them, and false still means what it
always meant.  There is no server side: a server here sits behind a reverse
proxy that speaks TLS for it.
"
Class {
	#name : 'Socket',
	#superclass : 'Object',
	#instVars : [ 'handle', 'readSemaphore', 'writeSemaphore' ],
	#category : 'Network'
}

{ #category : 'primitives' }
Socket class >> primCommand: commandInteger with: a with: b with: c [
	"The one primitive.  See the class comment.

	 Failing here means the arguments were not what the command wanted,
	 which is a fault in this file rather than a fault in the network, so
	 it raises rather than answering nil."

	<primitive: 208>
	^NetError signal: 'the socket primitive was called wrongly: command ',
		commandInteger printString
]

{ #category : 'availability' }
Socket class >> isAvailable [
	"Whether this build has sockets at all.  The Blue Book memory has not:
	 its SmallIntegers cannot hold a handle, and it has no worker pool for
	 a server to run on."

	^self primCommand: 0 with: nil with: nil with: nil
]

{ #category : 'availability' }
Socket class >> isTlsAvailable [
	"Whether this build can speak TLS: OpenSSL was found when it was
	 built.  Without it startTls: raises, and HttpClient refuses an https
	 URL by name."

	^self primCommand: 21 with: nil with: nil with: nil
]

{ #category : 'services' }
Socket class >> environmentAt: aName [
	"The environment variable's value as a String, or nil when there is
	 none.  Smalltalk environmentAt: is the spelling to use; it is here
	 because it has the arguments' shape and the same customer -- an API
	 key read from where the shell put it."

	^self primCommand: 25 with: aName with: nil with: nil
]

{ #category : 'availability' }
Socket class >> lastError [
	"What the operating system said about the most recent failure, on THIS
	 process.  Per process and not per socket, because the call that fails
	 most often is the one that was going to produce the socket."

	^self primCommand: 1 with: nil with: nil with: nil
]

{ #category : 'availability' }
Socket class >> lastErrno [
	^self primCommand: 2 with: nil with: nil with: nil
]

{ #category : 'private' }
Socket class >> check: answer [
	"Answer what the primitive answered, or raise with the system's own words."

	answer isNil ifTrue: [
		^(NetError new) errno: self lastErrno; signal: self lastError].
	^answer
]

{ #category : 'instance creation' }
Socket class >> connectTo: hostString port: portInteger [
	"Open a connection to host:port, waiting until it is made, and answer
	 the Socket.  Raises NetError if the host cannot be reached or refuses.

	 The connect begins at once and completes in the background; the
	 socket is writable when it has, so the wait is a wait for writable
	 and the answer is then asked for.

	 Every address the name resolves to is tried, in the resolver's order,
	 and the error raised is the last address's.  A name can resolve to
	 several addresses and a server may listen on one of them: `localhost'
	 is ::1 and then 127.0.0.1 on many machines, and an Ollama listens on
	 127.0.0.1 alone -- which was a `connection refused' from a server that
	 was up, until this looped.  The VM connects to one address at a time
	 (command 11's third argument says which) because a non-blocking
	 connect learns of a refusal only from connectResult, later; so the
	 loop is here, and command 20 says how many addresses there are."

	| count last |
	count := self check: (self primCommand: 20 with: hostString with: portInteger with: nil).
	0 to: count - 1 do: [:i | | socket |
		socket := self new.
		[socket setHandle: (self check:
			(self primCommand: 11 with: hostString with: portInteger with: i)).
		 [socket connectResult] whileFalse: [socket waitWritable].
		 ^socket]
			on: NetError
			do: [:e | socket close. last := e]].
	^(NetError new) errno: last errno; signal: last messageText
]

{ #category : 'instance creation' }
Socket class >> onHandle: anInteger [
	"A socket the VM already made -- what accept answers."
	^self new setHandle: anInteger
]

{ #category : 'services' }
Socket class >> randomBytes: anInteger [
	"A String of anInteger bytes from the system's entropy source, for
	 session identifiers and the like.  Not for printing."

	| bytes |
	bytes := String new: anInteger.
	self check: (self primCommand: 16 with: bytes with: nil with: nil).
	^bytes
]

{ #category : 'services' }
Socket class >> arguments [
	"The words after `st80 -serve <image> [-workers n]', as an Array of
	 Strings; empty in every other run mode.  Smalltalk arguments is the
	 spelling to use."

	^self primCommand: 17 with: nil with: nil with: nil
]

{ #category : 'services' }
Socket class >> stopRequested [
	"Whether the operating system has asked this server to stop -- SIGINT,
	 SIGTERM, a console control.  A listener loop that checks this can close
	 itself and let the requests in flight finish."

	^self primCommand: 18 with: nil with: nil with: nil
]

{ #category : 'services' }
Socket class >> openCount [
	"How many sockets the VM holds open, listeners included."
	^self primCommand: 19 with: nil with: nil with: nil
]

{ #category : 'private' }
Socket >> setHandle: anInteger [
	"Take ownership of a handle the VM made, and give it the two Semaphores
	 the network thread will signal.  The VM counts them from here until
	 close, so a Socket the image drops still has its semaphores while a
	 wake for them is on its way."

	handle := anInteger.
	readSemaphore := Semaphore new.
	writeSemaphore := Semaphore new.
	self class check:
		(self class primCommand: 9 with: handle with: readSemaphore with: writeSemaphore)
]

{ #category : 'accessing' }
Socket >> handle [
	^handle
]

{ #category : 'testing' }
Socket >> isOpen [
	^handle notNil
]

{ #category : 'private' }
Socket >> checkOpen [
	handle isNil ifTrue: [^NetError new signal: 'the socket is closed']
]

{ #category : 'private' }
Socket >> connectResult [
	"true once the connect has completed, false while it is still in
	 progress, and a NetError if it failed."

	self checkOpen.
	^self class check: (self class primCommand: 12 with: handle with: nil with: nil)
]

{ #category : 'waiting' }
Socket >> arm: maskInteger [
	"Ask to be signalled once, when next readable (1), writable (2), or
	 either (3).  One-shot: see the class comment."

	self checkOpen.
	self class check: (self class primCommand: 10 with: handle with: maskInteger with: nil)
]

{ #category : 'waiting' }
Socket >> waitReadable [
	"Arm for reading and wait for the signal.  The caller calls again
	 afterwards, and treats a false answer as a reason to come back here."

	self arm: 1.
	readSemaphore wait.
	self checkOpen
]

{ #category : 'waiting' }
Socket >> waitWritable [
	self arm: 2.
	writeSemaphore wait.
	self checkOpen
]

{ #category : 'waiting' }
Socket >> waitReadableTimeout: milliseconds [
	"Arm for reading and wait up to milliseconds for the signal.  Answer
	 true if it came and false if the time ran out first.

	 A watchdog process signals the same semaphore when the time is up,
	 and does nothing if the data arrived first -- almost.  It can read
	 `done' an instant before the waiter sets it and signal anyway, which
	 leaves one excess signal on the semaphore.  That is harmless by the
	 contract every wait here keeps: the next wait returns at once, the
	 call answers false again, and the wait resumes.  The interest stays
	 armed after a timeout for the same reason: a signal nobody is waiting
	 for is an excess signal, not a lost one."

	^self wait: 1 on: readSemaphore timeout: milliseconds
]

{ #category : 'waiting' }
Socket >> waitWritableTimeout: milliseconds [
	"As waitReadableTimeout:, for the other direction: what a timed read
	 does when TLS answers that the read must first write."

	^self wait: 2 on: writeSemaphore timeout: milliseconds
]

{ #category : 'private' }
Socket >> wait: maskInteger on: aSemaphore timeout: milliseconds [
	| deadline done |
	self arm: maskInteger.
	deadline := Time millisecondClockValue + milliseconds.
	done := false.
	[(Delay forMilliseconds: milliseconds) wait.
	 done ifFalse: [aSemaphore signal]] fork.
	aSemaphore wait.
	done := true.
	self checkOpen.
	^Time millisecondClockValue < deadline
]

{ #category : 'private' }
Socket >> waitOn: answer default: maskInteger [
	"Whether the primitive's answer was `not yet' -- and if it was, the
	 wait is taken here first.  false is `not yet, the obvious way', which
	 is maskInteger's direction; -1 and -2 are TLS saying the other way,
	 readable and writable.  Anything else is a result for the caller."

	answer == false ifTrue: [
		maskInteger = 1 ifTrue: [self waitReadable] ifFalse: [self waitWritable].
		^true].
	answer == -1 ifTrue: [self waitReadable. ^true].
	answer == -2 ifTrue: [self waitWritable. ^true].
	^false
]

{ #category : 'reading' }
Socket >> receiveInto: aString [
	"Fill aString from the connection, waiting until at least one byte has
	 arrived, and answer how many were read: 1 to aString size, or 0 at end
	 of stream.  Raises NetError if the connection failed."

	| answer |
	[self checkOpen.
	 answer := self class primCommand: 5 with: handle with: aString with: aString size.
	 self waitOn: answer default: 1] whileTrue.
	^self class check: answer
]

{ #category : 'reading' }
Socket >> receiveInto: aString timeout: milliseconds [
	"As receiveInto:, but give up after milliseconds and answer nil.

	 The receive is what decides: a timed wait may end early for reasons
	 of its own -- a watchdog and a wake arriving together -- so its answer
	 is advice about whether to try again, and the deadline is kept here,
	 where a receive that still answers `not yet' after it has passed is
	 the timeout."

	| deadline answer |
	deadline := Time millisecondClockValue + milliseconds.
	[self checkOpen.
	 answer := self class primCommand: 5 with: handle with: aString with: aString size.
	 answer == false or: [answer == -1 or: [answer == -2]]] whileTrue: [ | left |
		left := deadline - Time millisecondClockValue.
		left <= 0 ifTrue: [^nil].
		answer == -2
			ifTrue: [self waitWritableTimeout: left]
			ifFalse: [self waitReadableTimeout: left]].
	^self class check: answer
]

{ #category : 'reading' }
Socket >> receiveUpTo: anInteger [
	"Answer a String of what has arrived, at most anInteger bytes, waiting
	 for at least one; an empty String at end of stream."

	| buffer count |
	buffer := String new: anInteger.
	count := self receiveInto: buffer.
	^count = anInteger ifTrue: [buffer] ifFalse: [buffer copyFrom: 1 to: count]
]

{ #category : 'writing' }
Socket >> send: aString [
	"Send the whole of aString, waiting whenever the kernel's buffer is
	 full.  Answers the receiver.  A send of a partial buffer is the
	 ordinary case on a busy connection, hence the loop from wherever the
	 last one stopped."

	| start answer |
	start := 1.
	[start <= aString size] whileTrue: [
		self checkOpen.
		answer := self class primCommand: 6 with: handle with: aString with: start.
		(self waitOn: answer default: 2)
			ifFalse: [start := start + (self class check: answer)]]
]

{ #category : 'tls' }
Socket >> startTls: hostName [
	"Make this connected socket the client end of a TLS connection to
	 hostName, and complete the handshake, waiting as every wait here
	 waits.  The certificate must be one the system's store vouches for
	 and must be for hostName, or this raises with OpenSSL's reason; so
	 does a peer that does not speak TLS, and a build that has none.
	 After it, receiveInto: and send: carry the bytes encrypted."

	| answer |
	self checkOpen.
	self class check: (self class primCommand: 22 with: handle with: hostName with: nil).
	[self checkOpen.
	 answer := self class primCommand: 23 with: handle with: nil with: nil.
	 self waitOn: answer default: 1] whileTrue.
	self class check: answer
]

{ #category : 'tls' }
Socket >> startTls: hostName timeout: milliseconds [
	"As startTls:, giving up with a TimedOut when the handshake has not
	 finished in milliseconds -- a peer that accepts the connection and
	 then says nothing would otherwise hold the process for ever, which
	 an HTTP client's read timeout is meant to prevent and could not."

	| deadline answer |
	self checkOpen.
	self class check: (self class primCommand: 22 with: handle with: hostName with: nil).
	deadline := Time millisecondClockValue + milliseconds.
	[self checkOpen.
	 answer := self class primCommand: 23 with: handle with: nil with: nil.
	 answer == -1 or: [answer == -2]] whileTrue: [ | left |
		left := deadline - Time millisecondClockValue.
		left <= 0 ifTrue: [
			^(TimedOut new) tag: self; signal: 'the TLS handshake did not finish in ', milliseconds printString, ' ms'].
		answer == -2
			ifTrue: [self waitWritableTimeout: left]
			ifFalse: [self waitReadableTimeout: left]].
	self class check: answer
]

{ #category : 'tls' }
Socket >> isSecure [
	"Whether startTls: has been sent to this socket."
	handle isNil ifTrue: [^false].
	^self class primCommand: 24 with: handle with: nil with: nil
]

{ #category : 'writing' }
Socket >> shutdownWrite [
	"Say that nothing more will be sent.  The peer reads end of stream; what
	 it sends back can still be read."

	self checkOpen.
	self class check: (self class primCommand: 8 with: handle with: nil with: nil)
]

{ #category : 'closing' }
Socket >> close [
	"Close, and wake anybody waiting on this socket so that they find it
	 closed rather than sleeping for ever.  Closing twice is fine."

	| old |
	handle isNil ifTrue: [^self].
	old := handle.
	handle := nil.
	self class primCommand: 7 with: old with: nil with: nil.
	readSemaphore signal.
	writeSemaphore signal
]

{ #category : 'accessing' }
Socket >> peerAddress [
	"The peer's address as text -- `127.0.0.1', or an IPv6 address."
	self checkOpen.
	^self class check: (self class primCommand: 14 with: handle with: nil with: nil)
]

{ #category : 'accessing' }
Socket >> localPort [
	self checkOpen.
	^self class check: (self class primCommand: 13 with: handle with: nil with: nil)
]

{ #category : 'options' }
Socket >> noDelay: aBoolean [
	"Nagle off (true) or on.  A server writing a response in one piece
	 wants it off."
	self checkOpen.
	self class check: (self class primCommand: 15 with: handle with: 0 with: aBoolean)
]

{ #category : 'options' }
Socket >> keepAlive: aBoolean [
	self checkOpen.
	self class check: (self class primCommand: 15 with: handle with: 1 with: aBoolean)
]

{ #category : 'printing' }
Socket >> printOn: aStream [
	aStream nextPutAll: self class name.
	handle isNil
		ifTrue: [aStream nextPutAll: ' (closed)']
		ifFalse: [aStream nextPutAll: ' #'; print: handle]
]

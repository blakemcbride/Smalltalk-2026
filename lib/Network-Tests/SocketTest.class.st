"
Sockets over the loopback interface, on port 0.

Every test here makes its own listener on a port the system picks, connects to
it from the same image, and closes both ends in an ensure:, so the suite needs
no network, no fixed port, and nothing another test left behind.  These run
under `st80 -bootstrap -tests', where there is one interpreter and every
process is green; the arm-wait-retry contract is the same there as on thirty
workers, because the VM's network thread is the same thread in both.

What the tests assert is arithmetic where they can: bytes sent equal bytes
received, a 300,000-byte transfer arrives intact and in order through a
producer on another process, and a close from one process wakes a waiter on
another rather than leaving it asleep.
"
Class {
	#name : 'SocketTest',
	#superclass : 'TestCase',
	#instVars : [ 'server', 'client', 'accepted' ],
	#category : 'Network-Tests'
}

{ #category : 'running' }
SocketTest >> setUp [
	Socket isAvailable ifFalse: [^self].
	server := ServerSocket listenOn: 0 address: '127.0.0.1' backlog: 8
]

{ #category : 'running' }
SocketTest >> tearDown [
	accepted notNil ifTrue: [accepted close].
	client notNil ifTrue: [client close].
	server notNil ifTrue: [server close]
]

{ #category : 'private' }
SocketTest >> connect [
	"A connection from this image to its own listener, both ends open."
	client := Socket connectTo: '127.0.0.1' port: server localPort.
	accepted := server accept
]

{ #category : 'tests' }
SocketTest >> testAvailable [
	self assert: Socket isAvailable
]

{ #category : 'tests' }
SocketTest >> testListenerHasAPort [
	self assert: server localPort > 0.
	self assert: server isOpen
]

{ #category : 'tests' }
SocketTest >> testConnectAndAccept [
	self connect.
	self assert: client isOpen.
	self assert: accepted isOpen.
	self assert: accepted peerAddress equals: '127.0.0.1'.
	self assert: client peerAddress equals: '127.0.0.1'
]

{ #category : 'tests' }
SocketTest >> testBytesBothWays [
	| buffer count |
	self connect.
	client send: 'hello, server'.
	buffer := String new: 64.
	count := accepted receiveInto: buffer.
	self assert: count equals: 13.
	self assert: (buffer copyFrom: 1 to: count) equals: 'hello, server'.
	accepted send: 'hello, client'.
	self assert: (client receiveUpTo: 64) equals: 'hello, client'
]

{ #category : 'tests' }
SocketTest >> testEndOfStream [
	"A peer that shuts its writing side down is read as end of stream, and
	 the other direction still works."
	self connect.
	client shutdownWrite.
	self assert: (accepted receiveInto: (String new: 16)) equals: 0.
	accepted send: 'bye'.
	self assert: (client receiveUpTo: 16) equals: 'bye'
]

{ #category : 'tests' }
SocketTest >> testAcceptWaitsForALateClient [
	"accept before anybody connects: the acceptor parks, a process forked
	 afterwards connects, and the acceptor wakes with the connection."
	| promise |
	promise := Processor forkParallel: [server accept].
	client := Socket connectTo: '127.0.0.1' port: server localPort.
	accepted := promise value.
	self assert: accepted isOpen.
	client send: 'late'.
	self assert: (accepted receiveUpTo: 8) equals: 'late'
]

{ #category : 'tests' }
SocketTest >> testALargeTransferArrivesIntact [
	"300,000 bytes through the kernel's buffers, which are smaller than
	 that: the sender waits for room more than once and the receiver reads
	 many fragments.  Checked byte for byte against what was sent."
	| text promise received count buffer |
	self connect.
	text := String new: 300000.
	1 to: text size do: [:i | text at: i put: (Character value: 32 + (i \\ 90))].
	promise := Processor forkParallel: [accepted send: text. accepted shutdownWrite. true].
	received := WriteStream on: (String new: text size).
	buffer := String new: 8192.
	[(count := client receiveInto: buffer) > 0] whileTrue: [
		received nextPutAll: (buffer copyFrom: 1 to: count)].
	self assert: promise value.
	self assert: received contents size equals: text size.
	self assert: received contents equals: text
]

{ #category : 'tests' }
SocketTest >> testCloseWakesAWaiter [
	"A process waiting for data on a socket another process closes wakes
	 up and finds the socket closed, rather than sleeping for ever."
	| promise |
	self connect.
	promise := Processor forkParallel: [
		[accepted receiveInto: (String new: 16). #read]
			on: NetError do: [:e | #closed]].
	(Delay forMilliseconds: 50) wait.
	accepted close.
	self assert: promise value equals: #closed
]

{ #category : 'tests' }
SocketTest >> testTwoConnectionsAtOnce [
	"Two clients, two accepted sockets, each pair its own conversation."
	| second acceptedSecond |
	self connect.
	second := Socket connectTo: '127.0.0.1' port: server localPort.
	acceptedSecond := server accept.
	[client send: 'one'.
	 second send: 'two'.
	 self assert: (acceptedSecond receiveUpTo: 8) equals: 'two'.
	 self assert: (accepted receiveUpTo: 8) equals: 'one']
		ensure: [second close. acceptedSecond close]
]

{ #category : 'tests' }
SocketTest >> testARefusedConnectRaises [
	"A port nobody listens on: the one the listener had, once it is closed."
	| port |
	port := server localPort.
	server close.
	server := nil.
	self should: [Socket connectTo: '127.0.0.1' port: port] raise: NetError
]

{ #category : 'tests' }
SocketTest >> testAClosedSocketRefusesEverything [
	self connect.
	client close.
	self deny: client isOpen.
	self should: [client send: 'x'] raise: NetError.
	self should: [client receiveInto: (String new: 8)] raise: NetError.
	self should: [client peerAddress] raise: NetError.
	client close			"twice is fine"
]

{ #category : 'tests' }
SocketTest >> testATimedWaitGivesUp [
	"Nothing arrives, and the wait says so after the time is up."
	| began |
	self connect.
	began := Time millisecondClockValue.
	self deny: (accepted waitReadableTimeout: 60).
	self assert: Time millisecondClockValue - began >= 50.
	"And data that then arrives is still read: the excess signal is harmless."
	client send: 'now'.
	self assert: (accepted receiveUpTo: 8) equals: 'now'
]

{ #category : 'tests' }
SocketTest >> testATimedWaitEndsEarlyWhenDataComes [
	self connect.
	client send: 'here'.
	self assert: (accepted waitReadableTimeout: 5000).
	self assert: (accepted receiveUpTo: 8) equals: 'here'
]

{ #category : 'tests' }
SocketTest >> testAWaitSurvivesACollection [
	"A collection while a process is parked on a socket: the semaphores the
	 VM holds for the socket are roots, so the wake still arrives."
	| promise |
	self connect.
	promise := Processor forkParallel: [accepted receiveUpTo: 16].
	(Delay forMilliseconds: 20) wait.
	Smalltalk garbageCollect.
	client send: 'after'.
	self assert: promise value equals: 'after'
]

{ #category : 'tests' }
SocketTest >> testRandomBytes [
	| one two |
	one := Socket randomBytes: 16.
	two := Socket randomBytes: 16.
	self assert: one size equals: 16.
	self deny: one equals: two.
	self deny: (one allSatisfy: [:c | c value = 0])
]

{ #category : 'tests' }
SocketTest >> testArgumentsAnswerAnArray [
	"Empty here: a test run is not a -serve run."
	self assert: (Smalltalk arguments isKindOf: Array).
	self assert: Smalltalk arguments isEmpty
]

{ #category : 'tests' }
SocketTest >> testStopIsNotRequested [
	self deny: Socket stopRequested
]

{ #category : 'tests' }
SocketTest >> testOptions [
	self connect.
	client noDelay: true.
	client keepAlive: true.
	client send: 'ok'.
	self assert: (accepted receiveUpTo: 4) equals: 'ok'
]

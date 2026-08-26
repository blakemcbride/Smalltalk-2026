"
A listening socket: it makes Sockets and never carries bytes itself.

accept waits the way every wait in this package waits -- armed, on a
Semaphore the VM's network thread signals -- so a server with no clients yet
costs no worker at all.  The scheduler knows about the armed listener, which
is why an image whose only process is waiting in accept is a server with no
clients rather than a deadlock.
"
Class {
	#name : 'ServerSocket',
	#superclass : 'Socket',
	#category : 'Network'
}

{ #category : 'instance creation' }
ServerSocket class >> listenOn: portInteger [
	"Listen on every interface.  Port 0 asks for one the system picks, which
	 localPort then answers -- what a test wants."
	^self listenOn: portInteger address: nil backlog: 128
]

{ #category : 'instance creation' }
ServerSocket class >> listenOn: portInteger address: hostStringOrNil backlog: backlogInteger [
	"Listen on one interface -- `127.0.0.1' for loopback only, nil for all."
	| socket |
	socket := self new.
	socket setHandle: (self check:
		(self primCommand: 3 with: hostStringOrNil with: portInteger with: backlogInteger)).
	^socket
]

{ #category : 'accepting' }
ServerSocket >> accept [
	"Wait for a connection and answer it as a Socket."
	| answer |
	[self checkOpen.
	 answer := self class primCommand: 4 with: handle with: nil with: nil.
	 answer == false] whileTrue: [self waitReadable].
	^Socket onHandle: (self class check: answer)
]

{ #category : 'accepting' }
ServerSocket >> acceptTimeout: milliseconds [
	"Wait up to milliseconds for a connection; answer it, or nil.  The
	 accept decides, and the deadline is kept here -- see receiveInto:timeout:."
	| answer deadline |
	deadline := Time millisecondClockValue + milliseconds.
	[self checkOpen.
	 answer := self class primCommand: 4 with: handle with: nil with: nil.
	 answer == false] whileTrue: [ | left |
		left := deadline - Time millisecondClockValue.
		left <= 0 ifTrue: [^nil].
		self waitReadableTimeout: left].
	^Socket onHandle: (self class check: answer)
]

{ #category : 'reading' }
ServerSocket >> receiveInto: aString [
	^NetError new signal: 'a listening socket carries no bytes'
]

{ #category : 'writing' }
ServerSocket >> send: aString [
	^NetError new signal: 'a listening socket carries no bytes'
]

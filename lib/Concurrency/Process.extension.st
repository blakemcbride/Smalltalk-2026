"
Raising an exception inside a process that is not the one running.

A watchdog cannot report a timeout by signalling in itself: the handler that
cares is on the OTHER process's stack, and an exception only searches the
stack it was raised on.  So the signal has to happen over there.
"
Extension { #name : 'Process' }

{ #category : '*Concurrency' }
Process >> signalException: anException [
	"Arrange for this process to raise anException where it stands.

	 A frame that signals is pushed onto the suspended stack, so when this
	 process next runs it signals BEFORE continuing -- and the search for a
	 handler walks its own senders, which is the whole point.  A block's
	 newProcess is used to build that frame because it is the only way this
	 system makes a runnable context from Smalltalk; only its context is
	 wanted, never the process wrapped round it.

	 The receiver being the active process is not an error and must not go
	 through the machinery above: it is already here, so it can just signal."
	| frame |
	self == Processor activeProcess ifTrue: [^anException signal].
	suspendedContext isNil ifTrue: [^self].
	frame := [anException signal] newProcess suspendedContext.
	"Field one of any context is its sender; splicing it in is what makes
	 the signal appear to happen inside this process rather than beside it."
	frame instVarAt: 1 put: suspendedContext.
	suspendedContext := frame
]

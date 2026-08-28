"
Stopping a process from outside it, and raising an exception inside one.

1983's Process>>terminate and >>suspend, and the signalException: that was
written here beside them, share one assumption: a process that is not the
active process is not running.  Under the green scheduler that was a fact
-- it was on a list, or it was suspended, and either way its
suspendedContext was where it stood.  On a pool of workers it is false in
the case that matters most: a process running on another core has its
registers in that core's interpreter, and its suspendedContext is wherever
it was parked LAST time.  terminate unlinked it from nothing and cleared a
field nobody was reading, and the loop went on counting (Bugs3 B16);
suspend said 'This process was not active'; signalException: gave up on a
nil it found there.

The VM now does the part Smalltalk cannot.  primDetach: brings a process
to a stop wherever it is -- taken off its list under that list's lock, or
parked by the worker running it at its next bytecode -- and answers where
it was; the methods below go on from a process that is parked, on no list,
in nobody's hands.  primTerminateActive ends the active process itself.

terminate also runs the ensure: blocks the process was inside (Bugs3 B13).
1983's ran none: it predates unwind blocks, so a process terminated inside
`aMutex critical:' left the Mutex held for ever, and the next critical:
anywhere in the image waited on it.  The blocks run ON THE TERMINATING
PROCESS -- the one sending terminate -- innermost first, the way
Exception>>unwindTo: runs them and the way Squeak's terminate ran them
before it grew runUntilErrorOrReturnFrom:; that is the only model this
system's machinery supports, and it is enough for what an ensure: block
is for, releasing what the process held.  A block that asks which process
it is on gets the terminator, which is a thing to know and not a thing to
rely on.  runUnwindBlock disarms each frame it runs, so a block does not
run again if the frame is ever unwound a second time.

A process that has terminated has a nil suspendedContext, which is the
mark 1983 left and the mark primitive 87 now refuses (Bugs3 B3): resuming
a terminated process raises here instead of stopping the image.
"
Extension { #name : 'Process' }

{ #category : '*Concurrency' }
Process >> primDetach: takeFromSemaphore [
	"Bring me to a stop wherever I am, and answer where that was:

	     0  nowhere -- already suspended, terminated, or never resumed
	     1  running on a worker, or nominated to run; it has let go
	     2  waiting for the processor, on a ready list
	     3  waiting on a Semaphore, and taken off it
	     4  waiting on a Semaphore and LEFT there, takeFromSemaphore
	        being false

	 In every case but 4 I am afterwards parked, on no list, in nobody's
	 hands, with suspendedContext where I stopped.  Fails for the active
	 process, which cannot be stopped from inside itself this way."
	<primitive: 231>
	^self primitiveFailed
]

{ #category : '*Concurrency' }
Process >> primTerminateActive [
	"End the active process -- me -- for good: my suspendedContext is
	 nil from here on, and this worker goes to run something else.  Does
	 not return."
	<primitive: 232>
	^self primitiveFailed
]

{ #category : '*Concurrency' }
Process >> runUnwindBlocksFrom: aContext [
	"Run every ensure: and ifCurtailed: block from aContext outwards to
	 the bottom of its stack, innermost first, on the process sending
	 this.  aContext itself first, since a process stopped between two
	 bytecodes can stop with the ensure: frame on top."
	| ctx |
	ctx := aContext.
	ctx isNil ifTrue: [^self].
	ctx isUnwindContext ifTrue: [ctx runUnwindBlock].
	[ctx := ctx findNextUnwindUpTo: nil.
	 ctx isNil] whileFalse: [ctx runUnwindBlock]
]

{ #category : '*Concurrency' }
Process >> terminate [
	"Stop me for good, running the ensure: blocks I am inside first.

	 For the active process: unwind from here to the bottom, then end.
	 For any other, wherever it is -- a ready list, a Semaphore, another
	 worker's core -- stop it there, take its stack, and unwind that.
	 The stack is taken BEFORE its blocks are run, so a block that fails
	 leaves a process that is terminated rather than one that is not."
	| context |
	Processor activeProcess == self ifTrue: [
		self runUnwindBlocksFrom: thisContext.
		^self primTerminateActive].
	self primDetach: true.
	context := suspendedContext.
	context isNil ifTrue: [^self].
	suspendedContext := nil.
	self runUnwindBlocksFrom: context
]

{ #category : '*Concurrency' }
Process >> suspend [
	"Stop me so that resume can start me again.

	 The primitive does it for the active process.  For any other the VM
	 brings me to a stop first, wherever I am; 1983's fallback looked only
	 on the ready lists, so a process running on another worker was 'not
	 active'.  A process waiting on a Semaphore is still refused, as it
	 was: taken out of its wait and resumed, it would go on past the wait
	 as though the signal had come."
	<primitive: 88>
	Processor activeProcess == self ifTrue: [^self primitiveFailed].
	(self primDetach: false) = 4 ifTrue: [
		^self error: 'This process is waiting on a Semaphore and cannot be suspended'].
	^self
]

{ #category : '*Concurrency' }
Process >> resume [
	"Let me run.  The primitive refuses anything but a process that is
	 parked and free, and the reason it refused is the error."
	<primitive: 87>
	suspendedContext isNil ifTrue: [
		^self error: 'cannot resume a process that has terminated'].
	myList notNil ifTrue: [
		^self error: 'cannot resume a process that is already waiting'].
	^self error: 'cannot resume a process that is running'
]

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
	 through the machinery above: it is already here, so it can just signal.

	 Any other receiver is first brought to a stop wherever it is, which is
	 what makes a timeout a timeout: a process waiting on a Semaphore will
	 not execute anything until that semaphore is signalled, so the
	 exception would sit on its stack until the very wait we are cutting
	 short finished on its own -- `[(Delay forMilliseconds: 500) wait]
	 valueWithinMilliseconds: 100' has to come back in 100, not 500 -- and
	 a process running on another worker has its real stack in that
	 worker's registers, where a frame spliced under its stale
	 suspendedContext would never be seen.  Once stopped it is resumed
	 with the frame on top, unless it was suspended on purpose, in which
	 case the frame waits for whoever resumes it."
	| frame where |
	self == Processor activeProcess ifTrue: [^anException signal].
	where := self primDetach: true.
	suspendedContext isNil ifTrue: [^self].
	frame := [anException signal] newProcess suspendedContext.
	"Field one of any context is its sender; splicing it in is what makes
	 the signal appear to happen inside this process rather than beside it."
	frame instVarAt: 1 put: suspendedContext.
	suspendedContext := frame.
	where = 0 ifFalse: [self resume]
]

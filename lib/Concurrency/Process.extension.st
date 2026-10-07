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
	     3  waiting on a Semaphore, and taken off it -- answered as that
	        Semaphore, not as 3, so terminate can put a process that was
	        inside an unwind block back into the same wait
	     4  waiting on a Semaphore and LEFT there, takeFromSemaphore
	        being false
	     5  nothing was done: the SENDER is itself being stopped by
	        another worker, and gave way to that rather than wait for me
	        while that worker waits for it (Bugs6 SCHED-1).  The sender
	        yields -- which lets the stop land -- and asks again if it
	        ever runs again

	 In every case but 4 and 5 I am afterwards parked, on no list, in
	 nobody's hands, with suspendedContext where I stopped.  Fails for
	 the active process, which cannot be stopped from inside itself this
	 way."
	<primitive: 231>
	^self primitiveFailed
]

{ #category : '*Concurrency' }
Process >> detachTakingFromSemaphore: takeFromSemaphore [
	"primDetach:, asked again for as long as it answers 5.

	 Two processes stopping each other at the same moment -- a worker and
	 its watchdog, each terminating the other as both fire -- used to
	 spin both their workers for ever inside the primitive, each waiting
	 for the other to park: a worker inside the primitive never reaches
	 the bytecode it would park at.  On two workers nothing else in the
	 image ran again.  Now one of the two gives way (the primitive says
	 which) and answers 5, and the yield here is where its own stop
	 lands: it is parked at its next bytecode, and the other process goes
	 on.  Only a process that is later resumed -- suspended rather than
	 terminated, or handed an exception it survives -- comes back round
	 the loop, and it still owes the stop it was asked for."
	| where |
	[(where := self primDetach: takeFromSemaphore) = 5]
		whileTrue: [Processor yield].
	^where
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
Process >> priority: anInteger [
	"Set my priority, refusing anything the scheduler cannot run me at.

	 1983 checks the upper bound only -- `anInteger <= Processor
	 highestPriority' -- so 0, -3 and 4.5 were all accepted, and each one
	 makes a process that is not scheduled and not reported (Bugs4 PROC-5).
	 The VM is where it stops being silent and starts being invisible:
	 SCHED_sleep answers without doing anything when a process's priority is
	 not a SmallInteger in range, because the priority is what indexes
	 quiescentProcessLists, so `p priority: 0. p resume' leaves p on no list
	 at all, running nowhere, for ever, with no error anywhere.

	 One-relative because the array is: priority 1 is
	 SystemRockBottomPriority and there is no zeroth list.  Pharo checks the
	 same pair of bounds."
	anInteger isInteger ifFalse: [
		^self error: 'a process priority must be an Integer'].
	anInteger < 1 ifTrue: [
		^self error: 'priority too low'].
	anInteger > Processor highestPriority ifTrue: [
		^self error: 'priority too high'].
	priority := anInteger
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
	 leaves a process that is terminated rather than one that is not.

	 Two exceptions, both Bugs5 OM-5.  A process stopped in Mutex>>waitFor:
	 just after its wait holds the lock before owner says so, and is
	 recorded first, so the ensure: in critical: gives it back.  And a
	 process stopped INSIDE an unwind block is not unwound from outside at
	 all: runUnwindBlock disarms a frame before running its block, so the
	 unwind below would skip it and abandon the rest of the block -- the
	 rest of a critical: that was releasing a lock, say.  That process is
	 let finish the block itself and terminates when it returns; see
	 finishUnwindBlock:waitingOn:.  A process that terminates ITSELF from
	 an unwind block asked to stop there, and does."
	| where context unwinding |
	Processor activeProcess == self ifTrue: [
		self runUnwindBlocksFrom: thisContext.
		^self primTerminateActive].
	where := self detachTakingFromSemaphore: true.
	context := suspendedContext.
	context isNil ifTrue: [^self].
	self noteMutexOwnerIn: context waiting: where isInteger not.
	unwinding := self unwindBlockInProgressFrom: context.
	unwinding notNil ifTrue: [^self finishUnwindBlock: unwinding waitingOn: where].
	suspendedContext := nil.
	self runUnwindBlocksFrom: context
]

{ #category : '*Concurrency' }
Process >> noteMutexOwnerIn: aContext waiting: wasWaiting [
	"If I was stopped in Mutex>>waitFor:, let the Mutex record me as its
	 owner if the wait had already handed me the lock.  If in
	 Semaphore>>waitAndMark:, the same for Semaphore>>critical:'s cell."
	aContext method == (Mutex compiledMethodAt: #waitFor:) ifTrue: [
		^aContext receiver noteOwner: self stoppedIn: aContext waiting: wasWaiting].
	(wasWaiting not
	 and: [aContext method == (Semaphore compiledMethodAt: #waitAndMark:)
	 and: [aContext isPastFirstSend]])
		ifTrue: [(aContext tempAt: 1) at: 1 put: true]
]

{ #category : '*Concurrency' }
Process >> unwindBlockInProgressFrom: aContext [
	"The OUTERMOST frame on my stack, from aContext down, that is running
	 an ensure: or ifCurtailed: block -- or nil.

	 Outermost, because everything above it is part of finishing that
	 block: an unwind block that itself runs an ensure: is still running
	 its own when the inner one is done."
	| method ctx found |
	method := ContextPart compiledMethodAt: #runUnwindBlock.
	ctx := aContext.
	[ctx isNil] whileFalse: [
		ctx method == method ifTrue: [found := ctx].
		ctx := ctx sender].
	^found
]

{ #category : '*Concurrency' }
Process >> spliceTerminateUnder: aContext [
	"Make aContext return into a frame that terminates the process
	 running it, and that frame return to where aContext would have."
	| frame |
	"asContext, not newProcess: newProcess wraps the block in one that
	 ends its process, and this frame has to return to the next."
	frame := [Processor activeProcess terminate] asContext.
	"Field one of any context is its sender."
	frame instVarAt: 1 put: aContext sender.
	aContext instVarAt: 1 put: frame
]

{ #category : '*Concurrency' }
Process >> finishUnwindBlock: aContext waitingOn: where [
	"I was stopped inside an unwind block, with aContext running it.  Let
	 me run on until that block is done and then terminate myself, which
	 runs the unwind blocks outside it.

	 If the stop took me off a Semaphore (where is that Semaphore), I am
	 put back into the same wait rather than let past it: an unwind block
	 waiting for a Mutex must still wait for it.  The frame doing the
	 waiting answers what the interrupted wait would have, so the
	 Semaphore the primitive left as its result is popped first."
	| frame |
	self spliceTerminateUnder: aContext.
	where isInteger ifFalse: [
		frame := [where wait] asContext.
		suspendedContext pop.
		frame instVarAt: 1 put: suspendedContext.
		suspendedContext := frame].
	self resume
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
	(self detachTakingFromSemaphore: false) = 4 ifTrue: [
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
	where := self detachTakingFromSemaphore: true.
	suspendedContext isNil ifTrue: [^self].
	"Stopped in Mutex>>waitFor: with the lock already handed over: record
	 the owner, or the unwind the signal may start leaves it held (Bugs5
	 OM-5)."
	self noteMutexOwnerIn: suspendedContext waiting: where isInteger not.
	frame := self signalFrameFor: anException over: suspendedContext.
	"Field one of any context is its sender; splicing it in is what makes
	 the signal appear to happen inside this process rather than beside it."
	frame instVarAt: 1 put: suspendedContext.
	suspendedContext := frame.
	where = 0 ifFalse: [self resume]
]

{ #category : '*Concurrency' }
Process >> signalFrameFor: anException over: aContext [
	"A context that signals anException and, if a handler resumes it,
	 returns into aContext leaving aContext exactly as it was stopped.

	 It was `[anException signal] newProcess suspendedContext', and
	 newProcess wraps its block in one that ends the process when the
	 block finishes -- so a handler that resumed the exception, a
	 Notification or a Warning, ended the process instead of letting it
	 go on.  asContext is the block's own context, which returns to its
	 sender.

	 And what it returns is aContext's own top of stack, popped just
	 before.  aContext was stopped between two bytecodes, not at a send
	 that expects an answer, so whatever a return pushes onto it is one
	 value too many: stopped between pushing the receiver and the
	 argument of `a + b', it would go on to add b to the resumption value
	 instead of to a.  Popping the top and handing it back puts the same
	 value in the same slot.  A process taken out of a Semaphore wait is
	 the same case: its top is the Semaphore the wait primitive left as
	 its answer, and the wait answers that.  Only a context with nothing
	 at all in its frame -- no temporaries and an empty stack -- has no
	 top to hand back; it gets one nil more, which is what Squeak's
	 signalException: does to every context it interrupts."
	(aContext instVarAt: 3) > 0 ifFalse: [
		^[anException signal. nil] asContext].
	^[anException signal. aContext pop] asContext
]

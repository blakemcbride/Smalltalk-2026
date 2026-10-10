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

Two kinds of place a process is not stopped IN, but let out of first
(Bugs5 OM-5, Bugs6 SCHED-6 and SCHED-7).  An unwind block it is already
running: runUnwindBlock disarms the frame before the block, so an unwind
from outside would skip the rest of it -- the rest of a lock's release,
say.  And a critical: section it is RUNNING in -- Semaphore's, Mutex's or
Monitor's: the lock's ensure: would give the lock back around a half-done
update, and Delay's queue was left with a nil at its head, which stopped
the timing process and with it every Delay in the image.  In both cases
the process is resumed to finish what it was in, and what terminate or
signalException: wanted happens when it has: terminate by a frame spliced
under the unwind block, or by the section's own unwind block made to end
the process after it has run; signalException: by a frame that raises
where the unwind block returns to, or by the section's unwind block made
to raise once it has run.  A process WAITING inside a section is not let
out first: a wait is a stable point, and a consumer blocked in
SharedQueue>>next must not be made to take an item on its way out.
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
	     6  nothing was done: another stopper holds me and has not yet
	        let go (Bugs6 SCHED-7).  The sender yields and asks again

	 In every case but 4, 5 and 6 I am afterwards parked, on no list, in
	 nobody's hands, with suspendedContext where I stopped -- and HELD by
	 the sender until it sends primRelease:, so that nobody else can
	 resume me or stop me again meanwhile.  Fails for the active process,
	 which cannot be stopped from inside itself this way."
	<primitive: 231>
	^self primitiveFailed
]

{ #category : '*Concurrency' }
Process >> primRelease: resumeIt [
	"Let go of me, having stopped me with primDetach:; and first put me
	 back to work if resumeIt is true -- resumed and THEN released, so
	 that there is no instant at which a stray resume could take me.
	 With false, harmless when I am not held: terminate and
	 signalException: release in an ensure: as well as on each path."
	<primitive: 235>
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
	[(where := self primDetach: takeFromSemaphore) = 5 or: [where = 6]]
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
	self primPriority: anInteger
]

{ #category : '*Concurrency' }
Process >> primPriority: anInteger [
	"Store my priority -- and, if I am waiting for the processor, move me
	 to the ready list of the new priority, in one step under the ready
	 lock (Bugs6 SCHED-3).  1983 stored the field and left the process on
	 the list of its old priority, so a process given a new priority
	 while ready was on a list its field did not name: primDetach: took it
	 for one waiting on a Semaphore, suspend refused it, and terminate
	 edited a ready list under the wrong lock.  Pharo re-queues; so does
	 this.  Where the primitive is absent the field alone is stored, which
	 is the single-threaded 1983 behaviour and correct there: the list is
	 then told by the field."
	<primitive: 237>
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
	| where context unwinding section |
	Processor activeProcess == self ifTrue: [
		self runUnwindBlocksFrom: thisContext.
		^self primTerminateActive].
	where := self detachTakingFromSemaphore: true.
	"Held from here until released, on every path: by the resume that
	 lets me finish what I was in, or explicitly once my stack is taken,
	 and in the ensure: should anything in between fail."
	[context := suspendedContext.
	 context isNil ifTrue: [^self].
	 self noteMutexOwnerIn: context waiting: where isInteger not.
	 unwinding := self unwindBlockInProgressFrom: context.
	 unwinding notNil ifTrue: [^self finishUnwindBlock: unwinding waitingOn: where].
	 section := self criticalSectionFrom: context stoppedAt: where.
	 section notNil ifTrue: [^self finishSection: section].
	 "The stack is taken by a compare-and-swap, so that two terminators
	  cannot both take it and both run its unwind blocks (Bugs6 KERNB-13):
	  the second finds nil and gives up.  Slot 2 is suspendedContext,
	  after Link's nextLink."
	 (self compareAndSwapSlot: 2 from: context to: nil) ifFalse: [^self]]
		ensure: [self primRelease: false].
	self runUnwindBlocksFrom: context
]

{ #category : '*Concurrency' }
Process >> criticalSectionFrom: aContext stoppedAt: where [
	"The ensure: frame of the OUTERMOST critical: section I was stopped
	 RUNNING in -- Semaphore's, Mutex's or Monitor's critical:, known by
	 method identity, the way runUnwindBlock is -- from aContext down; or
	 nil.

	 Running is where 1 (parked by the worker executing me) or 2 (taken
	 off a ready list): anywhere in the section, including the middle of
	 a store.  A process stopped WAITING on a Semaphore inside a section
	 -- where is that Semaphore -- is at a stable point and is unwound
	 from there as it always was; the one suspended on purpose (0) is
	 left as whoever suspended it left it.  Outermost, because a section
	 inside a section is part of finishing the outer one, and because
	 Monitor>>critical: is Mutex>>critical: one frame in.

	 Bugs6 SCHED-7: a process terminated between the two halves of
	 OrderedCollection>>insert:before: inside Delay>>wait left a nil at
	 the head of SuspendedDelays, the timing process raised on it, and no
	 Delay in the image fired again -- three runs in three of a randomized
	 stress; and SharedQueue's items were torn the same way.

	 Only a frame whose unwind block is still in it counts.  ensure: is
	 `result := self value. thisContext runUnwindBlock. ^result', and a
	 process can be stopped on the two bytecodes after runUnwindBlock has
	 returned -- the lock given back, the frame disarmed, nothing left of
	 the section but its return.  finishSection: put its block into that
	 frame's first temporary, and nothing ever read it again: the process
	 returned from ensure: and went on, untouched, and the forty-process
	 Mutex stress left one running in two runs of thirty (found by the
	 sanitizer runs of 2026-10-09).  A disarmed frame is past its section,
	 and the process is unwound from outside as from anywhere else."
	| sem mut mon ctx found |
	(where = 1 or: [where = 2]) ifFalse: [^nil].
	sem := Semaphore compiledMethodAt: #critical:.
	mut := Mutex compiledMethodAt: #critical:.
	mon := Monitor compiledMethodAt: #critical:.
	ctx := aContext.
	[ctx isNil] whileFalse: [ | caller |
		(ctx isUnwindContext
		 and: [(ctx tempAt: 1) notNil
		 and: [(caller := ctx sender) notNil
		 and: [caller method == sem or: [caller method == mut or: [caller method == mon]]]]])
			ifTrue: [found := ctx].
		ctx := ctx sender].
	^found
]

{ #category : '*Concurrency' }
Process >> finishSection: anUnwindContext [
	"I was stopped running inside the critical: section anUnwindContext
	 guards.  Let me finish it, and end when it ends: the section's
	 unwind block is replaced by one that runs it and then terminates me.

	 In the unwind block, and not in a frame spliced under the section
	 (which is what finishUnwindBlock:waitingOn: does for a block already
	 running), because a section can end by a ^ or an exception out
	 through it, and those discard a spliced frame without running it; an
	 unwind block runs however the section ends, once, since
	 runUnwindBlock disarms it first.  `self terminate' from the block is
	 the active-process path when I run it myself, which runs the unwind
	 blocks outside the section and ends me; run by a second terminator
	 that has already taken my stack, it finds no suspendedContext and
	 does nothing."
	| guarded |
	guarded := anUnwindContext tempAt: 1.
	anUnwindContext tempAt: 1 put: [guarded value. self terminate].
	self primRelease: true
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
	self primRelease: true
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
	"Stopped and held: let go, and it stays parked for a resume."
	self primRelease: false.
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
	| frame where unwinding section |
	self == Processor activeProcess ifTrue: [^anException signal].
	where := self detachTakingFromSemaphore: true.
	"Held from here until released on every path (see terminate)."
	[suspendedContext isNil ifTrue: [^self].
	 "Stopped in Mutex>>waitFor: with the lock already handed over: record
	  the owner, or the unwind the signal may start leaves it held (Bugs5
	  OM-5)."
	 self noteMutexOwnerIn: suspendedContext waiting: where isInteger not.
	 "Inside an unwind block, or running inside a critical: section: let
	  that finish first, and raise where it ends (Bugs6 SCHED-6, SCHED-7).
	  Delivered into the middle of Mutex>>release, which is two sends, the
	  exception's own unwind skipped the disarmed ensure: and the signal
	  was never sent: a Mutex with an owner long gone, every later
	  critical: on it blocked for ever."
	 unwinding := self unwindBlockInProgressFrom: suspendedContext.
	 unwinding notNil ifTrue: [^self signal: anException after: unwinding waitingOn: where].
	 section := self criticalSectionFrom: suspendedContext stoppedAt: where.
	 section notNil ifTrue: [^self signal: anException leaving: section].
	 frame := self signalFrameFor: anException over: suspendedContext waitingOn: where.
	 "Field one of any context is its sender; splicing it in is what makes
	  the signal appear to happen inside this process rather than beside
	  it.  Resumed unless it was suspended on purpose, in which case the
	  frame waits for whoever resumes it."
	 frame instVarAt: 1 put: suspendedContext.
	 suspendedContext := frame.
	 self primRelease: where ~= 0]
		ensure: [self primRelease: false]
]

{ #category : '*Concurrency' }
Process >> signal: anException after: aContext waitingOn: where [
	"I was stopped inside an unwind block, with aContext running it.  Let
	 the block finish, and raise anException in the frame it returns to;
	 and if the stop took me off a Semaphore (where is that Semaphore),
	 put me back into the same wait first.  The shape of
	 finishUnwindBlock:waitingOn:, with a signal where that has a
	 terminate.

	 The frame answers nil to aContext's sender, which is right because
	 every sender of runUnwindBlock -- ensure:'s normal path, unwindTo:,
	 aboutToReturn:through:, runUnwindBlocksFrom: -- discards its answer.
	 A handler that resumes the exception lets the frame return there; one
	 that returns, retries or passes unwinds from the frame outwards, and
	 the ensure: this block belongs to has already been disarmed."
	| frame |
	frame := [anException signal. nil] asContext.
	"Field one of any context is its sender."
	frame instVarAt: 1 put: aContext sender.
	aContext instVarAt: 1 put: frame.
	where isInteger ifFalse: [
		frame := [where wait] asContext.
		suspendedContext pop.
		frame instVarAt: 1 put: suspendedContext.
		suspendedContext := frame].
	self primRelease: where ~= 0
]

{ #category : '*Concurrency' }
Process >> signal: anException leaving: anUnwindContext [
	"I was stopped running inside the critical: section anUnwindContext
	 guards.  Let me finish it, and raise anException as it ends: the
	 section's unwind block is replaced by one that runs it -- releasing
	 the lock -- and then signals.  See finishSection: for why the unwind
	 block and not a spliced frame.  The handler search walks out from
	 the block through the section's frames to whatever on:do: is below,
	 as it would from anywhere in the section."
	| guarded |
	guarded := anUnwindContext tempAt: 1.
	anUnwindContext tempAt: 1 put: [guarded value. anException signal].
	self primRelease: true
]

{ #category : '*Concurrency' }
Process >> signalFrameFor: anException over: aContext waitingOn: where [
	"A context that signals anException and, if a handler resumes it,
	 returns into aContext leaving aContext exactly as it was stopped --
	 or, when the stop took me off a Semaphore (where is that Semaphore),
	 puts me back into the same wait first.

	 Back into the wait, because a resumption that simply returned let
	 me PAST it (Bugs6 KERNB-3).  A Warning -- which halt, notify: and
	 confirm: signal, and which its own defaultAction resumes when
	 nothing handles it -- poked into a process waiting for a Mutex
	 returned into Mutex>>waitFor: just past its wait: owner := me ran,
	 the process walked into the critical section beside the holder, and
	 its release on the way out left the semaphore counting two for good.
	 The interrupted wait's answer is the Semaphore, and that is what the
	 fresh wait answers, so aContext sees exactly the return it was
	 expecting, only later.  The same shape finishUnwindBlock:waitingOn:
	 and signal:after:waitingOn: already had."
	where isInteger ifFalse: [
		^[anException signal. aContext pop. where wait] asContext].
	^self signalFrameFor: anException over: aContext
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

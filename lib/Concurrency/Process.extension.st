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
in nobody's hands, and held by the process stopping it until that lets it
go.  primTerminateActive ends the active process itself.  The stopping
process is not itself stopped in the middle of that: see
stopTakingFromSemaphore:thenDo:.

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
under the unwind block, or by a DeferredStop put in the place of the
section's own unwind block, which runs it and then ends the process;
signalException: by a frame that raises where the unwind block returns
to, or by the same DeferredStop, which raises where the section's unwind
block returns to.  The DeferredStop acts only in the process it was made
for, never in a terminator unwinding the section, and keeps the requests
rather than nesting them: a terminate beats a signal (Bugs07 SCHED-5).
A section is let finish for a grace -- a tenth of a second -- and no
longer: one still running then is given up, and the process is unwound
from outside after all (Bugs07 FIXES-3); without the bound, a loop inside
`aMutex critical:' could not be stopped.  A process WAITING inside a
section is not let out first: a wait is a stable point, and a consumer
blocked in SharedQueue>>next must not be made to take an item on its way
out.
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
	 Only the process holding me lets go (Bugs07 SCHED-2): with false,
	 from anyone else or when I am not held, nothing happens; with true
	 from anyone else, the primitive fails and I stay as I am.  It used
	 to let anyone go of me, and a stopper's late second release took
	 away the hold the next stopper had just taken.  Sent once per stop,
	 by detach:thenDo: -- or by undoStop: in its place."
	<primitive: 235>
	^self primitiveFailed
]

{ #category : '*Concurrency' }
Process >> stopTakingFromSemaphore: takeFromSemaphore thenDo: aBlock [
	"Bring me to a stop, then evaluate aBlock with where I was and let go
	 of me -- resumed first if aBlock answers true (see detach:thenDo:).
	 Asked again for as long as the primitive answers 5 or 6.  Answers
	 where I was.  What terminate, suspend and signalException: are
	 written over.

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
	 the loop, and it still owes the stop it was asked for.  The yield
	 and the asking again are outside detach:thenDo:, so that a stopper
	 waiting for another stopper to let me go can itself be stopped."
	| where |
	[where := self detach: takeFromSemaphore thenDo: aBlock.
	 where = 5 or: [where = 6]] whileTrue: [Processor yield].
	^where
]

{ #category : '*Concurrency' }
Process >> detach: takeFromSemaphore thenDo: aBlock [
	"primDetach: me; and unless that did nothing, evaluate aBlock with
	 where I was and let go of me, resumed first if aBlock answers true.
	 Answers where I was.

	 From the moment the primitive answers until I am let go, I am HELD
	 by the process running this, and nothing else can resume me or stop
	 me (Bugs6 SCHED-7) -- so the process running this must not itself be
	 stopped in between (Bugs07 SCHED-1).  It was: the release was in an
	 ensure: entered several bytecodes after the primitive had answered,
	 and suspend had none at all, so a stopper terminated there -- a
	 supervisor stopping its children and stopped in turn by its parent,
	 a watchdog's timeout landing on a process busy terminating something
	 -- was unwound with nothing to let me go.  I stayed held for good:
	 resume refused me, every later terminate of me yielded and asked
	 again for ever, and a Mutex I was inside was lost; and stopped a
	 little later, between the stack walks and the resume, its ensure:
	 let me go without resuming me, which left me on no list and in
	 nobody's hands, suspended by nobody.  Sixty-four such leftovers
	 filled the table and stopped every stop in the image.

	 So a frame of this method that is past its first send -- the
	 primitive -- counts as an unwind block in progress
	 (unwindBlockInProgressFrom:): a terminate or signalException: of the
	 process running it lets it finish, and takes effect where it
	 returns; terminate waits for it.  Before the primitive the process
	 holds nothing and is stopped like any other: let finish from there,
	 a stopper stopped by its own victim went on to stop that victim
	 while the victim waited for it to end, and both died.  If aBlock
	 fails, undoStop: puts me back as the stop found me.  A suspend is not
	 put off: a stopper suspended in here keeps me held until it is
	 resumed, as it would keep a lock.  aBlock must not leave by ^, which
	 would skip the frame a deferred stop puts under this one."
	| where resumeIt |
	where := self primDetach: takeFromSemaphore.
	(where = 4 or: [where = 5 or: [where = 6]]) ifTrue: [^where].
	resumeIt := [aBlock value: where] ifCurtailed: [self undoStop: where].
	self primRelease: resumeIt == true.
	^where
]

{ #category : '*Concurrency' }
Process >> undoStop: where [
	"Let go of me, having stopped me at where, as though the stop had
	 never happened: running or ready again if I was, back in the wait I
	 was taken out of, left alone if I was suspended -- for a stopper
	 whose work on me failed part-way (Bugs07 SCHED-1).  A process taken
	 out of a wait and simply resumed would go on past it as though the
	 Semaphore had been signalled, which inside Mutex>>critical: is a
	 second process in the section; the wait is put back the way
	 finishUnwindBlock:waitingOn: puts it back."
	self primRelease: (self restoreStoppedAt: where)
]

{ #category : '*Concurrency' }
Process >> restoreStoppedAt: where [
	"Put me back as a stop at where found me, and answer whether I am to
	 be resumed: running or ready again if I was, back in the wait I was
	 taken out of, left alone if I was suspended or have ended.  What
	 undoStop: lets go of me with, and what a stopper answers when it
	 stopped me only to look (see withdraw:from:)."
	| frame |
	suspendedContext isNil ifTrue: [^false].
	(where = 1 or: [where = 2]) ifTrue: [^true].
	where isInteger ifTrue: [^false].
	frame := [where wait] asContext.
	suspendedContext pop.
	frame instVarAt: 1 put: suspendedContext.
	suspendedContext := frame.
	^true
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
	 bytecodes can stop with the ensure: frame on top.  Each block on its
	 own: an error one of them raises is reported and the next one runs
	 (see runUnwindBlockOf:)."
	| ctx |
	ctx := aContext.
	ctx isNil ifTrue: [^self].
	ctx isUnwindContext ifTrue: [self runUnwindBlockOf: ctx].
	[ctx := ctx findNextUnwindUpTo: nil.
	 ctx isNil] whileFalse: [self runUnwindBlockOf: ctx]
]

{ #category : '*Concurrency' }
Process >> runUnwindBlockOf: anUnwindContext [
	"Run anUnwindContext's unwind block for terminate, and let no Error
	 it raises out: report it, as an error nobody handled, and carry on.

	 Bugs07 KERNB-3.  The blocks of a process terminated from outside run
	 on the TERMINATOR, and an error one of them raised searched the
	 terminator's stack.  A handler there -- `[p terminate] on: Error do:',
	 the ordinary way to stop something that may fail -- caught it, and
	 its return: unwound out of the loop in runUnwindBlocksFrom:, so
	 every block further out on the victim's stack was skipped, and since
	 that stack had already been taken nobody ever ran them: a victim
	 terminated inside `aMutex critical:' whose own ensure: failed left
	 the Mutex locked for ever.  With no handler, Error>>defaultAction
	 unwound the terminator's own stack and, the terminator being a
	 forked process, ended it in the middle of the loop: the terminator
	 was killed by its victim's cleanup, and the Mutex lost the same way.
	 The same held for a process terminating itself: a handler of its own
	 below the failing block caught the error and the process went on,
	 not terminated at all.

	 What fails in a cleanup block cannot be handed back to anyone -- the
	 process it belonged to is being ended -- so it is said, the way an
	 unhandled error is said, and counted as one (Error>>noteUnhandled,
	 which -eval's exit status reads), and the rest of the cleanup runs.
	 Only Error: a Warning or a Notification raised there is resumed by
	 its own default action, as anywhere."
	[anUnwindContext runUnwindBlock]
		on: Error
		do: [:e |
			e noteUnhandled.
			[e reportAsText]
				on: Error
				do: [:inner | e reportOnStandardError:
					'an error was raised that could not describe itself'].
			e return: nil]
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
	 an unwind block asked to stop there, and does.

	 A process stopped while it is itself stopping another -- inside
	 detach:thenDo:, holding its own victim -- is let finish that too,
	 and this waits until it has ended (Bugs07 SCHED-1): the stop it was
	 making is a few bytecodes long, and whoever terminates it is entitled
	 to find its victim let go when terminate returns, as it was when
	 the stopper's own unwind blocks did the letting go.

	 A process stopped running inside a critical: section is let finish
	 the section, and this waits until it has ended -- but only for the
	 grace a DeferredStop allows (Bugs07 FIXES-3).  The section that has
	 not ended by then is given up: I am stopped again, the section no
	 longer counts, and I am unwound from outside, its ensure: giving the
	 lock back.  A loop inside `aMutex critical:' used to make me
	 unkillable: this returned at once, as though it had worked, and I
	 ran on for ever with the lock.  A section nested inside the one
	 given up -- a library's, say, entered from the loop -- is a section
	 of its own and is let finish in its turn.

	 And the stack I leave behind is unwound even if the process doing it
	 is stopped part-way: the ifCurtailed: goes on from where the loop
	 was, since every block the loop has already run is disarmed.  A
	 supervisor terminated while it was running a child's unwind blocks
	 used to abandon the rest of them, and the child's Mutex with them."
	| outcome stop again |
	Processor activeProcess == self ifTrue: [
		self runUnwindBlocksFrom: thisContext.
		^self primTerminateActive].
	again := false.
	[self stopTakingFromSemaphore: true thenDo: [:where |
		outcome := self endStoppedAt: where.
		(outcome isKindOf: DeferredStop) ifTrue: [stop := outcome].
		outcome == true or: [outcome == #stopping or: [stop notNil]]].
	 outcome == #stopping ifTrue: [self waitUntilTerminated].
	 stop notNil ifTrue: [
		(self waitAtMostGraceFor: [suspendedContext isNil]) ifFalse: [
			stop giveUp.
			again := true]].
	 (outcome isKindOf: ContextPart) ifTrue: [self runUnwindBlocksFrom: outcome]]
		ifCurtailed: [
			(outcome isKindOf: ContextPart) ifTrue: [self runUnwindBlocksFrom: outcome]].
	again ifTrue: [self terminate]
]

{ #category : '*Concurrency' }
Process >> endStoppedAt: where [
	"I have been stopped at where and am held, for terminate.  Answer
	 true if I am to be resumed to finish the unwind block I was in, and
	 end when I have; #stopping for the same when what I was in is a stop
	 of my own, which terminate waits out; the DeferredStop of the
	 critical: section I was running in, to be resumed to finish it, for
	 as long as its grace lasts; my stack, taken, for the terminator to
	 unwind; or nil, when there is nothing left to do."
	| context unwinding section |
	context := suspendedContext.
	context isNil ifTrue: [^nil].
	self noteMutexOwnerIn: context waiting: where isInteger not.
	unwinding := self unwindBlockInProgressFrom: context.
	unwinding notNil ifTrue: [
		self finishUnwindBlock: unwinding waitingOn: where.
		^unwinding method == (Process compiledMethodAt: #detach:thenDo:)
			ifTrue: [#stopping]
			ifFalse: [true]].
	section := self criticalSectionFrom: context stoppedAt: where.
	section notNil ifTrue: [^self finishSection: section].
	"The stack is taken by a compare-and-swap, so that two terminators
	 cannot both take it and both run its unwind blocks (Bugs6 KERNB-13):
	 the second finds nil and gives up.  Slot 2 is suspendedContext,
	 after Link's nextLink."
	(self compareAndSwapSlot: 2 from: context to: nil) ifFalse: [^nil].
	^context
]

{ #category : '*Concurrency' }
Process >> waitAtMostGraceFor: aBlock [
	"Wait until aBlock answers true -- I have ended, or an exception put
	 off to the end of a section has been raised -- and answer true; or
	 answer false once a DeferredStop's grace has passed and it still has
	 not.  Polled by a Delay, as waitUntilTerminated is and for the same
	 reason."
	| deadline |
	deadline := Time millisecondClockValue + DeferredStop graceMilliseconds.
	[aBlock value] whileFalse: [
		Time millisecondClockValue >= deadline ifTrue: [^false].
		(Delay forMilliseconds: 1) wait].
	^true
]

{ #category : '*Concurrency' }
Process >> waitUntilTerminated [
	"Wait until I have ended, having been let finish a stop of my own
	 first (see terminate).  By a Delay and not a yield: a yield runs
	 only processes of the waiter's own priority, and I may be of a lower
	 one on the same worker.  The stop is a few bytecodes long and my
	 unwind blocks after it as long as they are; a terminate of me that
	 ran them itself waited as long."
	[suspendedContext isNil] whileFalse: [(Delay forMilliseconds: 1) wait]
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
	 inside a section is part of finishing the outer one.  A Monitor's
	 section is its Mutex's: Monitor>>critical: is Mutex>>critical: one
	 frame in, and its own ensure: inside that only keeps its books.

	 A section whose DeferredStop has given up -- its grace ran out with
	 the section unfinished (Bugs07 FIXES-3) -- does not count any more;
	 a section inside it still does.

	 Bugs6 SCHED-7: a process terminated between the two halves of
	 OrderedCollection>>insert:before: inside Delay>>wait left a nil at
	 the head of SuspendedDelays, the timing process raised on it, and no
	 Delay in the image fired again -- three runs in three of a randomized
	 stress; and SharedQueue's items were torn the same way.

	 And only a section whose lock I hold at the moment I was stopped
	 counts (Bugs07 SCHED-4), for a Mutex's -- and so a Monitor's -- which
	 knows its holder; a Semaphore's does not, and counts whenever its
	 frame does.  Monitor>>waitForChange gives the lock up in the middle of the
	 section and waits: SharedQueue>>next, Promise>>value and every
	 condition wait are a Monitor's critical: around one, and a consumer
	 woken and not yet running again -- the ordinary state of every
	 consumer of a busy queue, since nextPut: wakes them all -- is inside
	 the section with the lock given up.  Taken for one running in it,
	 it was let finish the section: it went round waitUntil:, slept
	 again, and was terminated only when the NEXT item came, which it
	 then took and threw away; a timeout signalled into it did not fire
	 until data came.  Without the lock it is at a stable point and is
	 unwound from there, as a waiter is.

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
	| sem mut ctx found |
	(where = 1 or: [where = 2]) ifFalse: [^nil].
	sem := Semaphore compiledMethodAt: #critical:.
	mut := Mutex compiledMethodAt: #critical:.
	ctx := aContext.
	[ctx isNil] whileFalse: [ | caller block |
		(ctx isUnwindContext
		 and: [(block := ctx tempAt: 1) notNil
		 and: [((block isKindOf: DeferredStop) and: [block hasGivenUp]) not
		 and: [(caller := ctx sender) notNil
		 and: [caller method == sem
			or: [caller method == mut and: [caller receiver owner == self]]]]]])
			ifTrue: [found := ctx].
		ctx := ctx sender].
	^found
]

{ #category : '*Concurrency' }
Process >> stopIfDeferred [
	"Sent by the active process -- me -- where it is about to wait inside
	 a critical: section, and again where it has come back from waiting:
	 if a terminate or signalException: of me was put off to the end of a
	 section I am in (a DeferredStop in its frame), it takes effect here
	 instead, at a stable point.

	 Bugs07 SCHED-4.  A process let finish a section must not go to sleep
	 in it: the stop then waits for whatever wakes it, and for a consumer
	 in SharedQueue>>next that is the next item, which it takes on its way
	 out and nobody ever receives; a timeout signalled into it fires only
	 when data comes.  Monitor's waits send this, with the waiter's own
	 Semaphore already listed and the lock already given up, inside the
	 block whose curtailing takes the Semaphore off the list again."
	| ctx stop |
	ctx := thisContext.
	[(ctx := ctx findNextUnwindUpTo: nil) isNil] whileFalse: [
		stop := ctx tempAt: 1.
		((stop isKindOf: DeferredStop) and: [stop process == self])
			ifTrue: [^stop takeEffect]]
]

{ #category : '*Concurrency' }
Process >> finishSection: anUnwindContext [
	"I was stopped running inside the critical: section anUnwindContext
	 guards.  Let me finish it, and end when it ends: the section's
	 unwind block is replaced by a DeferredStop that runs it and then
	 terminates me.

	 In the unwind block, and not in a frame spliced under the section
	 (which is what finishUnwindBlock:waitingOn: does for a block already
	 running), because a section can end by a ^ or an exception out
	 through it, and those discard a spliced frame without running it; an
	 unwind block runs however the section ends, once, since
	 runUnwindBlock disarms it first.  The DeferredStop ends me only when
	 I run it myself; run by a terminator that has already taken my
	 stack, it releases the lock and does nothing more.  A section
	 already holding one has the terminate added to it (Bugs07 SCHED-5).
	 Answers the DeferredStop: I am to be resumed, and terminate waits
	 for me through its grace (Bugs07 FIXES-3)."
	| stop |
	stop := self deferredStopIn: anUnwindContext.
	stop terminate.
	^stop
]

{ #category : '*Concurrency' }
Process >> deferredStopIn: anUnwindContext [
	"The DeferredStop in the critical: section frame anUnwindContext,
	 put there in place of its unwind block if it is not there already.
	 A second request for the same section is added to the first rather
	 than wrapped round it (Bugs07 SCHED-5)."
	| block |
	block := anUnwindContext tempAt: 1.
	(block isKindOf: DeferredStop) ifTrue: [^block].
	block := DeferredStop for: self guarding: block in: anUnwindContext.
	anUnwindContext tempAt: 1 put: block.
	^block
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
	 its own when the inner one is done.

	 A frame of detach:thenDo: past its first send counts as one: it is
	 where a stopper holds the process it stopped, and is let finish for
	 the same reason an unwind block is (Bugs07 SCHED-1)."
	| method detaching ctx found |
	method := ContextPart compiledMethodAt: #runUnwindBlock.
	detaching := Process compiledMethodAt: #detach:thenDo:.
	ctx := aContext.
	[ctx isNil] whileFalse: [
		(ctx method == method
		 or: [ctx method == detaching and: [ctx isPastFirstSend]])
			ifTrue: [found := ctx].
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
	 Semaphore the primitive left as its result is popped first.
	 Answers true: I am to be resumed."
	| frame |
	self spliceTerminateUnder: aContext.
	where isInteger ifFalse: [
		frame := [where wait] asContext.
		suspendedContext pop.
		frame instVarAt: 1 put: suspendedContext.
		suspendedContext := frame].
	^true
]

{ #category : '*Concurrency' }
Process >> suspend [
	"Stop me so that resume can start me again.

	 The primitive does it for the active process.  For any other the VM
	 brings me to a stop first, wherever I am; 1983's fallback looked only
	 on the ready lists, so a process running on another worker was 'not
	 active'.  A process waiting on a Semaphore is still refused, as it
	 was: taken out of its wait and resumed, it would go on past the wait
	 as though the signal had come.  Stopped and held, it is let go
	 without being resumed, and stays parked for a resume."
	<primitive: 88>
	Processor activeProcess == self ifTrue: [^self primitiveFailed].
	(self stopTakingFromSemaphore: false thenDo: [:where | false]) = 4 ifTrue: [
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
	 case the frame waits for whoever resumes it.

	 Stopped running inside a critical: section, it is let finish the
	 section and raises anException as it leaves -- for the grace a
	 DeferredStop allows, and this waits that long at most (Bugs07
	 FIXES-3).  If anException has not been raised by then, it is taken
	 back out of the section's DeferredStop, the section gives up, and it
	 is delivered again, the section no longer counting.  A timeout on
	 work done under a lock used to wait for the work to end, which for a
	 loop inside `aMutex critical:' was never."
	| stop |
	self == Processor activeProcess ifTrue: [^anException signal].
	[stop := nil.
	 self stopTakingFromSemaphore: true thenDo: [:where | | answer |
		answer := self deliver: anException stoppedAt: where.
		(answer isKindOf: DeferredStop)
			ifTrue: [stop := answer. true]
			ifFalse: [answer]].
	 stop notNil
	 and: [(self waitAtMostGraceFor: [
			suspendedContext isNil or: [(stop isPending: anException) not]]) not
	 and: [self withdraw: anException from: stop]]] whileTrue
]

{ #category : '*Concurrency' }
Process >> withdraw: anException from: aDeferredStop [
	"Stop me, and if anException is still waiting in aDeferredStop to be
	 raised at the end of its section, take it out and have the section
	 give up; then put me back as the stop found me.  Answers whether it
	 was taken out, and so has still to be delivered.  Done with me
	 stopped, so that I cannot be raising it at the same moment
	 (signalException:, Bugs07 FIXES-3)."
	| taken |
	taken := false.
	self stopTakingFromSemaphore: true thenDo: [:where |
		taken := aDeferredStop withdraw: anException.
		self restoreStoppedAt: where].
	^taken
]

{ #category : '*Concurrency' }
Process >> deliver: anException stoppedAt: where [
	"I have been stopped at where and am held, for signalException:.
	 Arrange for anException to be raised in me, and answer whether I am
	 to be resumed -- or, when it is put off to the end of a critical:
	 section, the section's DeferredStop, and I am to be resumed."
	| frame unwinding section |
	suspendedContext isNil ifTrue: [^false].
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
	^where ~= 0
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
	 the ensure: this block belongs to has already been disarmed.
	 Answers whether I am to be resumed."
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
	^where ~= 0
]

{ #category : '*Concurrency' }
Process >> signal: anException leaving: anUnwindContext [
	"I was stopped running inside the critical: section anUnwindContext
	 guards.  Let me finish it, and raise anException as it ends: the
	 section's unwind block is replaced by a DeferredStop that runs it --
	 releasing the lock -- and then has anException raised where the
	 section returns to.  See finishSection: for why the unwind block and
	 not a spliced frame, and DeferredStop for when the exception is
	 raised and when it is dropped: never in a terminator, never inside
	 another exception's unwind, never once a terminate has been asked
	 for (Bugs07 SCHED-5).  The handler search walks out from the end of
	 the section to whatever on:do: is below, as it would from anywhere
	 in the section.  Answers the DeferredStop: I am to be resumed, and
	 signalException: waits for the exception through its grace (Bugs07
	 FIXES-3)."
	| stop |
	stop := self deferredStopIn: anUnwindContext.
	stop signal: anException.
	^stop
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

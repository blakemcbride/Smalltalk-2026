"
A stop put off to the end of a critical: section.

A process caught RUNNING inside a Semaphore's, Mutex's or Monitor's
critical: is let finish the section before a terminate or a
signalException: takes effect (Bugs6 SCHED-6 and SCHED-7): the lock's
ensure: would otherwise give the lock back around a half-done update.  What
was asked for is recorded here, and this object takes the place of the
section's own unwind block -- an ensure: frame's first temporary -- so that
it runs however the section ends, once, since runUnwindBlock disarms the
frame first.  It runs the block it replaced, and then does what was asked:
ends the process, or raises the exceptions in it.

It replaced a block that did the same in one line -- `[guarded value. self
terminate]', `[guarded value. anException signal]' -- and the line was wrong
in three ways (Bugs07 SCHED-5).  An unwind block is not a place in the
process's control flow: it runs wherever, and on whichever process, the
section happens to be unwound.  Unwound by a TERMINATOR -- the process
waited inside the section after the signal was put off, and was terminated
from outside -- the exception was raised in the terminator, out of its
`terminate', and the rest of the victim's unwind blocks were abandoned: an
outer ensure: that released another lock, or forkParallel:'s, whose Promise
then stayed pending for ever.  Unwound by ANOTHER EXCEPTION's handler, the
first exception was raised inside that unwind, where the handler that would
have caught it was out of service, and the process died of it.  And two
requests nested: a terminate after a signalException: wrapped the wrapped
block once more, the handler for the exception unwound past the outer
wrapping, and the terminate was lost -- the process ran on.

So the requests are kept, not wrapped: a terminate beats any signal, and
signals queue in the order they came and are raised newest first -- the
order Process>>signalException: raises them in outside a section, where
each is a frame on top of the last, and the order Pharo raises them in
everywhere (Bugs07 X-6).  A handler that resumes one lets the next be
raised; one that returns out of its on:do: unwinds past the rest, which
are never raised, outside a section as inside.  They are acted on only
by the process they were made for, never by a terminator unwinding it --
that one is ending it anyway -- and never from inside the process's own
terminate.  An exception is not raised from inside another exception's
unwind; it is dropped there, as the unwind is leaving the place it was
aimed at.  And it is raised in the frame the section's unwind block
returns to, after the block has returned, not inside it: inside, the
process would count as running an unwind block for as long as the handler
ran, and a terminate sent meanwhile would be put off to a point the
handler's return: skips.

A process that has been let finish a section must not go to sleep in it:
the stop would then wait for whatever wakes it, which for a consumer in
SharedQueue>>next is the next item, taken and thrown away (Bugs07 SCHED-4).
Monitor's waits ask Process>>stopIfDeferred, which finds this in the
section's frame and acts on it there, at a stable point, through
takeEffect.

And the section is let finish for a grace of graceMilliseconds, no longer
(Bugs07 FIXES-3).  Every section in the library is a few sends long, and
the deferral exists for those; a user's is not bounded at all, and a loop
inside `aMutex critical:' made its process unkillable -- terminate
returned at once, the process ran on, and the Mutex stayed held for ever;
a timeout on work done under a lock never fired.  Which sections are the
library's cannot be told from the frames: the same Monitor>>critical:
runs SharedQueue's two stores and a user's loop, and LibraryLocks runs
whatever block it is handed.  How long a section has run can.  So terminate
and signalException: wait for the grace, and if the process is still in the
section then, this gives up (hasGivenUp): the stopper stops it again, the
section no longer counts, and the process is unwound from outside -- its
ensure: releasing the lock -- or the exception is withdrawn and delivered
where the process stands.  A section entered inside the one given up is a
section of its own, with a grace of its own, so a library section the loop
was in at that moment is still let finish.
"
Class {
	#name : 'DeferredStop',
	#superclass : 'Object',
	#instVars : [ 'process', 'guarded', 'section', 'terminating', 'exceptions', 'givenUp' ],
	#classVars : [ 'Grace' ],
	#category : 'Concurrency'
}

{ #category : 'accessing' }
DeferredStop class >> graceMilliseconds [
	"How long a process is let finish a critical: section before a stop
	 put off to its end gives up and takes effect from outside (Bugs07
	 FIXES-3).  A library section is microseconds long; a tenth of a
	 second leaves room for the process to wait its turn for a worker on
	 a loaded machine, and is short enough that a timeout or a shutdown
	 is not held up by it noticeably.  graceMilliseconds: sets another."
	^Grace ifNil: [100]
]

{ #category : 'accessing' }
DeferredStop class >> graceMilliseconds: anIntegerOrNil [
	"Let a section run this many milliseconds before a stop put off to
	 its end gives up, or with nil a tenth of a second again.

	 The grace is a time, and so how a stop turns out depends on how
	 busy the machine is: a process descheduled for longer than the
	 grace, or a collection that long in the middle of its section, has
	 its section given up although nothing in it ran long.  That is the
	 bound doing its job on a server, and a test asking what happens
	 when a section is let finish -- not when it is given up -- cannot
	 ask it on a loaded machine: Bugs07 X-8's check of a terminate
	 beating a deferred signal failed one run in many that way.  So a
	 test can set it, and a server whose sections are longer than the
	 library's can too.  Global: every stopper reads it."
	Grace := anIntegerOrNil
]

{ #category : 'instance creation' }
DeferredStop class >> for: aProcess guarding: aBlock in: anUnwindContext [
	"The stop to put into anUnwindContext, a critical: section's ensure:
	 frame on aProcess's stack, in place of its unwind block aBlock."
	^self basicNew setProcess: aProcess guarding: aBlock in: anUnwindContext
]

{ #category : 'private' }
DeferredStop >> setProcess: aProcess guarding: aBlock in: anUnwindContext [
	process := aProcess.
	guarded := aBlock.
	section := anUnwindContext.
	terminating := false.
	givenUp := false
]

{ #category : 'accessing' }
DeferredStop >> process [
	^process
]

{ #category : 'requesting' }
DeferredStop >> terminate [
	"End the process when the section ends.  Beats any signal."
	terminating := true
]

{ #category : 'requesting' }
DeferredStop >> giveUp [
	"The grace has run out with the section unfinished: from now on the
	 section does not count (Process>>criticalSectionFrom:stoppedAt:), and
	 the stopper takes effect from outside."
	givenUp := true
]

{ #category : 'testing' }
DeferredStop >> hasGivenUp [
	^givenUp
]

{ #category : 'testing' }
DeferredStop >> isPending: anException [
	"Whether anException is still waiting to be raised at the section's
	 end."
	^exceptions notNil and: [exceptions identityIncludes: anException]
]

{ #category : 'requesting' }
DeferredStop >> withdraw: anException [
	"Take anException back out, if it is still waiting, and give up;
	 answer whether it was there.  Sent with the process stopped
	 (Process>>withdraw:from:).  Removed from the collection in place:
	 a process stopped between taking the collection to raise from and
	 clearing it holds the same one, and must not raise it as well.

	 Not once a terminate has been asked for (Bugs07 X-8).  A terminate
	 beats any signal, and an exception here will never be raised; taken
	 out and delivered where the process stands it WAS raised -- its
	 handler run, the process going on in it -- by a signaller whose
	 grace ran out before the terminator's did.  The section is left to
	 the terminator, who gives it up when its own grace does."
	terminating ifTrue: [^false].
	(self isPending: anException) ifFalse: [^false].
	exceptions removeAll: (exceptions select: [:each | each == anException]).
	givenUp := true.
	^true
]

{ #category : 'requesting' }
DeferredStop >> signal: anException [
	"Raise anException in the process when the section ends, before any
	 already asked for: newest first, as outside a section."
	exceptions isNil ifTrue: [exceptions := OrderedCollection new].
	exceptions addLast: anException
]

{ #category : 'evaluating' }
DeferredStop >> value [
	"Run as the section's unwind block, by runUnwindBlock: run the block
	 this replaced -- the lock's release -- and then do what was asked,
	 if this is the process it was asked of and the section is ending in
	 that process's own course (see the class comment)."
	| unwinder how |
	guarded value.
	Processor activeProcess == process ifFalse: [^nil].
	unwinder := thisContext sender.
	how := self howLeftFrom: unwinder.
	how == #terminating ifTrue: [^nil].
	terminating ifTrue: [^process terminate].
	how == #unwinding ifTrue: [exceptions := nil. ^nil].
	self raiseAfter: unwinder
]

{ #category : 'private' }
DeferredStop >> howLeftFrom: anUnwinder [
	"How the section is being left, read from the frames between the
	 runUnwindBlock running me and the section's own frame: #terminating
	 if by the process's own terminate, #unwinding if by an exception's
	 return:, retry or default action, and #ending otherwise -- normally,
	 or by a ^ through it, both of which are the process's own course."
	| ctx terminator unwinder |
	terminator := Process compiledMethodAt: #runUnwindBlocksFrom:.
	unwinder := Exception compiledMethodAt: #unwindTo:.
	ctx := anUnwinder sender.
	[ctx isNil or: [ctx == section]] whileFalse: [
		ctx method == terminator ifTrue: [^#terminating].
		ctx method == unwinder ifTrue: [^#unwinding].
		ctx := ctx sender].
	^#ending
]

{ #category : 'private' }
DeferredStop >> raiseAfter: anUnwinder [
	"Raise the exceptions asked for in the frame anUnwinder -- the
	 runUnwindBlock running me -- returns to, once it has: the shape of
	 Process>>signal:after:waitingOn:.  The frame answers nil to
	 anUnwinder's sender, which every sender of runUnwindBlock discards."
	| pending frame |
	exceptions isNil ifTrue: [^nil].
	pending := exceptions.
	exceptions := nil.
	frame := [pending reverseDo: [:each | each signal]. nil] asContext.
	"Field one of any context is its sender."
	frame instVarAt: 1 put: anUnwinder sender.
	anUnwinder instVarAt: 1 put: frame.
	^nil
]

{ #category : 'evaluating' }
DeferredStop >> takeEffect [
	"Do now what was asked, in the process it was asked of, from a stable
	 point inside the section -- Process>>stopIfDeferred, where the
	 process is about to wait or has just come back from waiting.  A
	 terminate ends it from here, through the section's unwind block,
	 which is still me and then finds the process's own terminate under
	 way; the exceptions are raised here, once each, newest first, and a
	 handler that resumes the last lets the wait go on."
	| pending |
	terminating ifTrue: [^process terminate].
	exceptions isNil ifTrue: [^nil].
	pending := exceptions.
	exceptions := nil.
	pending reverseDo: [:each | each signal]
]

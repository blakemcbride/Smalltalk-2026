"
Mutual exclusion between native threads.

Semaphore already does this correctly -- primitives 85 and 86 are atomic,
and Phase H made the test-then-add in wait a single critical section, which
is the bug that made a signal landing at the wrong moment vanish.  So this
class is not new mechanism.  It is the two things a bare Semaphore gets
wrong when a human uses it.

The first is that a critical block which does not finish keeps the lock for
ever.  A non-local return out of it, or an exception through it, leaves
excessSignals at zero and every other worker waiting on nothing.  So the
release is in an ensure: and cannot be skipped, which is why this class
could not be written before exceptions existed.

The second is re-entry.  A Semaphore has no idea who holds it, so a process
that takes the same one twice waits for itself, for ever, with no
diagnostic -- the single most common way to deadlock a Smalltalk image.
The owner is recorded here, so that case is DETECTED and raises an error
naming it.  It is not made to work: a recursive mutex is a different
promise, and one that hides the design mistake rather than reporting it.

The nil check in acquire is not defensive noise.  Processor activeProcess
answers nil wherever no Process has been nominated -- during the bootstrap,
and in any worker running a method that was not entered through the
scheduler -- and `owner == me' with both of them nil reads as re-entry.  A
free Mutex then refuses its very FIRST acquire, reporting a deadlock that
cannot exist.  The gate found it on the first native-thread run.

A non-blocking tryCritical: belongs here and is deliberately absent.  It
needs a wait that can fail, and there is no primitive for one; writing it
in Smalltalk means testing excessSignals and then decrementing it, which is
the exact test-then-act race Phase H removed from Semaphore>>wait.  It
waits for VM support rather than for a plausible-looking version of it.
"
Class {
	#name : 'Mutex',
	#superclass : 'Object',
	#instVars : [ 'semaphore', 'owner' ],
	#category : 'Concurrency'
}

{ #category : 'initialization' }
Mutex >> initialize [
	semaphore := Semaphore forMutualExclusion.
	owner := nil
]

{ #category : 'accessing' }
Mutex >> owner [
	"The process holding this, or nil.  For diagnostics; by the time the
	 answer is read it may already be stale, which is in the nature of the
	 question."
	^owner
]

{ #category : 'accessing' }
Mutex >> isHeld [
	^owner notNil
]

{ #category : 'evaluating' }
Mutex >> critical: aBlock [
	"Evaluate aBlock with this held, and answer what it answers.  The
	 release happens however aBlock leaves -- normally, by non-local
	 return, or through a signalled exception -- and only if this process
	 got the lock.

	 The wait is INSIDE the protected block, and the ensure: block asks
	 the owner field rather than assuming.  It was `self acquire. ^aBlock
	 ensure: [self release]', and a process terminated after its wait had
	 returned but before ensure: was entered held the lock with no unwind
	 block to give it back: forty processes looping on one Mutex, one
	 terminated every 5 ms, left it locked for ever seven runs in seven
	 (Bugs5 OM-5).  With the wait inside, a process stopped anywhere
	 between entering the block and recording itself in owner is covered:
	 before the wait or taken out of it, owner is not this process and
	 nothing is released; after it, Process>>terminate records the owner
	 that waitFor: had not yet written (see noteOwner:stoppedIn:waiting:).

	 The re-entry check stays outside, where an error it raises unwinds
	 through nothing of ours: inside, its unwind would release the lock
	 the OUTER critical: holds."
	| me |
	me := Processor activeProcess.
	(me notNil and: [owner == me]) ifTrue: [
		^Error new signal: 'Mutex is already held by this process'].
	^[self waitFor: me.
	  aBlock value]
		ensure: [owner == me ifTrue: [self release]]
]

{ #category : 'locking' }
Mutex >> acquire [
	"Take this, waiting if another process holds it.

	 Public because Monitor has to release and retake the lock around a
	 wait, which critical: alone cannot express.  Anything that calls this
	 owes a release on every path out, which is what critical: exists to
	 guarantee -- prefer it."
	| me |
	me := Processor activeProcess.
	(me notNil and: [owner == me]) ifTrue: [
		^Error new signal: 'Mutex is already held by this process'].
	self waitFor: me
]

{ #category : 'private' }
Mutex >> waitFor: aProcess [
	"Wait for the semaphore and record aProcess as the owner, and nothing
	 else -- the shape is the point.

	 A process can be stopped at any bytecode, so there is a moment, after
	 the wait has handed it the lock and before owner says so, when it
	 holds the lock and nothing records it.  In a method of its own that
	 moment is recognisable from outside: the top context is this method,
	 stopped after its send of wait, and the process was not taken off the
	 semaphore.  noteOwner:stoppedIn:waiting: is the outside that looks."
	semaphore wait.
	owner := aProcess
]

{ #category : 'private' }
Mutex >> noteOwner: aProcess stoppedIn: aContext waiting: wasWaiting [
	"aProcess has been stopped, with aContext on top, by a terminate or a
	 signalException: that is about to unwind it.  If it was stopped in
	 waitFor: after its wait had returned, it holds this and owner does not
	 yet say so: record it, so that the ensure: in critical: gives it back.

	 wasWaiting is whether the stop took it OFF the semaphore -- then the
	 wait never completed, the pc is past the send all the same, and the
	 lock belongs to somebody else."
	| stream sendEnd |
	wasWaiting ifTrue: [^self].
	aContext method == (Mutex compiledMethodAt: #waitFor:) ifFalse: [^self].
	aContext pc isNil ifTrue: [^self].
	"The pc just past the method's one send.  waitFor: sends nothing but
	 wait, so the first send found is it."
	stream := InstructionStream on: aContext method.
	(stream scanFor: [:byte | byte >= 208 or: [byte between: 131 and: 134]])
		ifFalse: [^self].
	sendEnd := stream pc + ((aContext method at: stream pc) >= 208
		ifTrue: [1]
		ifFalse: [(aContext method at: stream pc) = 132 ifTrue: [3] ifFalse: [2]]).
	aContext pc >= sendEnd ifTrue: [owner := aProcess]
]

{ #category : 'locking' }
Mutex >> release [
	"Give this up.  Answers the receiver."
	owner := nil.
	semaphore signal
]

{ #category : 'printing' }
Mutex >> printOn: aStream [
	aStream nextPutAll: 'a Mutex('.
	aStream nextPutAll: (owner isNil ifTrue: ['free'] ifFalse: ['held']).
	aStream nextPut: $)
]

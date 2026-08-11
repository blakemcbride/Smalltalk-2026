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
	 return, or through a signalled exception."
	self acquire.
	^aBlock ensure: [self release]
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
	owner == me ifTrue: [
		^Error new signal: 'Mutex is already held by this process'].
	semaphore wait.
	owner := me
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

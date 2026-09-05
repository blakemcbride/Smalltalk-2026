"
Mutual exclusion with a condition to wait on.

A Mutex answers ''nobody else is in here''.  A Monitor answers the question
that always follows: ''and I cannot go on until something changes''.  A
consumer holding the lock over an empty queue must release it -- otherwise
no producer could ever fill it -- and take it back once told, and those two
have to be arranged so that a notification cannot land in the gap between
them and be lost.

That gap is the whole difficulty, and it is why this is a class rather than
an idiom.  waitForChange puts this process on the waiters list BEFORE it
releases the lock, so a signaller arriving immediately afterwards finds it
there.  And because each waiter waits on its own Semaphore, a signal that
arrives even before the wait begins is not lost either -- it is counted,
and the wait returns at once.  There is no ordering of the two that drops
a wakeup.

waitUntil: re-tests in a loop rather than trusting one wakeup.  A woken
waiter is not promised that what it waited for is still true, because
another worker may have taken it in between; on eight cores that is not a
corner case but the ordinary outcome of two consumers and one item.

Reentrant, which a Mutex deliberately is not.  A Monitor is the lock a
class puts round its whole protocol, and a method of that protocol that
sends another is the ordinary case, not the mistake Mutex refuses to hide;
doc/CONCURRENCY.md and the manual promised it and the class did not keep
the promise -- `m critical: [m critical: [...]]' raised 'Mutex is already
held' (Bugs3 B60).  So the owner and a depth are kept here, above the
Mutex: a process that already holds the monitor goes straight in, one
deeper, and lets go only when the outermost critical: does.  waitForChange
has to give up the WHOLE depth, or a waiter two levels in would sleep with
the monitor still held and nothing could ever change; it remembers the
depth and puts it back once it has the monitor again.

Thread-safe by the shape of the test rather than by another lock: `owner'
is written only by the process that holds the mutex, so a process that
reads its own identity there is the holder and nothing can have changed
that between the read and the increment; a process that reads anything
else takes the mutex, which orders it after the last release.  The nil
guard is the one Mutex>>acquire explains: Processor activeProcess answers
nil where no Process is nominated -- the bootstrap -- and nil == nil would
read as re-entry into a free monitor.
"
Class {
	#name : 'Monitor',
	#superclass : 'Object',
	#instVars : [ 'mutex', 'waiters', 'owner', 'depth' ],
	#category : 'Concurrency'
}

{ #category : 'initialization' }
Monitor >> initialize [
	mutex := Mutex new.
	waiters := OrderedCollection new.
	owner := nil.
	depth := 0
]

{ #category : 'evaluating' }
Monitor >> critical: aBlock [
	"Evaluate aBlock holding the monitor.  Reentrant: from inside
	 critical: this goes one level deeper and lets go when that level
	 does; the monitor is released when the outermost block ends, however
	 it ends."
	| me |
	me := Processor activeProcess.
	(me notNil and: [owner == me]) ifTrue: [
		depth := depth + 1.
		^aBlock ensure: [depth := depth - 1]].
	mutex acquire.
	owner := me.
	depth := 1.
	^aBlock ensure: [
		depth := 0.
		owner := nil.
		mutex release]
]

{ #category : 'waiting' }
Monitor >> waitUntil: aConditionBlock [
	"Answer once aConditionBlock answers true, holding the monitor both
	 while it is tested and when this returns.  Send only from inside
	 critical:."
	[aConditionBlock value] whileFalse: [self waitForChange]
]

{ #category : 'waiting' }
Monitor >> waitForChange [
	"Release the monitor, wait until something signals a change, and take
	 the monitor back.  Send only from inside critical:.

	 The whole depth is given up, not one level: a waiter that entered
	 critical: twice still has to let everyone else in while it sleeps.
	 The depth comes back with the monitor.

	 'Send only from inside critical:' was documented and unguarded, and
	 Bugs4 PROC-4 is what that cost: sent from outside, this releases a Mutex
	 the sender never took, and a Mutex release is an unconditional signal.
	 Every process queued on the monitor's semaphore was let through, and the
	 section that admits one process at a time admitted two hundred and five
	 at once -- silently, and nowhere near the line that did it.  An
	 unbalanced release cannot be undone once it has been spent, so the
	 precondition is checked before anything is released rather than
	 diagnosed afterwards."
	| mine savedDepth |
	self isHeldByActiveProcess ifFalse: [
		^Error new signal: 'waitForChange must be sent from inside critical:'].
	mine := Semaphore new.
	waiters addLast: mine.
	savedDepth := depth.
	depth := 0.
	owner := nil.
	mutex release.
	mine wait.
	mutex acquire.
	owner := Processor activeProcess.
	depth := savedDepth
]

{ #category : 'waiting' }
Monitor >> signalAll [
	"Wake everything waiting.  Send from inside critical:.

	 Every waiter rather than one, because a Monitor cannot know whose
	 condition a given change satisfies; waking one risks waking the wrong
	 one and stalling with work available.  The others re-test and go back
	 to sleep, which is cheap and cannot deadlock."
	| woken |
	woken := waiters.
	waiters := OrderedCollection new.
	woken do: [:each | each signal]
]

{ #category : 'waiting' }
Monitor >> signal [
	"Wake one waiter, if any.  Correct only where every waiter is waiting
	 for the same thing; when in doubt send signalAll."
	waiters isEmpty ifTrue: [^self].
	waiters removeFirst signal
]

{ #category : 'testing' }
Monitor >> hasWaiters [
	^waiters isEmpty not
]

{ #category : 'testing' }
Monitor >> isHeldByActiveProcess [
	"Whether the process asking holds me -- what a method that must be
	 sent from inside critical: can assert.  Exact for the asker, since
	 only the holder can find itself in owner."
	| me |
	me := Processor activeProcess.
	^me notNil and: [owner == me]
]

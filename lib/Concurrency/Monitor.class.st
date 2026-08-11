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
"
Class {
	#name : 'Monitor',
	#superclass : 'Object',
	#instVars : [ 'mutex', 'waiters' ],
	#category : 'Concurrency'
}

{ #category : 'initialization' }
Monitor >> initialize [
	mutex := Mutex new.
	waiters := OrderedCollection new
]

{ #category : 'evaluating' }
Monitor >> critical: aBlock [
	"Evaluate aBlock holding the monitor."
	^mutex critical: aBlock
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
	 the monitor back.  Send only from inside critical:."
	| mine |
	mine := Semaphore new.
	waiters addLast: mine.
	mutex release.
	mine wait.
	mutex acquire
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

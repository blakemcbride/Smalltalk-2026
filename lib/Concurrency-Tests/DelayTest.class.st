"
Delay, single-threaded: the timing process, and the one failure mode that
killed it on cores.  test_parallel_shared.c is the gate on real workers;
these hold the shape of the rewrite where any profile can run them.
"
Class {
	#name : 'DelayTest',
	#superclass : 'TestCase',
	#category : 'Concurrency-Tests'
}

{ #category : 'tests' }
DelayTest >> testWaitReturnsAfterItsTime [
	| before |
	before := Time millisecondClockValue.
	(Delay forMilliseconds: 30) wait.
	self assert: Time millisecondClockValue - before >= 25
]

{ #category : 'tests' }
DelayTest >> testADelayCanBeWaitedOnAgain [
	| delay |
	delay := Delay forMilliseconds: 5.
	delay wait.
	delay wait.
	self assert: true
]

{ #category : 'tests' }
DelayTest >> testTheShorterOfTwoDelaysResumesFirst [
	"Two processes, the second forked with the shorter delay: it must be
	 the one that resumes first, which is the queue doing its job."
	| order lock |
	order := OrderedCollection new.
	lock := Mutex new.
	[(Delay forMilliseconds: 60) wait. lock critical: [order add: #long]] fork.
	[(Delay forMilliseconds: 15) wait. lock critical: [order add: #short]] fork.
	(Delay forMilliseconds: 150) wait.
	self assert: order asArray equals: #(#short #long)
]

{ #category : 'tests' }
DelayTest >> testAStaleTimerSignalDoesNotKillTheTimingProcess [
	"What hung two workers: a signal on TimingSemaphore arriving when no
	 delay is due.  1983's timing process sent delayInProgress: to nil and
	 died; every Delay after it then waited for ever.  A stale signal is
	 now a look at the clock, so the next wait still returns."
	(Delay forMilliseconds: 1) wait.
	(Delay classPool at: #TimingSemaphore) signal.
	(Delay forMilliseconds: 5) wait.
	(Delay classPool at: #TimingSemaphore) signal; signal.
	(Delay forMilliseconds: 5) wait.
	self assert: true
]

{ #category : 'tests' }
DelayTest >> testADurationIsRoundedToTheMillisecond [
	"Bugs3 B12: a Float duration reached the timer, which wants an
	 Integer, with ActiveDelay already pointing at the Delay, and no
	 Delay in the image ever returned again.  Rounded when the Delay is
	 made now -- half a millisecond asked for is a millisecond, not
	 nothing -- and the second wait is the check that the first did no
	 harm."
	self assert: ((Delay forMilliseconds: 0.5) instVarAt: 1) equals: 1.
	self assert: ((Delay forMilliseconds: 0.4) instVarAt: 1) equals: 0.
	self assert: ((Delay forSeconds: 1.5) instVarAt: 1) equals: 1500.
	(Delay forMilliseconds: 0.5) wait.
	(Delay forMilliseconds: 5) wait.
	self assert: true
]

{ #category : 'tests' }
DelayTest >> testANegativeDurationIsOver [
	self assert: ((Delay forMilliseconds: -7) instVarAt: 1) equals: 0.
	(Delay forMilliseconds: -7) wait.
	(Delay forMilliseconds: 2) wait.
	self assert: true
]

{ #category : 'tests' }
DelayTest >> testWhatTheTimerCannotCountIsRefusedWhenMade [
	"Not inside wait, where 1983 found out, with the lock held."
	self should: [Delay forSeconds: 1e12] raise: Error.
	self should: [Delay forMilliseconds: Delay maximumMilliseconds + 1] raise: Error.
	self should: [Delay forMilliseconds: 'soon'] raise: Error.
	self should: [Delay untilMilliseconds: -1] raise: Error.
	(Delay forMilliseconds: Delay maximumMilliseconds) instVarAt: 1.
	(Delay forMilliseconds: 2) wait.
	self assert: true
]

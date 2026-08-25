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

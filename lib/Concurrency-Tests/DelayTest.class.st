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

{ #category : 'tests' }
DelayTest >> testTheMillisecondClockDoesNotWrap [
	"Bugs4 CHRON-1.  The Blue Book's clock is thirty bits and goes back to
	 zero every 12.43 days of machine uptime, and every comparison against
	 it in this image is a plain one -- so a Delay that straddled a wrap
	 was never due again, its waiter never woke, and the timing process
	 re-armed and re-woke for ever.  On one worker that is the whole image
	 hung.

	 The clock is unmasked now, and 62 bits of SmallInteger is 146 million
	 years.  What that changes here is the range of a resumption time: the
	 documented way to build one is `Delay untilMilliseconds:
	 Delay millisecondClockValue + n', which on a machine up for a
	 fortnight is past 2^30 and used to be refused as `not a value of the
	 millisecond clock'."
	self assert: Time millisecondClockValue isInteger.
	self assert: Time millisecondClockValue > 0.
	self assert: (Delay untilMilliseconds: 2000000000) resumptionTime
		equals: 2000000000.
	self assert: (Delay untilMilliseconds: Delay millisecondClockValue + 1000)
		resumptionTime > Delay millisecondClockValue.
	self assert: (Time millisecondsToRun: [100 factorial]) >= 0
]

{ #category : 'tests' }
DelayTest >> testWaitArmsATimerThatIsNotThere [
	"Bugs4 CHRON-2, without the snapshot.

	 An image resumed from a file comes back with a queue of delays and a
	 VM whose timer is not armed at all, because the timer lives in the VM
	 and the VM is new.  1983 arms only when a delay BECOMES the head, so
	 a wait that filed itself behind the stale head armed nothing, nothing
	 was counting, and the VM stopped with `every process is blocked'.

	 `Processor signal: nil atTime: 0' is that state exactly: it cancels
	 the VM's one outstanding request and leaves the image's queue alone.
	 The last wait below is LONGER than the head's remaining time, so it
	 takes the branch that files behind the head rather than the one that
	 becomes it -- and the only thing that can wake either of them is that
	 branch arming for the head.

	 A regression here does not fail, it stops: both processes wait on
	 semaphores nothing signals.  That is loud rather than silent -- the
	 VM says `every process is blocked; nothing can run', or the run ends
	 at its bytecode limit -- and it is the honest shape of the fault,
	 which is a wait that never returns.  It was written as a polling loop
	 first, and 500 milliseconds of `Processor yield' cost more bytecodes
	 than the whole suite is allowed."
	| headWoke |
	headWoke := Array with: false.
	[(Delay forMilliseconds: 60) wait. headWoke at: 1 put: true] fork.
	(Delay forMilliseconds: 10) wait.
	"The timer is now armed for the forked delay, and nothing else is."
	Processor signal: nil atTime: 0.
	(Delay forMilliseconds: 200) wait.
	self assert: (headWoke at: 1)
]

{ #category : 'tests' }
DelayTest >> testASnapshotRoundTripKeepsTheTimeLeft [
	"Bugs4 CHRON-2, the arithmetic half.

	 A resumption time is a reading of this machine's monotonic clock and
	 means nothing to the image that resumes the file, so preSnapshot puts
	 it on the wall clock and postSnapshot puts it back.  The two are far
	 apart -- milliseconds since 1901 against milliseconds since the
	 machine started -- so the middle assertion is not a formality.

	 The class-side pair is done twice because preSnapshot takes
	 AccessProtect and postSnapshot releases it: a postSnapshot that
	 raised on the way, as 1983's does for a Delay that is not waiting,
	 would leave the lock held and the second pre would never return."
	| delay before |
	delay := Delay forMilliseconds: 60000.
	delay setResumption.
	before := delay resumptionTime.
	delay preSnapshot.
	self assert: delay resumptionTime > (before * 100).
	delay postSnapshot.
	self assert: (delay resumptionTime - before) abs <= 3000.
	Delay preSnapshot.
	Delay postSnapshot.
	Delay preSnapshot.
	Delay postSnapshot.
	self assert: true
]

{ #category : 'tests' }
DelayTest >> testTerminatingAWaiterTakesItsDelayOutOfTheQueue [
	"Bugs4 CHRON-6.  1983 has Delay>>disable for this and nothing sends
	 it, so a terminated timed wait left its Delay and its Semaphore in
	 the queue until the delay's own time came -- up to six days for a
	 `Delay maximumMilliseconds' -- and every wait in between paid for it
	 in the sorted insert.  wait retires the Delay from an ensure: now,
	 and terminate runs unwind blocks."
	| queue processes queued |
	queue := Delay classPool at: #SuspendedDelays.
	processes := (1 to: 20) collect: [:i | [(Delay forMilliseconds: 5000) wait] fork].
	(Delay forMilliseconds: 100) wait.
	queued := queue size.
	self assert: queued >= 19.
	processes do: [:each | each terminate].
	(Delay forMilliseconds: 100) wait.
	self assert: queued - queue size >= 19
]

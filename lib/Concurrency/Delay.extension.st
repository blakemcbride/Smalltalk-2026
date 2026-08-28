"
Delay, with a timing process that survives a stale signal.

1983's timing process is a loop: wait on TimingSemaphore, take
AccessProtect, resume ActiveDelay, activate the next.  It assumes every
signal on TimingSemaphore is the one it armed for the delay it is about to
resume, and it keeps that true by cancelling the timer when it resumes a
delay early and by initSignals when it arms a new one.  Both are races on
cores.  Two workers waiting on one-millisecond delays hung every run:
worker B, holding AccessProtect, replaced ActiveDelay with an earlier
delay and re-armed the timer while the timing process was already awake
for the old one; the timing process then resumed B's delay before its
time and cancelled a timer that had already fired, and the signal that
firing had queued arrived when ActiveDelay was nil.  `nil delayInProgress:
false' ended the timing process, and every Delay after it waited for ever.
initSignals is the same shape from the other side: a plain store of zero
into a count the VM increments from another thread.

The loop below treats a signal on TimingSemaphore as `look at the clock',
not as `the head is due'.  Holding AccessProtect, it resumes every delay
whose time has come -- the head and whatever behind it is due too -- and
re-arms the timer for whatever is left at the head.  A signal that arrives
with nothing due re-arms and goes round again; a signal that arrives with
ActiveDelay nil does nothing at all.  Nothing cancels the timer and
nothing resets the semaphore, so there is nothing to race.  Re-arming
replaces the VM's one outstanding request, which is what primitive 136
does anyway.

The lock is released by ensure: in both the timing process and wait, so
an error inside either -- setResumption refusing a Delay that is already
waiting, say -- cannot leave AccessProtect held and every later wait
behind it.  And what is given to delay: is checked there, when the Delay
is made, rather than discovered at armTimer with the lock held and the
class variables half changed: a Float, a negative, or a duration wider
than the timer counts is rounded, floored at zero, or refused with an
error that says so (Bugs3 B12).

delaySemaphore is here for BlockClosure>>valueWithin:onTimeout:, which
signals it directly to retire a watchdog whose job is done; the Delay is
then resumed again when its time comes, which signals a semaphore nobody
is waiting on, which is harmless.
"
Extension { #name : 'Delay' }

{ #category : '*Concurrency' }
Delay class >> initializeTimingProcess [
	"Fork the process that resumes delays as their times come."
	[[true] whileTrue: [
		TimingSemaphore wait.
		AccessProtect wait.
		[self resumeWhatIsDue] ensure: [AccessProtect signal]]]
			forkAt: Processor timingPriority
]

{ #category : '*Concurrency' }
Delay class >> resumeWhatIsDue [
	"With AccessProtect held: resume every delay whose time has come, and
	 arm the timer for the earliest that has not."
	| now |
	now := Time millisecondClockValue.
	[ActiveDelay notNil and: [ActiveDelay resumptionTime <= now]] whileTrue: [ | done |
		done := ActiveDelay.
		ActiveDelay := SuspendedDelays isEmpty
			ifTrue: [nil]
			ifFalse: [SuspendedDelays removeFirst].
		done resume].
	ActiveDelay notNil ifTrue: [ActiveDelay armTimer]
]

{ #category : '*Concurrency' }
Delay >> delaySemaphore [
	"The semaphore this Delay's waiter is suspended on.
	 Signalling it wakes that waiter without cancelling the delay, which is
	 what a watchdog wants on the path where the thing it was watching
	 finished in time."
	^delaySemaphore
]

{ #category : '*Concurrency' }
Delay >> wait [
	"Suspend the active process until my resumption time.

	 Nothing shared is changed until the timer is armed.  This used to
	 move the old ActiveDelay onto SuspendedDelays and then activate,
	 which stored ActiveDelay before arming; an armTimer that raised --
	 for a Float duration, or one wider than the primitive takes -- left
	 ActiveDelay pointing at a delay with no timer behind it, and every
	 later, shorter delay was filed behind it in SuspendedDelays, where
	 nothing would ever fire it (Bugs3 B12).  delay: now refuses what the
	 timer cannot count, so armTimer cannot raise; the order here is what
	 keeps the class variables consistent should it ever learn to."
	AccessProtect wait.
	[self setResumption.
	 (ActiveDelay isNil or: [resumptionTime < ActiveDelay resumptionTime])
		ifTrue: [ | displaced |
			displaced := ActiveDelay.
			self activate.
			displaced isNil ifFalse: [SuspendedDelays add: displaced]]
		ifFalse: [SuspendedDelays add: self]]
			ensure: [AccessProtect signal].
	delaySemaphore wait
]

{ #category : '*Concurrency' }
Delay >> activate [
	"Become the delay the timer is armed for.  With AccessProtect held.
	 Armed first and published second, so that a delay the VM would not
	 time is never the delay everything else queues behind."
	self armTimer.
	ActiveDelay := self
]

{ #category : '*Concurrency' }
Delay >> delay: millisecondCount [
	"Set my duration, checked.  1983 stored whatever it was given and
	 found out at armTimer, inside wait, with AccessProtect held and the
	 class variables half changed: `Delay forMilliseconds: 0.5' was a
	 doesNotUnderstand of digitLength on the Float and then no Delay in
	 the image ever returned (Bugs3 B12)."
	delayDuration := self class checkedMilliseconds: millisecondCount.
	delayInProgress := false.
	delaySemaphore := Semaphore new
]

{ #category : '*Concurrency' }
Delay >> resumption: millisecondCount [
	"Set the clock value I resume at, checked the same way."
	delayDuration := nil.
	resumptionTime := self class checkedClockValue: millisecondCount.
	delayInProgress := false.
	delaySemaphore := Semaphore new
]

{ #category : '*Concurrency' }
Delay class >> maximumMilliseconds [
	"The longest delay the timer can count: 2^29 - 1, six days and change.
	 The VM's millisecond clock wraps at 2^30, and primitive 136 reads a
	 target more than half a wrap ahead as already past -- so a longer
	 delay would not be refused, it would return at once."
	^536870911
]

{ #category : '*Concurrency' }
Delay class >> checkedMilliseconds: aNumber [
	"aNumber as a duration the timer can count, or an error that says why.

	 Rounded to the millisecond rather than truncated, because that is
	 the clock's grain and forSeconds: already rounds: half a millisecond
	 asked for is a millisecond, not nothing.  A negative duration is a
	 delay that is already over, and is zero."
	| count |
	aNumber isNumber ifFalse: [
		^self error: 'a Delay is made from a number of milliseconds, not ' , aNumber printString].
	count := aNumber rounded.
	count < 0 ifTrue: [^0].
	count > self maximumMilliseconds ifTrue: [
		^self error: 'a Delay of ' , count printString , ' milliseconds is longer than the timer can count (' , self maximumMilliseconds printString , ')'].
	^count
]

{ #category : '*Concurrency' }
Delay class >> checkedClockValue: aNumber [
	"aNumber as a value of the millisecond clock, which counts from zero
	 to 2^30 - 1 and then wraps."
	| value |
	aNumber isNumber ifFalse: [
		^self error: 'a Delay resumes at a millisecond clock value, not ' , aNumber printString].
	value := aNumber rounded.
	(value < 0 or: [value >= 1073741824]) ifTrue: [
		^self error: value printString , ' is not a value of the millisecond clock'].
	^value
]

{ #category : '*Concurrency' }
Delay >> armTimer [
	"Ask the VM to signal TimingSemaphore at my resumption time.  There is
	 one request outstanding; this replaces it."
	Processor signal: TimingSemaphore atTime: resumptionTime
]

{ #category : '*Concurrency' }
Delay >> reactivate [
	"After a snapshot: arm the timer again if I was waiting."
	delayInProgress ifTrue: [self armTimer]
]

{ #category : '*Concurrency' }
Delay >> resume [
	"My time has come: release my waiter.  With AccessProtect held."
	delayInProgress := false.
	delaySemaphore signal
]

{ #category : '*Concurrency' }
Delay >> disable [
	"Terminate the process waiting on me, and take me out of the queue.
	 The timer is left armed: a signal that finds nothing due is ignored."
	AccessProtect wait.
	[delayInProgress ifTrue: [
		ActiveDelay == self
			ifTrue: [
				ActiveDelay := SuspendedDelays isEmpty
					ifTrue: [nil]
					ifFalse: [SuspendedDelays removeFirst].
				ActiveDelay notNil ifTrue: [ActiveDelay armTimer]]
			ifFalse: [SuspendedDelays remove: self ifAbsent: []].
		delaySemaphore terminateProcess.
		delayInProgress := false]]
			ensure: [AccessProtect signal]
]

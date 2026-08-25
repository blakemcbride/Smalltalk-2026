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
behind it.

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
	"Suspend the active process until my resumption time."
	AccessProtect wait.
	[self setResumption.
	 (ActiveDelay isNil or: [resumptionTime < ActiveDelay resumptionTime])
		ifTrue: [
			ActiveDelay isNil ifFalse: [SuspendedDelays add: ActiveDelay].
			self activate]
		ifFalse: [SuspendedDelays add: self]]
			ensure: [AccessProtect signal].
	delaySemaphore wait
]

{ #category : '*Concurrency' }
Delay >> activate [
	"Become the delay the timer is armed for.  With AccessProtect held."
	ActiveDelay := self.
	self armTimer
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

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
signals it directly to retire a watchdog whose job is done.

Three things this file did not know about, all found by Bugs4.

The millisecond clock used to wrap every 12.43 days and every comparison
here read it as though it did not.  `ActiveDelay resumptionTime <= now'
was false for every now after a wrap, so a Delay that straddled one was
never due again: its waiter slept for ever, and the timing process --
the highest priority process in the image -- re-armed, woke, found
nothing due and re-armed, for ever.  On one worker that is the whole
image hung (CHRON-1).  Time class>>millisecondClockValue now answers the
unmasked count, 62 bits of it, so the plain comparisons here are right
again and armTimer below is the one place that still knows about the
modulus -- it reduces the target primitive 136 takes, because THAT is
still four bytes wide.

Nothing re-armed the VM's timer when an image was resumed from a
snapshot.  The timer lives in the VM and a resumed image gets a new one,
disarmed; the head of the queue was by then a delay whose time had
passed, so a fresh `wait' filed itself behind the head without arming
anything, no timer was pending, and the VM stopped with `every process
is blocked' (CHRON-2).  Two answers, because the fault has two halves:
postSnapshot drains what is due and arms for the rest, which is the
right thing on the path snapshotAs:thenQuit: takes; and wait re-arms for
the head every time, which costs one primitive call and is the answer
for every other path, including a raw snapshotPrimitive.

And terminating a process that was waiting on a Delay left the Delay in
the queue until its own time came -- up to six days for a
`Delay maximumMilliseconds' -- holding its Semaphore and slowing every
later wait through the sorted insert (CHRON-6).  1983 has `disable' for
exactly this and nothing sends it.  wait now retires the Delay in an
ensure:, so terminate runs it on the way out; the same block covers the
watchdog case above, where valueWithin:onTimeout: signals the semaphore
early and the Delay would otherwise sit in the queue to its full term.
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
Delay class >> initialize [
	"Set up the class variables and fork the timing process.  Once.

	 1983's runs whenever it is sent, and the class comment invites it:
	 `Delay initialize.'  Sent a second time in a running image it makes a
	 new TimingSemaphore, a new AccessProtect and a new SuspendedDelays,
	 and forks a second timing process -- so every process already waiting
	 on a Delay is waiting on a semaphore in a queue nothing looks at any
	 more, and the old timing process is parked on a semaphore the VM will
	 never signal again.  Nothing reports it; the waiters simply never
	 return (Bugs4 CHRON-9).

	 The guard is on the QUEUE and not on whether the class variables are
	 already set, because the bootstrap's class-initializer pass runs every
	 initializer twice -- with the text style built in between, and with
	 the first pass allowed to fail silently -- so `already set' is the
	 ordinary state on the pass that matters, and refusing there would
	 leave the image with no timing process at all.  A re-initialization
	 with an empty queue loses nothing: the old timing process is left
	 parked on the old TimingSemaphore, which is unreachable the moment
	 this method replaces it, so the collector takes both.

	 A re-initialization with a queue is refused, because there is no
	 harmless version of it: what a caller who sends this wants is a
	 working Delay, and the only way to give them one is to not have lost
	 the queue they are already waiting in."
	(ActiveDelay notNil or: [SuspendedDelays notNil and: [SuspendedDelays notEmpty]])
		ifTrue: [
			^self error: 'Delay is already initialized: re-initializing it would strand every process now waiting on a Delay'].
	TimingSemaphore := Semaphore new.
	AccessProtect := Semaphore forMutualExclusion.
	SuspendedDelays := SortedCollection sortBlock:
		[:oldDelay :addedDelay |
			oldDelay resumptionTime <= addedDelay resumptionTime].
	ActiveDelay := nil.
	self initializeTimingProcess
]

{ #category : '*Concurrency' }
Delay class >> preSnapshot [
	"About to be written to a file: put every resumption time on a clock
	 the image can still recognise when it comes back.

	 resumptionTime is a reading of the VM's monotonic millisecond clock,
	 which counts from when the MACHINE started, so it means nothing to the
	 image that resumes this file -- another machine, or the same one after
	 a reboot.  1983's answer, kept here: convert each one to milliseconds
	 since 1 January 1901, which is a fact about the world rather than
	 about this VM, and convert it back in postSnapshot.

	 AccessProtect is taken here and released in postSnapshot, so the queue
	 cannot change while the image is being written.  1983 raises out of
	 the middle of this on a Delay that is not waiting and leaves the lock
	 held for ever; there is nothing to raise about, because a Delay in the
	 queue is by definition waiting and one that is not is not in it."
	AccessProtect wait.
	ActiveDelay notNil ifTrue: [ActiveDelay preSnapshot].
	SuspendedDelays do: [:delay | delay preSnapshot]
]

{ #category : '*Concurrency' }
Delay class >> postSnapshot [
	"Written, or resumed: put every resumption time back on this VM's
	 clock, resume whatever is already due, and arm the timer.

	 The arming is the part 1983 could take for granted and this system
	 cannot.  The timer is in the VM, and an image resumed from a file gets
	 a new VM with no timer armed at all; the head of the queue is by then
	 a delay whose time went by while the file sat on disk.  Nothing woke
	 it: a fresh `wait' filed itself behind that head, no timer was
	 pending, and the VM stopped with `every process is blocked; nothing
	 can run' -- a hundred processes waiting on five-second Delays, saved
	 and reloaded seven seconds later, all dead (Bugs4 CHRON-2).

	 resumeWhatIsDue is exactly the right thing to send: it is the timing
	 process's own body, so a stale head is drained by the same code that
	 drains a live one, and it arms for whatever is left.  1983 sent
	 `ActiveDelay reactivate' instead, which arms for a head it does not
	 first check the time of."
	[ActiveDelay notNil ifTrue: [
		ActiveDelay postSnapshot.
		SuspendedDelays do: [:delay | delay postSnapshot].
		self resumeWhatIsDue]]
			ensure: [AccessProtect signal]
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
		ifFalse: [
			"Filed behind the head, and the head re-armed anyway.  1983
			 armed the timer only when a delay BECAME the head, which
			 assumes the timer that was armed for the head is still
			 armed -- true within one run of the VM and false across a
			 snapshot, where the image comes back with a queue and the
			 VM comes back with no timer at all.  A wait that files
			 itself behind a stale head then armed nothing, no timer was
			 pending, and the VM stopped with every process blocked
			 (Bugs4 CHRON-2).  Re-arming is idempotent -- primitive 136
			 has one outstanding request and this replaces it with the
			 same one -- so the cost is a primitive call per wait and
			 the queue can no longer be left with nothing counting it."
			SuspendedDelays add: self.
			ActiveDelay armTimer]]
			ensure: [AccessProtect signal].
	"Retired on the way out however the wait ends.  Terminate runs unwind
	 blocks, so a process killed while waiting takes its Delay out of the
	 queue with it instead of leaving it there until its own time comes
	 (Bugs4 CHRON-6); and a waiter woken early by a direct signal on
	 delaySemaphore -- which is what valueWithin:onTimeout: does to retire
	 a watchdog -- takes it out too.  delayInProgress is already false on
	 the ordinary path, where the timing process cleared it before
	 signalling, so the ordinary wait pays one field read and no lock."
	[delaySemaphore wait] ensure: [delayInProgress ifTrue: [self retire]]
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
	"aNumber as a value of the millisecond clock.

	 The upper bound this used to carry -- 2^30, where the Blue Book's
	 clock wraps -- was right when Time class>>millisecondClockValue
	 answered a wrapped clock and wrong the moment it stopped: the
	 documented way to build one of these is `Delay untilMilliseconds:
	 Delay millisecondClockValue + 1000', and on a machine up for a
	 fortnight that sum is over 2^30 and was refused as `not a value of
	 the millisecond clock' (Bugs4 CHRON-1).  Any non-negative Integer is
	 a value of the wide clock; how far ahead of NOW it is cannot be
	 checked here, because it is asked when the Delay is made and answered
	 when it waits, and armTimer clamps it then."
	| value |
	aNumber isNumber ifFalse: [
		^self error: 'a Delay resumes at a millisecond clock value, not ' , aNumber printString].
	value := aNumber rounded.
	value < 0 ifTrue: [
		^self error: value printString , ' is not a value of the millisecond clock'].
	^value
]

{ #category : '*Concurrency' }
Delay >> armTimer [
	"Ask the VM to signal TimingSemaphore at my resumption time.  There is
	 one request outstanding; this replaces it.

	 This is the only place left in the image that knows the millisecond
	 clock has a modulus.  Primitive 136 takes a four-byte target and the
	 VM compares it against the clock's low thirty bits, reading more than
	 half a wrap ahead as already past; resumptionTime is on the wide
	 clock, which does not wrap at all.  So the wait is worked out here, in
	 the wide arithmetic where it is a plain subtraction, and only the
	 target handed over is reduced.

	 Clamped at both ends before it is reduced.  A resumption time already
	 past arms for now, which fires at once and lets resumeWhatIsDue drain
	 it -- that is how a queue resumed from a snapshot with stale times
	 gets cleared.  One further ahead than the timer counts arms for the
	 furthest it does count, which fires, finds nothing due and re-arms:
	 the timing process's loop is written to take a signal as `look at the
	 clock', so a premature one is a wasted trip round it and not a bug.
	 Without the top clamp such a target would reduce to a time in the past
	 and fire immediately, over and over."
	| now remaining |
	now := Time millisecondClockValue.
	remaining := ((resumptionTime - now max: 0)
					min: self class maximumMilliseconds).
	Processor signal: TimingSemaphore atTime: now + remaining \\ 1073741824
]

{ #category : '*Concurrency' }
Delay >> reactivate [
	"After a snapshot: arm the timer again if I was waiting."
	delayInProgress ifTrue: [self armTimer]
]

{ #category : '*Concurrency' }
Delay >> preSnapshot [
	"Put my resumption time on the wall clock, so that it survives being
	 written to a file.  With AccessProtect held.

	 UTC, not `Time totalSeconds', which 1983 uses and which is corrected
	 for the local zone: an image saved before a daylight-saving change and
	 resumed after it would have every pending delay moved by an hour.  How
	 long I have left to wait is not a fact about anybody's time zone.

	 Answers quietly for a Delay that is not waiting, where 1983 raises
	 `This Delay is not waiting' -- out of the middle of a class-side
	 preSnapshot that holds AccessProtect and has no ensure:, which would
	 leave the lock held and every later wait behind it."
	delayInProgress ifTrue: [
		resumptionTime := Time utcSeconds * 1000
			+ (resumptionTime - Time millisecondClockValue)]
]

{ #category : '*Concurrency' }
Delay >> postSnapshot [
	"Put my resumption time back on this VM's millisecond clock.  With
	 AccessProtect held.

	 The remainder is floored at zero because a snapshot can sit on a disk
	 for a year: the honest reading of a delay whose time went by while the
	 image was not running is `due now', and resumeWhatIsDue -- which the
	 class-side postSnapshot sends straight after this -- is what acts on
	 it (Bugs4 CHRON-2)."
	delayInProgress ifTrue: [
		resumptionTime := Time millisecondClockValue
			+ ((resumptionTime - (Time utcSeconds * 1000)) max: 0)]
]

{ #category : '*Concurrency' }
Delay >> resume [
	"My time has come: release my waiter.  With AccessProtect held."
	delayInProgress := false.
	delaySemaphore signal
]

{ #category : '*Concurrency' }
Delay >> retire [
	"Take me out of the queue.  Wakes nobody and terminates nobody.

	 This is disable's first half, split out because it is what an ended
	 wait needs: the waiter is already gone, or already awake, and all
	 that is left is a Delay in the queue with nobody behind it.  Left
	 there it costs its Semaphore and its entry until its own time comes,
	 which for a `Delay maximumMilliseconds' is six days, and every wait
	 in between pays for it in the sorted insert (Bugs4 CHRON-6).

	 The head is replaced and the timer re-armed for whatever is behind
	 it, so retiring the delay everything else is queued behind does not
	 leave the queue with nothing counting it."
	AccessProtect wait.
	[delayInProgress ifTrue: [
		delayInProgress := false.
		ActiveDelay == self
			ifTrue: [
				ActiveDelay := SuspendedDelays isEmpty
					ifTrue: [nil]
					ifFalse: [SuspendedDelays removeFirst].
				ActiveDelay notNil ifTrue: [ActiveDelay armTimer]]
			ifFalse: [SuspendedDelays remove: self ifAbsent: []]]]
				ensure: [AccessProtect signal]
]

{ #category : '*Concurrency' }
Delay >> disable [
	"Terminate the process waiting on me, and take me out of the queue.
	 The timer is left armed for the head: a signal that finds nothing due
	 is ignored.

	 The two halves are done in that order and NOT under one hold of
	 AccessProtect.  terminateProcess terminates the waiter, terminate
	 runs the unwind blocks of the process it terminates, and the block
	 wait leaves behind asks to retire this very Delay -- which wants the
	 lock.  Holding it across the terminate, as this method used to,
	 deadlocked the terminator against the process it was killing.
	 Retiring first also clears delayInProgress, so the waiter's own
	 unwind block finds nothing to do."
	self retire.
	delaySemaphore terminateProcess
]

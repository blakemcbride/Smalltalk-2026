"
Pharo's timeout, with its race decided once.

valueWithinMilliseconds:onTimeout: runs the receiver in the caller's
process and a watchdog in a process of its own, and the two share a
temporary: the receiver's ensure: block stores nil into theProcess when it
is done, and the watchdog, when its Delay fires, signals TimedOut into
whatever theProcess still holds.  Nothing orders the one against the
other.  Pharo is safe because it has one processor: the watchdog runs at
timingPriority - 1 and preempts the caller the instant its Delay fires,
so it finds theProcess either already nil or still inside the
`on: TimedOut do:'.  On a pool of workers the two run at once, and when
the work finishes as the time runs out -- the ordinary case for a timeout
sized to the work -- the watchdog read theProcess, the caller nilled it,
and the watchdog read it again for the send: `nil signalException:', an
unhandled doesNotUnderstand in the watchdog.  Or it read it twice before
the nil, the caller meanwhile left the on:do:, and the TimedOut was raised
in the caller wherever it had got to -- inside its next `Mutex
critical:', in an unwind block -- an Error nobody there expected, which
ended the process or was taken by an unrelated handler for that code's
own failure.  Eight workers of three hundred calls each were all hit
within their first thirty (Bugs07 KERNB-5).

So the race is decided once, by a compare-and-swap on a cell: the caller
in its ensure: moves it from #running to #done, the watchdog moves it from
#running to #firing, and only the one that moves it acts.  The watchdog
signals only if it won.  The caller, if it lost, waits in its ensure:
block until the watchdog's signalException: has returned -- and
signalException: puts off an exception aimed at a process inside an
unwind block to the end of that block (Bugs6 SCHED-6), which is still
inside the on: TimedOut do: that catches it.  The TimedOut can arrive
only there.  theProcess is never cleared, so there is nothing for the
watchdog to read twice.
"
Extension { #name : 'BlockClosure' }

{ #category : '*Chronology-Compat' }
BlockClosure >> valueWithinMilliseconds: milliseconds onTimeout: timeoutBlock [
	"Evaluate the receiver.
	If the evaluation does not complete in less than milliseconds evaluate the timeoutBlock instead"
	<debuggerCompleteToSender>

	| theProcess delay watchdog tag state fired |
	milliseconds <= 0 ifTrue: [^ timeoutBlock value ].

	"the block will be executed in the current process"
	theProcess := Processor activeProcess.
	delay := Delay forMilliseconds: milliseconds.
	tag := self.
	"Who acts: #running until the receiver is done or the time is up,
	 then #done or #firing, whichever got there first."
	state := Array with: #running.
	"Signalled by the watchdog once its signalException: has returned."
	fired := Semaphore new.

	"make a watchdog process"
	watchdog := [
		delay wait. 	"wait for timeout or completion"
		(state compareAndSwapSlot: 1 from: #running to: #firing) ifTrue: [
			[theProcess signalException: (TimedOut new tag: tag)]
				ensure: [fired signal]]
	] newProcess.

	"Watchdog needs to run at high priority to do its job (but not at timing priority)"
	watchdog priority: Processor timingPriority-1.

	"catch the timeout signal"
	^ [	watchdog resume.				"start up the watchdog"
		self ensure:[						"evaluate the receiver"
			(state compareAndSwapSlot: 1 from: #running to: #done)
				ifTrue: [delay delaySemaphore signal]	"arrange for the watchdog to exit"
				ifFalse: [fired wait]			"it is firing: take the TimedOut here"
		]] on: TimedOut do: [ :e |
			e tag == tag
				ifTrue:[ timeoutBlock value ]
				ifFalse:[ e pass]]
]

"
The semaphore a waiting process is parked on.

1983 keeps it in an instance variable and never lets anyone see it, because
in 1983 the only thing that ever released a Delay early was Delay>>disable.
Pharo's BlockClosure>>valueWithin:onTimeout: does something disable cannot
express: it signals the semaphore directly to retire a watchdog whose job is
already done, leaving the Delay itself alone.  That needs the accessor.
"
Extension { #name : 'Delay' }

{ #category : '*Concurrency' }
Delay >> delaySemaphore [
	"The semaphore this Delay's waiter is suspended on.

	 Signalling it wakes that waiter without cancelling the delay, which is
	 what a watchdog wants on the path where the thing it was watching
	 finished in time."
	^delaySemaphore
]

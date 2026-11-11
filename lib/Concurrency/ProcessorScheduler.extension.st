"
Asking the system about the workers underneath it.

Processor>>activeProcess reads one instance variable, and one variable
cannot answer a question that now has a different answer per thread.  The
variable stays -- a snapshot carries it, and a freshly loaded image has
nothing else to say -- but these ask the VM, which asks the caller.
"
Extension { #name : 'ProcessorScheduler' }

{ #category : '*Concurrency' }
ProcessorScheduler >> activeProcess [
	"The process THIS worker is running.  Falls back to the instance
	 variable, which is what a single-threaded image has always used and
	 what a snapshot carries."
	<primitive: 241>
	^activeProcess
]

{ #category : '*Concurrency' }
ProcessorScheduler >> activeWorkerIndex [
	"Which worker is asking, counting from zero.  Answers 0 when nothing
	 is running in a worker pool at all, which is the honest answer for a
	 single-threaded image rather than an error."
	<primitive: 243>
	^0
]

{ #category : '*Concurrency' }
ProcessorScheduler >> workerCount [
	"How many native threads are running Smalltalk.  One, if no pool has
	 been started."
	<primitive: 244>
	^1
]

{ #category : '*Concurrency' }
ProcessorScheduler >> primRemoveReadyProcess: aProcess [
	"Take aProcess off the ready list it is waiting on, and answer whether
	 it was there.  Fails, and answers false, if there is no VM support."
	<primitive: 252>
	^false
]

{ #category : '*Concurrency' }
ProcessorScheduler >> primFirstReadyProcessAt: aPriority [
	"The first process waiting for the processor at that priority, or nil."
	<primitive: 253>
	^nil
]

{ #category : '*Concurrency' }
ProcessorScheduler >> remove: aProcess ifAbsent: aBlock [
	"Remove aProcess from the list on which it is waiting for the
	 processor.  If it is not waiting, evaluate aBlock.

	 The 1983 version walked quiescentProcessLists from here, field by
	 field, with no lock and no idea that another worker might be walking
	 the same chain.  Asking the VM does the same job under the same lock
	 every other list operation takes -- and it is what lets the VM keep
	 the ready processes somewhere other than that one array, which is the
	 prerequisite for ever splitting them per worker."
	(self primRemoveReadyProcess: aProcess) ifFalse: [^aBlock value].
	^aProcess
]

{ #category : '*Concurrency' }
ProcessorScheduler >> suspendFirstAt: aPriority ifNone: noneBlock [
	"Suspend the first Process waiting to run at aPriority.  If none is
	 waiting, evaluate noneBlock."
	| first |
	first := self primFirstReadyProcessAt: aPriority.
	first isNil ifTrue: [^noneBlock value].
	^first suspend
]

{ #category : '*Concurrency' }
ProcessorScheduler >> forkParallel: aBlock [
	"Run aBlock as its own Process and answer a Promise for its value.

	 fork answers nothing and swallows what the block raises, which is
	 tolerable when a green process is a background chore and not when it
	 is a share of a computation somebody is waiting on.  This answers a
	 Promise instead, so the value comes back and so does the failure --
	 in the process that asks for it, which is the one able to act.

	 Where it RUNS is the scheduler's business.  This says the work may
	 proceed independently, not which native thread takes it.

	 The ensure: is not belt and braces.  `on: Error do:' settles the promise
	 for a task that RAISES; a task that is TERMINATED raises nothing, and
	 Bugs4 PROC-3 is what that cost: the promise stayed pending for ever, and
	 Promise>>value -- and parallelCollect:, which asks every promise -- waited
	 on a process that no longer existed, with no timeout and no diagnostic.
	 terminate runs the unwind blocks of the process it ends, so an ensure:
	 here is reached on that path as well as on a non-local return out of the
	 block, and breakIfUnsettled: says nothing when the two lines above have
	 already had their say.

	 An error the block does not handle is also REPORTED, where it is
	 raised, as fork's is (Bugs07 X-29).  It was only kept in the promise,
	 and a promise nobody asks for keeps it for ever: `Processor
	 forkParallel: [1/0]' said nothing at all where `[1/0] fork' printed
	 ZeroDivide, and a share of a computation that failed that way left
	 whoever was counting the shares waiting, with nothing to say why.
	 Nothing here can know whether the promise will be asked for, so the
	 report is made either way; an asker that then handles the error has
	 had it reported once already.  forkParallel:reportingErrors: with
	 false is for an asker that will always ask, as parallelCollect: does."
	^self forkParallel: aBlock reportingErrors: true
]

{ #category : '*Concurrency' }
ProcessorScheduler >> forkParallel: aBlock reportingErrors: aBoolean [
	"Run aBlock as its own Process and answer a Promise for its value,
	 reporting what it raises and does not handle when aBoolean is true.

	 The report is the one an unhandled error gets, made by passing the
	 error on from the handler that caught it, once the promise holds it:
	 there is no handler further out on a process this has just made, so
	 it reaches Error>>defaultAction as it would have with no handler here
	 at all.  The line goes to the standard error, the notifier opens
	 where there is a screen for it, and the unwinding runs every ensure:
	 down to this one's.  That is something torn down, so by fork's rule
	 the process then ENDS rather than carrying on from the failed send --
	 as it must here, because the promise already holds the failure, and a
	 block that went on would only have a second answer that a settled
	 promise refuses.  The promise is settled first, so an asker is never
	 kept waiting by the report.  (An `on: UnhandledError do:' inside the
	 block never sees the error, now or before: the `on: Error do:' here
	 takes it first, and a handler runs where its own on:do: stands, out
	 of reach of the handlers inside the block.)

	 With false, the error goes to the asker alone: the promise holds it
	 and the process ends, and nothing is said unless the asker says it."
	| promise |
	promise := Promise new.
	[[[promise fulfill: aBlock value]
		on: Error
		do: [:e |
			promise signalError: e.
			aBoolean ifTrue: [e pass]]]
			ensure: [promise breakIfUnsettled: (Error new messageText:
				'the process computing this Promise ended without settling it')]] fork.
	^promise
]

{ #category : '*Concurrency' }
ProcessorScheduler >> parallelCollect: aCollection do: aBlock [
	"Evaluate aBlock for each element, possibly at the same time, and
	 answer the results in order.  Every promise is asked for, so a
	 failure anywhere arrives here rather than being lost -- which is why
	 the tasks do not report their errors themselves: the first failure in
	 order is raised here, and reported here if nothing here handles it."
	| promises |
	promises := aCollection collect: [:each |
		self forkParallel: [aBlock value: each] reportingErrors: false].
	^promises collect: [:each | each value]
]

{ #category : '*Concurrency' }
ProcessorScheduler >> activePriority [
	"The priority of the process THIS worker is running.

	 1983 read the activeProcess instance variable, which several workers
	 running processes leave holding whichever switched last.  Every fork
	 asks this for the new process's priority, so a process forked on one
	 worker took the priority of a process on another -- the timing
	 process's, sometimes -- and ran ahead of everything it should have
	 waited behind."
	^self activeProcess priority
]

{ #category : '*Concurrency' }
ProcessorScheduler >> terminateActive [
	"Terminate the process THIS worker is running.

	 1983 terminated the activeProcess instance variable's process, which
	 on cores is whichever switched last, on any worker.  Processor yield
	 forks a helper that ends by sending this, so with twenty-four workers
	 yielding at once the helpers were terminating each other's callers:
	 a Delay waited on twice, a mutual-exclusion Semaphore signalled by a
	 process that had never taken it, and a worker's run ending inside
	 somebody else's yield.  Sixteen workers never showed it and
	 twenty-four always did."
	self activeProcess terminate
]

{ #category : '*Concurrency' }
ProcessorScheduler >> yield [
	"Give other Processes at my priority a chance to run.

	 Primitive 167, this system's own implementation of Squeak's number:
	 the VM parks the active process at the end of its ready list and
	 switches to whatever is ready, or does nothing when nothing is.
	 1983's body -- fork a helper that signals a semaphore, then wait on it
	 -- is kept as the fallback.  It creates a process per call whose block
	 context is shared between the caller and the helper, and on many
	 workers that sharing was the last fault left after every semaphore
	 race had been closed: thirty-one workers yielding in a loop failed
	 one run in two.  The primitive shares nothing."
	<primitive: 167>
	| semaphore |
	semaphore := Semaphore new.
	[semaphore signal] fork.
	semaphore wait
]

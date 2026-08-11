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
	 proceed independently, not which native thread takes it."
	| promise |
	promise := Promise new.
	[[promise fulfill: aBlock value]
		on: Error
		do: [:e | promise signalError: e]] fork.
	^promise
]

{ #category : '*Concurrency' }
ProcessorScheduler >> parallelCollect: aCollection do: aBlock [
	"Evaluate aBlock for each element, possibly at the same time, and
	 answer the results in order.  Every promise is asked for, so a
	 failure anywhere arrives here rather than being lost."
	| promises |
	promises := aCollection collect: [:each |
		self forkParallel: [aBlock value: each]].
	^promises collect: [:each | each value]
]

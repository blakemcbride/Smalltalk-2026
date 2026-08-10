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

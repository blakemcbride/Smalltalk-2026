"
Scheduling a closure.

BlockContext has had fork and newProcess since 1983.  BlockClosure is ours
and had neither, so in the closures dialect -- which is every profile that
loads lib/ -- `[...] fork' was a doesNotUnderstand.  Nothing noticed
because nothing in the library forked; Processor>>forkParallel: is the
first caller, and it found the hole immediately.

These are the 1983 methods, transcribed.  Process forContext:priority:
wants a block to run and a priority to run it at, and both classes can
supply those, so there is no new mechanism here -- only a class that was
never given the protocol its predecessor had.
"
Extension { #name : 'BlockClosure' }

{ #category : '*Concurrency' }
BlockClosure >> newProcess [
	"Answer a new Process that will run this closure.  It is not
	 scheduled; send it resume."
	^Process
		forContext:
			[self value.
			Processor terminateActive] asContext
		priority: Processor activePriority
]

{ #category : '*Concurrency' }
BlockClosure >> newProcessWith: anArray [
	"As newProcess, binding the closure's arguments to anArray."
	^Process
		forContext:
			[self valueWithArguments: anArray.
			Processor terminateActive] asContext
		priority: Processor activePriority
]

{ #category : '*Concurrency' }
BlockClosure >> fork [
	"Create and schedule a Process running this closure."
	^self newProcess resume
]

{ #category : '*Concurrency' }
BlockClosure >> forkAt: priority [
	"Create and schedule a Process running this closure at the given
	 priority."
	| forked |
	forked := self newProcess.
	forked priority: priority.
	^forked resume
]

{ #category : '*Concurrency' }
BlockClosure >> asContext [
	"A context for this closure that has not been entered, whose sender is
	 nil, and which Process>>forContext: can resume.

	 1983 did not need this: a BlockContext already IS a context, so
	 newProcess handed the block straight to the Process.  A BlockClosure
	 has three fields where a context has six, so doing the same thing
	 reads off the end of the object -- which is what happened, and what
	 the address sanitizer named, the first time anything forked."
	<primitive: 207>
	^self error: 'cannot make a context from this block'
]

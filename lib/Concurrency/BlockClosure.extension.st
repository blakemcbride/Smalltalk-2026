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

{ #category : '*Concurrency' }
BlockClosure >> on: anExceptionClass fork: handlerBlock [
	"Evaluate the receiver.  If anExceptionClass is signalled, fork a green
	 process to run handlerBlock and answer nil at once, without waiting
	 for it.

	 This is our version, not Pharo's, and the difference is worth stating.
	 Pharo builds this out of runUntilErrorOrReturnFrom: and context
	 surgery: it detaches the SIGNALLING context and hands that to the new
	 process, so a handler can still send ex return: or ex resume: and have
	 it mean something to the computation that raised.  doc/PLAN-PHARO.md's
	 Phase E declined that machinery on purpose, in favour of ANSI and
	 early-Squeak semantics.

	 So here the protected block is unwound as an ordinary on:do: would
	 unwind it, and only the handler is forked.  What that buys is what
	 nearly every use of on:fork: actually wants -- the raising process
	 does not block on the handler -- and what it costs is ex return: and
	 ex resume: meaning anything afterwards.  A handler that needs to
	 resume the signalling process wants on:do:, and should say so.

	 Written on fork and newProcess, which exist now, and on nothing else."
	^self on: anExceptionClass do: [:ex |
		[handlerBlock numArgs = 0
			ifTrue: [handlerBlock value]
			ifFalse: [handlerBlock value: ex]] fork.
		nil]
]

"The same for real closures, which answer numArgs from their own field."
Extension { #name : 'BlockClosure' }

{ #category : '*Kernel-Protocol' }
BlockClosure >> cull: anObject [
	^self numArgs = 0
		ifTrue: [self value]
		ifFalse: [self value: anObject]
]

{ #category : '*Kernel-Protocol' }
BlockClosure >> cull: first cull: second [
	self numArgs = 0 ifTrue: [^self value].
	self numArgs = 1 ifTrue: [^self value: first].
	^self value: first value: second
]

{ #category : '*Kernel-Protocol' }
BlockClosure >> isBlock [
	^true
]

{ #category : '*Kernel-Protocol' }
BlockClosure >> repeat [
	"Evaluate me forever.

	 The only ways out are a non-local return, an exception, or the process
	 being interrupted -- which is exactly why it is the shape every timeout
	 test uses to build something that will not stop on its own."
	[self value. true] whileTrue
]

{ #category : '*Kernel-Protocol' }
BlockClosure >> deepCopy [
	"Myself.  A closure holds its outerContext, which holds its sender,
	 its method and its receiver, and from there every object in the image
	 once per path: `SortedCollection new deepCopy' -- the one Blue Book
	 collection whose state includes a block -- ran for 47 seconds, took
	 788 MB and failed with RecursionDepthExceeded, and `[:a :b | a > b]
	 deepCopy' did not answer at all (Bugs3 B47).  A deep copy of a block
	 that shared nothing would not be a copy of the block: what it closes
	 over is what it is.  Pharo's rule, and the same answer ContextPart,
	 Behavior, CompiledMethod, Process and Semaphore give in this package."
	^self
]

{ #category : '*Kernel-Protocol' }
BlockClosure >> valueWithExit [
	"Evaluate me with one argument, a block that leaves me early when it
	 is sent value: -- `[:exit | ... exit value: x ... ] valueWithExit'.
	 The value I answer is what the exit was given, or the value of my
	 last statement when the exit was never taken.

	 The way out of a loop without leaving the method, which the manual's
	 control-flow chapter shows and the image did not have (Bugs3 B64).
	 The exit block's non-local return is what does the work, and it is
	 legal here because valueWithExit is still running when the exit is
	 sent: the block's home context is this method's.  Pharo's answers
	 nil regardless; answering the exit's argument makes `exit value:
	 each' a way of saying which element ended the loop."
	^self value: [:answer | ^answer]
]

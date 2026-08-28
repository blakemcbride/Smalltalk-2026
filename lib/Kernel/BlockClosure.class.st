"
A block that is a closure rather than a context.

A Blue Book BlockContext is the closure and the activation record at once,
which is why blocks there are not re-entrant: activating one writes the
instruction pointer, the stack pointer and the caller into the very object
somebody is holding, and its arguments live in the home method's frame
rather than its own.  A BlockClosure holds only what makes it that block --
where its code starts, how many arguments it takes, and the values it
captured -- and every activation gets a context of its own.

The shape is Squeak's, deliberately, because the primitive numbers below
are the ones Pharo source declares and ported methods have to keep working
unedited:

	outerContext	the context this closure was made in
	startpc		the first bytecode of the body, one-relative
	numArgs		how many arguments value: expects
	(indexed)	the copied values, one per captured variable

Not here yet, and deliberately named rather than left to be discovered as
a doesNotUnderstand: **fork, forkAt:, newProcess and newProcessWith:**.
BlockContext answers all four, and it can, because a BlockContext already
IS a context and Process forContext: takes one.  A closure is not, so
making a process out of one needs a way to build its activation without
running it -- a primitive, and one that belongs with the scheduler work in
Phase H rather than bolted on here.
"
Class {
	#name : 'BlockClosure',
	#superclass : 'Object',
	#type : 'variable',
	#instVars : [ 'outerContext', 'startpc', 'numArgs' ],
	#category : 'Kernel-Closures'
}

{ #category : 'accessing' }
BlockClosure >> numArgs [
	^numArgs
]

{ #category : 'accessing' }
BlockClosure >> outerContext [
	^outerContext
]

{ #category : 'accessing' }
BlockClosure >> startpc [
	^startpc
]

{ #category : 'evaluating' }
BlockClosure >> value [
	<primitive: 201>
	^self numArgsError: 0
]

{ #category : 'evaluating' }
BlockClosure >> value: a [
	<primitive: 202>
	^self numArgsError: 1
]

{ #category : 'evaluating' }
BlockClosure >> value: a value: b [
	<primitive: 203>
	^self numArgsError: 2
]

{ #category : 'evaluating' }
BlockClosure >> value: a value: b value: c [
	<primitive: 204>
	^self numArgsError: 3
]

{ #category : 'evaluating' }
BlockClosure >> value: a value: b value: c value: d [
	<primitive: 205>
	^self numArgsError: 4
]

{ #category : 'evaluating' }
BlockClosure >> valueWithArguments: anArray [
	"Evaluate me with the elements of anArray as my arguments.

	 The primitive fails for two reasons, and only one of them is the
	 count.  The elements are spread onto the CALLING method's stack, in a
	 frame the compiler sized for that method's own pushes (Bugs3 B1); a
	 fifteen-argument block overflowed a frame of 23 slots and the run was
	 over.  It fails now instead, and the two reasons are told apart here."
	<primitive: 206>
	anArray isArray ifFalse: [^self error: 'valueWithArguments: takes an Array'].
	anArray size = self numArgs ifFalse: [^self numArgsError: anArray size].
	^self error: 'valueWithArguments: cannot spread ' , anArray size printString
		, ' arguments: the calling method''s frame has no room for them'
]

{ #category : 'evaluating' }
BlockClosure >> valueNoContextSwitch [
	"In this VM a process switch is decided once per bytecode rather than
	at a send, so there is nothing to suppress and this is value."
	<primitive: 221>
	^self numArgsError: 0
]

{ #category : 'evaluating' }
BlockClosure >> valueNoContextSwitch: a [
	<primitive: 222>
	^self numArgsError: 1
]

{ #category : 'private' }
BlockClosure >> numArgsError: n [
	^self error: 'This block accepts a different number of arguments'
]

{ #category : 'controlling' }
BlockClosure >> whileTrue: aBlock [
	^[self value] whileTrue: [aBlock value]
]

{ #category : 'controlling' }
BlockClosure >> whileFalse: aBlock [
	^[self value] whileFalse: [aBlock value]
]

{ #category : 'controlling' }
BlockClosure >> repeat [
	[true] whileTrue: [self value]
]

{ #category : 'controlling' }
BlockClosure >> whileTrue [
	^[self value] whileTrue: []
]

{ #category : 'controlling' }
BlockClosure >> whileFalse [
	^[self value] whileFalse: []
]

{ #category : 'accessing' }
BlockClosure >> fixTemps [
	"Nothing to fix.

	 A BlockContext shares its temporaries with the method it was written
	 in, so a block kept past that method's return -- a SortedCollection's
	 sortBlock is the case that matters -- had to copy the home frame or
	 read whatever the frame became.  A closure copied its values when it
	 was made, which is the whole difference between the two, so the
	 answer is the receiver unchanged.

	 It is here because 1983 library code SENDS it: SortedCollection>>
	 sortBlock: does, and without this every asSortedCollection: in the
	 image fails the moment the argument is a closure."
	^self
]

{ #category : 'accessing' }
BlockClosure >> home [
	"The method context this closure was ultimately written in.

	 outerContext is one step out, which for a closure inside a closure is
	 another closure's activation; home walks the rest of the way."
	^outerContext ifNotNil: [:c | c home]
]

{ #category : 'accessing' }
BlockClosure >> receiver [
	^outerContext ifNotNil: [:c | c receiver]
]

{ #category : 'accessing' }
BlockClosure >> method [
	^outerContext ifNotNil: [:c | c method]
]

{ #category : 'private' }
BlockClosure >> valueError [
	^self error: 'wrong number of arguments'
]

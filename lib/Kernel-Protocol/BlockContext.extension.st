"
numArgs and cull:.

numArgs first, because BlockContext does not have it.  The field is there --
nargs, right in the class definition -- but 1983 gave it no accessor, since
the only thing that ever needed it was the interpreter, which reads the
field directly.  BlockClosure answers numArgs, so every piece of protocol
written against ""a block"" broke the moment it met a Blue Book one; cull:
below is the first of many.

cull: -- evaluate with as many of the offered arguments as the block will
take.

It exists because two spellings of the same idea are both common and neither
is wrong: [:x | x foo] and [self foo] are both reasonable answers to
ifNotNil:, and a library that accepted only one would be picking a side for
no reason.  numArgs is what makes the choice possible.
"
Extension { #name : 'BlockContext' }

{ #category : '*Kernel-Protocol' }
BlockContext >> numArgs [
	"How many arguments value: expects.  The field has been here since
	 1983; only the accessor is new."
	^nargs
]

{ #category : '*Kernel-Protocol' }
BlockContext >> cull: anObject [
	^self numArgs = 0
		ifTrue: [self value]
		ifFalse: [self value: anObject]
]

{ #category : '*Kernel-Protocol' }
BlockContext >> cull: first cull: second [
	self numArgs = 0 ifTrue: [^self value].
	self numArgs = 1 ifTrue: [^self value: first].
	^self value: first value: second
]

{ #category : '*Kernel-Protocol' }
BlockContext >> isBlock [
	^true
]

{ #category : '*Kernel-Protocol' }
BlockContext >> value: arg1 value: arg2 value: arg3 value: arg4 [
	"Evaluate me on four arguments.

	 1983 stops at three, and the compiler does not: it accepts a
	 four-argument block and then there is no way to call one except
	 valueWithArguments:, which is a doesNotUnderstand waiting for the
	 first caller who writes the obvious thing.  Primitive 81 takes its
	 argument count from the send, so the fourth costs nothing but the
	 method."

	<primitive: 81>
	^self valueWithArguments:
		(Array with: arg1 with: arg2 with: arg3 with: arg4)
]

{ #category : '*Kernel-Protocol' }
BlockContext >> valueWithArguments: anArray [
	"Evaluate me with the elements of anArray as my arguments.

	 1983's fallback reads the primitive's failure as `already being
	 evaluated' when the count matches, and the count is not the only thing
	 the primitive checks any more (Bugs3 B1): the elements are spread onto
	 the CALLING method's stack, in a frame the compiler sized for that
	 method's own pushes, and an Array that does not fit fails the primitive
	 rather than overflowing the frame.  So the two reasons are told apart
	 here, and the one that is new is named."
	<primitive: 82>
	anArray isArray ifFalse: [^self error: 'valueWithArguments: takes an Array'].
	nargs = anArray size
		ifFalse: [^self error: 'The block needs more or fewer arguments defined'].
	^self error: 'valueWithArguments: cannot spread ' , anArray size printString
		, ' arguments: the calling method''s frame has no room for them'
]

{ #category : '*Kernel-Protocol' }
BlockContext >> deepCopy [
	"Myself, for the reason BlockClosure>>deepCopy gives: a Blue Book
	 block is its own activation record, and its home holds the sender
	 chain and through it the image (Bugs3 B47).  ContextPart answers the
	 same; this is here so that the class which IS the block in the
	 bluebook dialect says so in its own name."
	^self
]

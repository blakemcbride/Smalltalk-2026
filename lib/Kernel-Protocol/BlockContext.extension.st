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

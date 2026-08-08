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

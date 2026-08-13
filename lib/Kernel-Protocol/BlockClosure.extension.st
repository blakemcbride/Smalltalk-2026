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

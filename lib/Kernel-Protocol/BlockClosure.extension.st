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

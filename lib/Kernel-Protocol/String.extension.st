"A String displays as its characters; only printString adds the quotes."
Extension { #name : 'String' }

{ #category : '*Kernel-Protocol' }
String >> displayString [
	^self
]

{ #category : '*Kernel-Protocol' }
String >> isString [
	^true
]

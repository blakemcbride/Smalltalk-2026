Extension { #name : 'Symbol' }

{ #category : '*Kernel-Protocol' }
Symbol >> displayString [
	^self asString
]

{ #category : '*Kernel-Protocol' }
Symbol >> isSymbol [
	^true
]

Extension { #name : 'Character' }

{ #category : '*Kernel-Protocol' }
Character >> displayString [
	^String with: self
]

{ #category : '*Kernel-Protocol' }
Character >> isCharacter [
	^true
]

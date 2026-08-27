Extension { #name : 'Character' }

{ #category : '*Kernel-Protocol' }
Character >> displayString [
	^String with: self
]

{ #category : '*Kernel-Protocol' }
Character >> isCharacter [
	^true
]

{ #category : '*Kernel-Protocol' }
Character >> value [
	"My code point.

	 Pharo has this and so does Squeak, and without it a Character inherits
	 Object>>value -- see the note there -- which answers the RECEIVER.  So
	 `$A value' answered $A where every other dialect answers 65, and it
	 answered it silently: ported code that reads a code point this way gets
	 a Character where it expected an Integer and finds out later, in
	 arithmetic or in a comparison, rather than at the send.

	 asInteger and asciiValue answer the same number and always did.  This
	 is the third spelling because it is the one ported source uses."
	^self asInteger
]

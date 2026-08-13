Extension { #name : 'Number' }

{ #category : '*Kernel-Protocol' }
Number >> isNumber [
	^true
]

{ #category : '*Kernel-Protocol' }
Number >> sign: aNumber [
	"My magnitude, carrying aNumber's sign.

	 Duration class>>readFrom: is the shape that wants it: the sign is read
	 once from the front of '-D:HH:MM:SS' and then applied to each field as
	 it is parsed, so the field parser must not have to know where the minus
	 was.  Zero counts as positive, which is what makes `0 sign: -1' answer
	 0 rather than a negative zero nobody asked for."
	^aNumber negative
		ifTrue: [self abs negated]
		ifFalse: [self abs]
]

{ #category : '*Kernel-Protocol' }
Number >> ** anExponent [
	"Raised to that power.  Pharo's spelling of raisedTo:."
	^self raisedTo: anExponent
]

{ #category : '*Kernel-Protocol' }
Number >> closeTo: aNumber [
	"Within a small absolute distance of that number.

	 The default precision is Pharo's: 0.0001, absolute rather than
	 relative.  Absolute is the wrong choice for very large or very small
	 magnitudes and it is what ported tests were written against, so it is
	 what they get; closeTo:precision: is there for anything that needs to
	 say otherwise."
	^self closeTo: aNumber precision: 0.0001
]

{ #category : '*Kernel-Protocol' }
Number >> closeTo: aNumber precision: aPrecision [
	^(self - aNumber) abs < aPrecision
]

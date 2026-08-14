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

{ #category : '*Kernel-Protocol' }
Number class >> readFrom: aStream [
	"Read a number, and get the last digit right.

	 1983 builds the fractional part as

	     value asFloat + (fraction asFloat / (radix raisedTo: digits))

	 which rounds twice -- once in the division and once in the addition --
	 so it is not a correctly-rounded decimal-to-double conversion.  Six of
	 forty test values failed `x printString asNumber = x' because of it,
	 and they failed only on this side: the same strings survive the
	 compiler's own lexer.  Once Float>>printString was fixed to emit enough
	 digits, this was the remaining half of a Float being unable to survive
	 its own printed form.

	 The fix is to do the arithmetic EXACTLY and round once at the end.  All
	 the digits go into an Integer, the decimal point becomes a division by
	 a power of the radix, and the result is a Fraction -- which this system
	 converts to a Float with correct rounding, checked before relying on
	 it.  No primitive is needed and none is spent.

	 Everything else here is 1983's, kept deliberately: the <radix>r<digits>
	 form, the <mantissa>e<exponent> form, the un-gobbling of a period that
	 turns out to end a sentence rather than begin a fraction, and answering
	 an Integer when there is no fraction at all."
	| value radix fraction digits start negative sawPoint |
	radix := 10.
	sawPoint := false.
	value := Integer readFrom: aStream.
	negative := value negative.
	(aStream peekFor: $r)
		ifTrue: [
			(radix := value) < 2 ifTrue: [^self error: 'INVALID RADIX'].
			value := Integer readFrom: aStream radix: radix.
			negative := value negative].
	(aStream peekFor: $.)
		ifTrue: [
			(aStream atEnd not
				and: [aStream peek digitValue between: 0 and: radix - 1])
				ifTrue: [
					start := aStream position.
					fraction := Integer readFrom: aStream radix: radix.
					digits := aStream position - start.
					"Exact: one Fraction, one rounding, at the end."
					value := negative
						ifTrue: [value - (fraction / (radix raisedTo: digits))]
						ifFalse: [value + (fraction / (radix raisedTo: digits))].
					"
					 NOT converted here.  An exponent may follow, and
					 applying it to a Float that has already been rounded
					 rounds twice again -- which is the whole fault this
					 method exists to remove.  The conversion happens once,
					 at the end, after everything exact has been done.
					"
					sawPoint := true]
				ifFalse: [
					aStream skip: -1.
					^value]].
	(aStream peekFor: $e)
		ifTrue: [
			value := value * (radix raisedTo: (Integer readFrom: aStream)).
			sawPoint := true].
	^sawPoint ifTrue: [value asFloat] ifFalse: [value]
]

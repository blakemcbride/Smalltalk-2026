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
	| value radix fraction digits start negative sawPoint answer |
	radix := 10.
	sawPoint := false.
	"
	 Leading separators are skipped, which 1983 does not do: it hands the
	 stream straight to Integer class>>readFrom:radix:, whose own comment
	 says it answers ZERO rather than an error when there are no digits.
	 So `Number readFrom: (ReadStream on: '  42 ')' answered 0 -- not an
	 error a caller could see, a plausible number.  A reader that stops at
	 the first non-digit still stops at the first non-digit; it just no
	 longer stops before it has started.
	"
	[aStream atEnd not and: [aStream peek isSeparator]] whileTrue: [aStream next].
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
	sawPoint ifFalse: [^value].
	"
	 The exponent is applied EXACTLY -- value is still an Integer or a
	 Fraction here -- so an exponent that no Float can hold is visible as
	 an overflow rather than as an answer.  1983 converted and carried on,
	 and `1e1000' was a literal that read as infinity with nothing said:
	 not an error, not a number anywhere near what was written, and it
	 reached the Scanner, so it was a LITERAL in a compiled method.
	 Underflow is left alone, because a decimal too small for a double
	 rounding to zero is what IEEE 754 says happens and what every reader
	 in every language does.
	"
	answer := value asFloat.
	answer isFinite ifFalse: [
		^self error: 'this number is too large for a Float: ' , value printString].
	^answer
]

{ #category : '*Kernel-Protocol' }
Number >> retry: aSymbol coercing: aNumber [
	"The arithmetic could not be done as written, so coerce whichever of us
	 is less general and try once more.

	 REWRITTEN BECAUSE THE ERROR RETURNS.  Error>>defaultAction reports,
	 unwinds and answers -- see Object>>at: in lib/Kernel-Exceptions for the
	 whole argument -- and 1983's last line here was a bare

		self error: 'coercion attempt failed'

	 with the method then falling off the end and answering self.  That was
	 not merely an odd answer: `3 + (nil foo)' never returned at all,
	 because the doesNotUnderstand resumed with nil, `nil generality' was
	 another doesNotUnderstand that resumed with nil, and `nil < nil' sent
	 retry:coercing: again.  Each turn of the loop printed two errors and
	 started the next one.

	 Two changes stop it.  The error is RETURNED, so the method ends where
	 it says it does.  And an argument that is not a Number is named as
	 such before its generality is ever asked for, which is the case that
	 actually arrives -- 1983 asks that question only for #=, and answers
	 false there, which stays."

	(aNumber isKindOf: Number) ifFalse: [
		aSymbol == #= ifTrue: [^false].
		^self error: 'a ' , aNumber class name , ' is not a Number, so '
			, self printString , ' ' , aSymbol , ' it has no answer'].
	self generality < aNumber generality
		ifTrue: [^(aNumber coerce: self) perform: aSymbol with: aNumber].
	self generality > aNumber generality
		ifTrue: [^self perform: aSymbol with: (self coerce: aNumber)].
	^self error: 'coercion attempt failed'
]

{ #category : '*Kernel-Protocol' }
Number >> log [
	"My base-ten logarithm.

	 1983 has Float>>log and nothing above it, so `100 log' was a
	 doesNotUnderstand for every Integer and Fraction -- which is most of
	 the receivers anybody writes it for."
	^self asFloat log
]

{ #category : '*Kernel-Protocol' }
Number >> log: aNumber [
	"My logarithm in that base."
	^self ln / aNumber ln
]

{ #category : '*Kernel-Protocol' }
Number >> ln [
	"My natural logarithm.  On Number so that an Integer or a Fraction
	 answers rather than not understanding."
	^self asFloat ln
]

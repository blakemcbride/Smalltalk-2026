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
	 an Integer when there is no fraction at all.

	 A String is taken as well as a stream, because `Number readFrom: '42''
	 is what ported code writes and Pharo answers it; 1983 sent atEnd to
	 the String (Bugs3 B51)."
	| value radix fraction digits start negative sawPoint answer |
	aStream isString ifTrue: [^self readFrom: (ReadStream on: aStream)].
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
			"
			 An r with no digit of the radix after it is not a radix
			 marker: it is a letter that happens to follow a number, as
			 in `12abc', and the number is the one before it.  1983 read
			 the empty digit run as 0 -- `'16r' asNumber' was 0 while
			 `'16r' asInteger' was 16 (Bugs3 B51) -- so the r is given
			 back here, as the period is a few lines down when it turns
			 out not to begin a fraction.
			"
			(aStream atEnd
				or: [(aStream peek digitValue between: 0 and: value - 1) not
					and: [aStream peek ~= $-]])
				ifTrue: [aStream skip: -1. ^value].
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
	 false there, which stays.

	 AND A COMPARISON WITH A FLOAT IS DECIDED EXACTLY (Bugs3 B36).  1983
	 coerces the less general side, and for a relational selector that
	 means the Integer or Fraction is ROUNDED to a double before anything
	 is compared.  So 9007199254740993 = 9007199254740992.0 was true,
	 because 2^53 + 1 rounds to 2^53; SmallInteger maxVal was equal to its
	 own asFloat and not below it; `(1/3) = (1/3) asFloat' was true; every
	 Integer above 1.8e308 was equal to Float infinity, to each other's
	 asFloat, and not less than infinity; = was not transitive; and two
	 equal numbers could hash differently, because one hashed as itself
	 and the other as the integer it had rounded to.  Not a corner: a
	 Set holding 2^53, 2^53 + 1 and (2^53 + 1) asFloat had two elements.

	 So a relational selector with a Float on one side and an Integer or
	 Fraction on the other is handed to compare:exactlyWith:, which
	 compares the Float's EXACT value.  With one exception, for speed: a
	 SmallInteger below 2^53 in magnitude converts to a double without
	 loss, so coercing it -- which is what happens below -- is already
	 exact, and it is the case that arrives ten thousand times for every
	 other: `aFloat < 0', `aFloat = 1'.  That case costs what it always
	 cost.  Arithmetic selectors are not touched; `3 + 0.5' still coerces,
	 because a Float sum is what that means."

	(aNumber isKindOf: Number) ifFalse: [
		aSymbol == #= ifTrue: [^false].
		^self error: 'a ' , aNumber class name , ' is not a Number, so '
			, self printString , ' ' , aSymbol , ' it has no answer'].
	((self isFloat or: [aNumber isFloat])
	 and: [aSymbol == #= or: [aSymbol == #< or: [aSymbol == #> or: [
		aSymbol == #<= or: [aSymbol == #>= or: [aSymbol == #~=]]]]]])
		ifTrue: [ | exact |
			exact := self isFloat ifTrue: [aNumber] ifFalse: [self].
			(exact class == SmallInteger
			 and: [exact between: -9007199254740992 and: 9007199254740992])
				ifFalse: [^self compare: aSymbol exactlyWith: aNumber]].
	self generality < aNumber generality
		ifTrue: [^(aNumber coerce: self) perform: aSymbol with: aNumber].
	self generality > aNumber generality
		ifTrue: [^self perform: aSymbol with: (self coerce: aNumber)].
	^self error: 'coercion attempt failed'
]

{ #category : '*Kernel-Protocol' }
Number >> compare: aSymbol exactlyWith: aNumber [
	"Decide a relational selector between me and aNumber on exact values,
	 where one of us is a Float and the other an Integer or a Fraction.

	 Each side is asked what it stands for in an exact comparison
	 (asExactNumberAgainst:): an Integer or a Fraction is itself, a finite
	 Float is its asExactFraction, an infinity is a stand-in number of its
	 sign beyond the other side, and a NaN is nil.  A NaN is equal to
	 nothing and unordered against everything, so every selector answers
	 false for it except ~=, which is the one that says `not equal'.  The
	 two exact numbers are then compared by their own arithmetic, which
	 coerces an Integer to a Fraction and never rounds.

	 See retry:coercing: for why this exists (Bugs3 B36) and for the one
	 case -- a SmallInteger a double can hold -- that does not come here."
	| a b |
	a := self asExactNumberAgainst: aNumber.
	b := aNumber asExactNumberAgainst: self.
	(a isNil or: [b isNil]) ifTrue: [^aSymbol == #~=].
	^a perform: aSymbol with: b
]

{ #category : '*Kernel-Protocol' }
Number >> asExactNumberAgainst: aNumber [
	"What I stand for in an exact comparison with aNumber: myself.  Float
	 overrides this; see compare:exactlyWith:."
	^self
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
	"My logarithm in that base.

	 `self ln / aNumber ln' is what 1983 does and what every dialect
	 since does, and it is a quotient of two rounded numbers: `1000 log:
	 10' answered 2.9999999999999996, and `(2 raisedTo: 1100) log: 2'
	 answered inf before Integer>>ln could be asked (Bugs3 B39).  The
	 question is nearly always asked of an integer power of an integer
	 base -- how many digits, how many bits -- so that case is checked
	 exactly: when both are Integers and the base raised to the nearest
	 whole number IS the receiver, that whole number is the answer and
	 the Float has no business being off by an ulp.  raisedTo: is exact
	 by repeated squaring, and the candidate is bounded by the receiver's
	 bit count before it is raised, so the check can never build a number
	 wildly larger than the receiver."
	| quotient power |
	quotient := self ln / aNumber ln.
	(self isInteger and: [aNumber isInteger and: [aNumber > 1 and: [self > 0]]])
		ifFalse: [^quotient].
	power := quotient rounded.
	(power >= 0 and: [power <= self highBit and: [(aNumber raisedTo: power) = self]])
		ifTrue: [^power asFloat].
	^quotient
]

{ #category : '*Kernel-Protocol' }
Number >> ln [
	"My natural logarithm.  On Number so that an Integer or a Fraction
	 answers rather than not understanding."
	^self asFloat ln
]

{ #category : '*Kernel-Protocol' }
Number >> raisedTo: aNumber [
	"Me raised to that power.

	 1983's body, with one arm changed: a non-integer exponent goes through
	 the machine's pow rather than through `(aNumber * self ln) exp'.  See
	 Float>>raisedTo: for why -- the short version is that two
	 transcendental functions lose digits where one does not, and
	 `10 raisedTo: 2.0' answered 100.00000000000004.

	 An INTEGER exponent still goes to raisedToInteger:, which is exact by
	 repeated squaring and keeps the receiver's kind: `2 raisedTo: 10' is
	 the Integer 1024 and `(1/2) raisedTo: 2' is the Fraction 1/4.  Coercing
	 those to Float would be a wrong answer, not a rounder one."
	aNumber isInteger ifTrue: [^self raisedToInteger: aNumber].
	^self asFloat primRaisedTo: aNumber asFloat
]

{ #category : '*Kernel-Protocol' }
Number >> isZero [
	"Whether I am zero.

	 The manual's Number tables list it (12-numbers.tex and appendix B)
	 and nothing implemented it, so `3 isZero' was a doesNotUnderstand of
	 a method the documentation promised (Bugs4 DOCS-4).  Pharo's body is
	 this line.  Float and Fraction answer it directly, each for its own
	 reason; see there."
	^self = 0
]

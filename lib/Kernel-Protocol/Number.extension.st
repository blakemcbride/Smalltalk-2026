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
	| value radix fraction digits start negative signed sawPoint answer exponent bits scale exponentFollows |
	aStream isString ifTrue: [^self readFrom: (ReadStream on: aStream)].
	radix := 10.
	sawPoint := false.
	exponentFollows := false.
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
	"
	 The sign is read HERE, before the digits, and applied once at the
	 end.  It was taken from `value negative' after the integer part was
	 read, and -0 reads as 0: `'-0.5' asNumber' answered 0.5, and so did
	 every negative number between -1 and 0, and `x printString asNumber'
	 changed x's sign for all of them -- a form field reaching
	 RestDispatcher>>numberFrom: included (Bugs5 KERN-3).  Everything
	 below works on the magnitude.
	"
	scale := 0.
	negative := aStream peekFor: $-.
	signed := [:magnitude | negative ifTrue: [magnitude negated] ifFalse: [magnitude]].
	value := Integer readFrom: aStream.
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
				ifTrue: [aStream skip: -1. ^signed value: value].
			"A radix from 2 to 36, as the compiler and printString: have
			 it: `'37r10' asNumber' was 37 and `'99rZZ' asNumber' 3500,
			 numbers the compiler refuses (Bugs6 KERNA-10)."
			(value between: 2 and: 36)
				ifFalse: [^self error: 'the radix must be from 2 to 36'].
			radix := value.
			"16r-0.8: the sign may come after the radix instead."
			(aStream peekFor: $-) ifTrue: [negative := negative not].
			value := Integer readFrom: aStream radix: radix].
	(aStream peekFor: $.)
		ifTrue: [
			(aStream atEnd not
				and: [aStream peek digitValue between: 0 and: radix - 1])
				ifTrue: [
					start := aStream position.
					fraction := Integer readFrom: aStream radix: radix.
					digits := aStream position - start.
					"
					 Exact: an Integer mantissa and a power of the radix,
					 one rounding at the end.  It was a Fraction, and a
					 Fraction reduces itself: gcd: on a 256-digit numerator
					 cost 300 ms of a REST worker for one form field before
					 any authentication (Bugs5 NET-2).  Nothing here needs
					 the ratio in lowest terms, only its Float.
					"
					value := (value * (radix raisedTo: digits)) + fraction.
					scale := digits negated.
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
					^signed value: value]].
	"
	 An e is an exponent marker only when an exponent follows it: a digit,
	 or a sign and then a digit.  Anything else and the e is given back
	 and the number is the one before it, as the r and the period above
	 are given back.  1983 took the e unconditionally and read the empty
	 exponent as 0, so `'12e' asNumber' was 12.0, `'5east' asNumber' 5.0
	 and `'1e+5' asNumber' 1.0 -- a Float where the text held an Integer,
	 and the stream left past a letter that belonged to whatever came next
	 (Bugs5 KERN-10).  The plus IS a sign here, as it is in the compiler's
	 grammar: `1e+5' compiled to 100000.0 and read as 1 (Bugs6 KERNA-10).
	"
	(aStream atEnd not and: [aStream peek = $e]) ifTrue: [
		start := aStream position.
		aStream next.
		(aStream atEnd not and: [aStream peek = $- or: [aStream peek = $+]])
			ifTrue: [aStream next].
		exponentFollows := aStream atEnd not and: [aStream peek isDigit].
		aStream position: start].
	(exponentFollows and: [aStream peekFor: $e])
		ifTrue: [ | exponentNegative |
			"
			 The size of the answer is estimated, in bits, BEFORE the power
			 is computed.  `value * (radix raisedTo: exponent)' with no
			 bound let `1e100000' spend 36 seconds on the power and 19 more
			 printing it into the error, and `1e999999999' never finish --
			 eleven bytes, and the HTTP client sends asNumber to a server's
			 status code (Bugs5 KERN-4).  A Float holds about 2^1024 down to
			 2^-1074, so an estimate past 1100 bits either way is decided
			 without the arithmetic: too large is the error below, too small
			 is the zero IEEE 754 rounds it to.  Everything nearer is still
			 done exactly, and its size is bounded by the length of the text.
			"
			exponentNegative := aStream peekFor: $-.
			exponentNegative ifFalse: [aStream peekFor: $+].
			exponent := Integer readFrom: aStream.
			exponentNegative ifTrue: [exponent := exponent negated].
			value = 0 ifFalse: [
				bits := value highBit + ((scale + exponent) * (radix ln / 2 ln)).
				bits > 1100 ifTrue: [
					^self error: 'this number is too large for a Float'].
				bits < -1100 ifTrue: [
					^negative ifTrue: [-0.0] ifFalse: [0.0]]].
			scale := scale + exponent.
			sawPoint := true].
	sawPoint ifFalse: [^signed value: value].
	"value * radix^scale, bounded now: the estimate above kept scale within
	 what the text and the Float range allow, and zero is zero whatever it
	 was scaled by."
	value = 0
		ifTrue: [answer := 0.0]
		ifFalse: [
			answer := scale >= 0
				ifTrue: [(value * (radix raisedTo: scale)) asFloat]
				ifFalse: [Fraction floatOfNumerator: value
							denominator: (radix raisedTo: scale negated)]].
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
	answer isFinite ifFalse: [
		"Not printed: hundreds of digits say less than the sentence."
		^self error: 'this number is too large for a Float'].
	"Times -1, not negated: `0.0 negated' is 0.0, and '-0.0' is -0.0."
	negative ifTrue: [answer := answer * -1].
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
	 those to Float would be a wrong answer, not a rounder one.

	 A positive receiver with no normal Float -- an Integer past 1e308, a
	 Fraction past it or under 2.2e-308 -- has its power of two taken out
	 first.  asFloat of such a receiver is an infinity or a zero, and pow
	 of that is an infinity or a zero whatever the exponent: `(2 raisedTo:
	 2000) raisedTo: 1/2' answered inf for 1.07e301, a number a Float
	 holds (Bugs5 KERN-13, the raisedTo: half of what Bugs2 B39 fixed for
	 ln and sqrt).  With me = m * 2^k, m within a factor of two of one,
	 my power is m^y * 2^(k*y), and k*y is done EXACTLY -- the exponent as
	 a Fraction -- so that its whole part goes to timesTwoPower: and only
	 its fractional part to pow.  `(aNumber * self ln) exp', the obvious
	 way, is off by hundreds of ulps here: ln of 2^2000 is 1386, and its
	 last bit is 2^-42 of the answer's exponent.

	 With m in [1, 2) and |k| near a thousand or more, an exponent past 64
	 either way puts the answer past 2^64000 or under its reciprocal --
	 an infinity or a zero, and which one is the sign of k times the sign
	 of the exponent -- so that is answered without building k*y, and n
	 is clamped where it cannot change the double it scales.  A NaN or
	 infinite exponent goes the obvious way, where it does no harm."
	| base k m y ky n |
	aNumber isInteger ifTrue: [^self raisedToInteger: aNumber].
	base := self asFloat.
	(self > 0 and: [base isFinite not or: [base < 2.2250738585072014e-308]])
		ifFalse: [^base primRaisedTo: aNumber asFloat].
	(aNumber isFloat and: [aNumber isFinite not])
		ifTrue: [^(aNumber * self ln) exp].
	self isInteger
		ifTrue: [
			k := self highBit - 1.
			m := Fraction floatOfNumerator: self denominator: (1 bitShift: k)]
		ifFalse: [
			k := self numerator highBit - self denominator highBit.
			m := k >= 0
				ifTrue: [Fraction floatOfNumerator: self numerator
							denominator: (self denominator bitShift: k)]
				ifFalse: [Fraction floatOfNumerator: (self numerator bitShift: k negated)
							denominator: self denominator].
			m < 1.0 ifTrue: [m := m * 2.0. k := k - 1]].
	aNumber abs > 64 ifTrue: [
		^(aNumber > 0) = (k > 0) ifTrue: [Float infinity] ifFalse: [0.0]].
	y := aNumber isFloat ifTrue: [aNumber asExactFraction] ifFalse: [aNumber].
	ky := k * y.
	n := ky floor.
	^((m primRaisedTo: aNumber asFloat) * (2.0 primRaisedTo: (ky - n) asFloat))
		timesTwoPower: ((n max: -1300) min: 1300)
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

{ #category : '*Kernel-Protocol' }
Number >> to: stop by: step do: aBlock [
	"Evaluate aBlock for each number from me to stop, counting by step.

	 1983's, refusing a step of zero, as Interval class>>from:to:by: has
	 since Bugs1 B14.  With a zero step the counter never moves and never
	 passes stop, so `1 to: 10 by: 0 do: [...]' -- or a step held in a
	 variable that happens to be 0, or 0.0, or sent by perform: -- ran the
	 block for ever, while `(1 to: 10 by: 0) do:' raised (Bugs5 KERN-7).
	 Neither compiler inlines to:by:do:, so this is every path."
	| nextValue |
	step = 0 ifTrue: [^self error: 'to:by:do: cannot count by a step of zero'].
	nextValue := self.
	step < 0
		ifTrue: [
			[stop <= nextValue] whileTrue: [
				aBlock value: nextValue.
				nextValue := nextValue + step]]
		ifFalse: [
			[stop >= nextValue] whileTrue: [
				aBlock value: nextValue.
				nextValue := nextValue + step]]
]

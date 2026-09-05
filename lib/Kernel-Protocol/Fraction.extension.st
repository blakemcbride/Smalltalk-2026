"
Exact conversion from a ratio to a Float.
"
Extension { #name : 'Fraction' }

{ #category : '*Kernel-Protocol' }
Fraction >> asFloat [
	"The nearest Float to me, rounding to nearest and ties to even.

	 1983 answers `numerator asFloat / denominator asFloat', which converts
	 BOTH terms before dividing -- so above 2^53 the low bits are gone
	 before the division ever happens.  9090909090909091 asFloat is
	 9090909090909092.0, which is why 0.09090909090909091 could not read
	 back as the 1/11 that printed it however exactly the reader assembled
	 the Fraction.  It reaches every ratio the image computes, not only the
	 ones it reads.

	 So the division is done in EXACT integer arithmetic and rounded once.
	 The pair is scaled until the quotient has 54 significant bits -- 53 for
	 the mantissa and one to round on -- the remainder supplies the sticky
	 bit that decides a tie, and timesTwoPower: applies the exponent without
	 touching the mantissa.  Nothing here converts to Float until the
	 mantissa is small enough to convert exactly."
	| a b shift quotient remainder guard |
	numerator = 0 ifTrue: [^0.0].
	a := numerator abs.
	b := denominator abs.
	shift := 54 - a highBit + b highBit.
	shift > 0
		ifTrue: [a := a bitShift: shift]
		ifFalse: [b := b bitShift: shift negated].
	quotient := a // b.
	remainder := a - (quotient * b).
	"The scaling can land one bit high; fold that bit back into the
	 remainder rather than dropping it, or the tie test below is blind."
	quotient highBit > 54 ifTrue: [
		(quotient bitAnd: 1) = 1 ifTrue: [remainder := remainder + b].
		quotient := quotient bitShift: -1.
		shift := shift - 1].
	guard := quotient bitAnd: 1.
	quotient := quotient bitShift: -1.
	shift := shift - 1.
	"Nearest, ties to even: round up on a guard bit only when something is
	 left over or the mantissa would otherwise end odd."
	(guard = 1 and: [remainder > 0 or: [quotient odd]])
		ifTrue: [
			quotient := quotient + 1.
			quotient highBit > 53 ifTrue: [
				quotient := quotient bitShift: -1.
				shift := shift - 1]].
	^(numerator negative
		ifTrue: [quotient negated]
		ifFalse: [quotient]) asFloat timesTwoPower: shift negated
]

{ #category : '*Kernel-Protocol' }
Fraction >> hash [
	"My Float's hash, because that is what = decides on.

	 `(1/2) = 0.5' is true, because comparing a Fraction with a Float
	 coerces the Fraction, and `(1/2) hash = 0.5 hash' was false -- 1983
	 hashes a Fraction as `numerator bitXor: denominator', which is a fine
	 hash of a ratio and unrelated to the hash of the number it equals.  So
	 `(Set with: 1/2) includes: 0.5' answered false.

	 Answering asFloat's hash makes every Fraction agree with the Float it
	 compares equal to, at the cost of one division per hash and of two
	 different Fractions that round to the same double colliding -- which
	 is what a hash may do, where disagreeing is not.

	 Since Bugs3 B36 the comparison the other way is exact -- a Float is
	 equal to a Fraction only when its exact value IS that Fraction -- and
	 this still agrees with it: such a Fraction converts to that Float
	 without rounding, so both sides hash the same bits.  Two Fractions
	 that round to one double are no longer equal to it and merely
	 collide.  And since Bugs3 B33 the Float hash they inherit reads all
	 sixty-four bits rather than fourteen, so a Set of Fractions is no
	 longer quadratic on top of being slow."
	^self asFloat hash
]

{ #category : '*Kernel-Protocol' }
Fraction >> isFraction [
	^true
]

{ #category : '*Kernel-Protocol' }
Fraction >> setNumerator: n denominator: d [
	"Become the ratio n/d, in lowest terms and with the sign in the
	 numerator.

	 1983 stores whatever it is handed.  `/' reduces afterwards and every
	 arithmetic method sends `reduced', so a Fraction that came out of
	 arithmetic was always in lowest terms and the fault never showed
	 there; a Fraction built BY NAME was not.  `Fraction numerator: 2
	 denominator: 4' answered (2/4), and Fraction>>= compares the two
	 fields rather than the value, so it was not equal to (1/2) -- while
	 Fraction>>hash is `numerator bitXor: denominator', which for 2/4 and
	 for 1/2 is 6 either way.  Two objects that hash alike and answer
	 false to = are two objects a Set holds both of and a Dictionary
	 cannot find by the other (Bugs4 NUMBERS-5).  Reducing here is what
	 Pharo does, and it makes = mean what it says for every Fraction
	 however it was made.

	 The zero test now runs AFTER the coercion rather than before it.
	 1983 asks `d = 0' of the argument as given and only then truncates
	 it, so a denominator of (1/2) passed the test -- (1/2) is not zero --
	 and then truncated to zero, leaving a Fraction (3/0) that raised
	 somewhere else entirely, in whatever later asked it for a value
	 (Bugs4 NUMBERS-6).  A denominator that truncates to nothing is a
	 denominator of zero, whatever it was written as, and it signals
	 ZeroDivide because that is what `1/0' signals in this image and a
	 caller cannot write one handler for two names.

	 The reduction leaves a Fraction even when the denominator comes to
	 one: `Fraction numerator: 4 denominator: 2' is (2/1), not the Integer
	 2, which is Pharo's answer too.  A constructor named for a class
	 answers that class; `reduced' is the method that collapses a whole
	 ratio to an Integer, and with this one reducing there is nothing else
	 left for it to do."
	| num den gcd |
	num := n truncated.
	den := d truncated.
	den = 0 ifTrue: [^ZeroDivide new signal: 'denominator cannot be zero'].
	den < 0 ifTrue: [
		num := num negated.
		den := den negated].
	gcd := num gcd: den.
	gcd = 1
		ifTrue: [
			numerator := num.
			denominator := den]
		ifFalse: [
			numerator := num // gcd.
			denominator := den // gcd]
]

{ #category : '*Kernel-Protocol' }
Fraction class >> coprimeNumerator: n denominator: d [
	"The ratio n/d for a caller that has already PROVED the two share no
	 factor, and that positive d.

	 numerator:denominator: reduces, and reducing is a gcd -- Euclid on two
	 numbers of several hundred digits, in an image whose large integers
	 are divided a byte at a time.  JSONParser>>fractionFrom:k: knows the
	 denominator is a power of ten and divides the 2s and 5s out by hand
	 for exactly that reason: its comment measured 130 of the 162
	 milliseconds a 256-digit fraction cost, and nine seconds of the nine
	 and a half a four-thousand-digit one cost.  Making the constructor
	 reduce would have handed that back, so the caller that has done the
	 work by other means says so here and the gcd is not repeated.

	 Nothing else should use this.  A ratio built through it that is NOT
	 in lowest terms is the fault NUMBERS-5 describes, put back by hand."
	^self new setCoprimeNumerator: n denominator: d
]

{ #category : '*Kernel-Protocol' }
Fraction >> setCoprimeNumerator: n denominator: d [
	"Store the two as given.  See Fraction class>>coprimeNumerator:
	 denominator: for what the caller is promising."
	d = 0 ifTrue: [^ZeroDivide new signal: 'denominator cannot be zero'].
	numerator := n.
	denominator := d
]

{ #category : '*Kernel-Protocol' }
Fraction >> reduced [
	"Me as an Integer when my denominator is one, and otherwise myself.

	 1983 divides both fields by their gcd and builds a new Fraction, which
	 is what kept every arithmetic method correct while the constructor
	 stored whatever it was given.  setNumerator:denominator: now reduces,
	 so by the time anything can send this the gcd is already one, and
	 computing it again would be the second of two Euclids on every add
	 and every multiply -- the expensive half of Fraction arithmetic, paid
	 twice.

	 What is left is the part 1983 did here that the constructor
	 deliberately does not do: collapse a whole ratio to an Integer.  A
	 zero numerator is the same case, since a reduced 0/d is 0/1."
	denominator = 1 ifTrue: [^numerator].
	^self
]

{ #category : '*Kernel-Protocol' }
Fraction >> isZero [
	"Whether I am zero.

	 I am reduced, so a zero value is a zero numerator and nothing else
	 need be looked at.  Number>>isZero sends `= 0', which coerces the
	 Integer up to a Fraction and compares two fields to learn what one
	 field already says."
	^numerator = 0
]

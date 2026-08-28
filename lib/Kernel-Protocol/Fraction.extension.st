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

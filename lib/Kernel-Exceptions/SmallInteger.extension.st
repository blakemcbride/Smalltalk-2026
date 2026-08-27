"
Dividing by zero signals ZeroDivide.

This overrides one 1983 method, and it is worth being explicit about that
because sources/ is frozen and lib/ is where every divergence lives.  The
body is Xerox's, unchanged, except for the last thing it does when the
divisor is zero: it said 'self error: ...', which is now a catchable Error
in any case, and says 'ZeroDivide new signal' so that a caller can name
what it is willing to handle.
"
Extension { #name : 'SmallInteger' }

{ #category : '*Kernel-Exceptions' }
SmallInteger >> / aNumber [
	<primitive: 10>
	aNumber = 0 ifTrue: [^ZeroDivide new signal: 'division by 0'].
	(aNumber isMemberOf: SmallInteger)
		ifTrue: [^(Fraction numerator: self denominator: aNumber) reduced]
		ifFalse: [^super / aNumber]
]

{ #category : '*Kernel-Exceptions' }
SmallInteger >> // aNumber [
	<primitive: 12>
	aNumber = 0 ifTrue: [^ZeroDivide new signal: 'division by 0'].
	^super // aNumber
]

{ #category : '*Kernel-Exceptions' }
SmallInteger >> \\ aNumber [
	<primitive: 11>
	aNumber = 0 ifTrue: [^ZeroDivide new signal: 'division by 0'].
	^super \\ aNumber
]

{ #category : '*Kernel-Exceptions' }
SmallInteger >> quo: aNumber [
	"Divide, truncating toward zero.

	 Two changes to 1983's, and the second is not about zero at all.  It
	 said

		aNumber = 0 ifTrue: [^self error: 'Attempt to divide by zero'].
		(aNumber isMemberOf: SmallInteger)
			ifTrue: [self primitiveFailed]
			ifFalse: [^super quo: aNumber]

	 -- so when the primitive declined a pair of SmallIntegers it gave up
	 rather than computing the answer.  There is exactly one such pair and
	 it is the one a division routine is always asked about:
	 `SmallInteger minVal quo: -1'.  The primitive is right to fail, because
	 the answer is one past the top of the range; the fallback was wrong to
	 stop, because Integer>>quo: computes it in digits and answers
	 4611686018427387904.  `//' and `rem:' are built on quo: and failed with
	 it, while `/', `*' and `\\' at the same receiver all answered.

	 The classic two's-complement asymmetry: the negative range has one more
	 value than the positive one, so negating the smallest overflows."
	<primitive: 13>
	aNumber = 0 ifTrue: [^ZeroDivide new signal: 'division by 0'].
	^super quo: aNumber
]

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

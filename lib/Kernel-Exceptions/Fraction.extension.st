"
A Fraction with no reciprocal signals ZeroDivide.

`(1/2) / 0' reaches Fraction>>reciprocal, not any of the division methods,
so it raised `Error: 0 has no reciprocal' where a SmallInteger receiver
raised ZeroDivide.  Number>>reciprocal, one class up, had the same fault in
different words -- `zero has no reciprocal' -- and is fixed alongside it,
because the two are reached by different receivers and a handler cannot be
written for one of them.

The messages are Xerox's, kept as they were; it is the class that changed.
"
Extension { #name : 'Fraction' }

{ #category : '*Kernel-Exceptions' }
Fraction >> reciprocal [
	numerator = 0 ifTrue: [^ZeroDivide new signal: '0 has no reciprocal'].
	numerator = 1 ifTrue: [^denominator].
	numerator = -1 ifTrue: [^denominator negated].
	^Fraction numerator: denominator denominator: numerator
]

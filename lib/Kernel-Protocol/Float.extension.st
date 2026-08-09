"
ln and exp, over C's rather than the 1983 Taylor series.

The series in Float>>ln and Float>>exp stops at MathApproximationEpsilon
and carries its own error on top of the representation's.  Pharo's doctest
for ""2 raisedTo: 1/12"" is what noticed -- the answer was wrong in the
seventh digit, which is float32's last.

The primitives fail only for a receiver ln has no answer for, and the 1983
methods already raised for those, so the fallback here loses nothing and
says why rather than answering nonsense.
"
Extension { #name : 'Float' }

{ #category : '*Kernel-Protocol' }
Float >> ln [
	<primitive: 58>
	^self error: 'ln is defined only for positive numbers'
]

{ #category : '*Kernel-Protocol' }
Float >> exp [
	<primitive: 59>
	^self error: 'exp cannot be computed for this number'
]

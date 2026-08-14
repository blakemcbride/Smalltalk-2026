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

{ #category : '*Kernel-Protocol' }
Float >> shortestPrintString [
	"The shortest decimal that reads back as me.

	 A unary method, and that is not a stylistic choice: a primitive reads
	 its receiver from the top of the stack, so attaching this to
	 printOn: aStream put the STREAM there instead.  The primitive failed
	 quietly, printOn: fell through to its 1983 fallback, and every Float
	 printed six digits exactly as before -- a fix that changed nothing and
	 said nothing.

	 The digits come from the VM, and that is not laziness.  The obvious
	 fix -- ask absPrintOn:digits: for sixteen instead of six -- was tried
	 and MEASURED, and it does not work: that method builds its digits with
	 Float arithmetic, accurate enough for a twenty-four bit mantissa and
	 not for fifty-three.  Asked for seventeen digits of 3.0/13.0 it
	 answers 0.23076923076923074, which neither this system's compiler nor
	 its own Number>>readFrom: reads back as the number that produced it.
	 Primitive 255 formats with the C library, whose %.*g is correctly
	 rounded and whose strtod is its exact inverse, and tries fifteen
	 digits before sixteen before seventeen -- so 0.1 prints as 0.1 and not
	 as 0.10000000000000001."
	<primitive: 255>
	"No primitive: answer what 1983 would have, which is at least a number
	 rather than nothing at all."
	| stream |
	stream := WriteStream on: (String new: 32).
	self printOn: stream digits: 6.
	^stream contents
]

{ #category : '*Kernel-Protocol' }
Float >> printOn: aStream [
	"Enough digits to survive being read back.

	 1983 prints six significant digits, which was right for the Float it
	 had: twenty-four bits of mantissa, about seven decimal digits of
	 information.  This memory's Float is a double, and six threw the rest
	 away silently -- `(1.0 / 3.0) printString' answered '0.333333'.

	 That is worse than untidy, because printing is not only for looking
	 at.  A literal written into a method's source and read back, a
	 storeString, an inspector -- each produced a DIFFERENT number, with
	 nothing to say it had.

	 The Blue Book build never reaches this method: it loads sources/ alone
	 and never lib/, so its Floats stay single precision and keep printing
	 the way Chapter 30 says and trace2 expects."
	aStream nextPutAll: self shortestPrintString
]

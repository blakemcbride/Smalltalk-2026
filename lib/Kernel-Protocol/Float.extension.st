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

{ #category : '*Kernel-Protocol' }
Float class >> initialize [
	"The constants, at the precision this memory's Float actually has.

	 1983's version assigns `Pi _ 3.14159', `Ln2 _ 0.693147', `Sqrt2 _
	 1.41421' and five-digit coefficient tables for sin, tan, exp and ln,
	 and that was RIGHT for the Float Chapter 30 describes: twenty-four bits
	 of mantissa, about seven decimal digits of information.  This memory's
	 Float is a double.

	 What made it a fault rather than a rounding is that the two halves of
	 the tower stopped agreeing.  ln and exp moved onto primitives 58 and 59
	 -- see the note at the top of this file -- so the system answered
	 seventeen correct digits for those and six for everything built on
	 these constants, with nothing to say which one a caller had.  `Float
	 pi' printed 3.14159 in a system whose printer now shows every digit it
	 has, `(Float pi / 2) sin' answered 0.9999986903681781, and `1.0
	 radiansToDegrees' was wrong in the sixth digit.  Anything geometric --
	 a rotation, an angle, a distance on a sphere -- inherited it, and the
	 seventeen printed digits made the wrongness look like precision.

	 The literals are the shortest decimals that read back as the correctly
	 rounded doubles.  The derived ones are computed rather than written
	 out: halving and doubling are exact in binary, and RadiansPerDegree
	 computed as Pi / 180.0 is the same double as the nearest one to the
	 exact quotient, which was checked rather than assumed.

	 The coefficient tables stay.  Nothing reaches them any more -- sin,
	 cos, tan, arcSin, arcCos, arcTan, ln, exp and sqrt are all primitives
	 now -- but they are the fallback if a primitive ever declines, and a
	 table that is present and unused costs five Arrays."

	Pi := 3.141592653589793.
	Halfpi := Pi / 2.0.
	Fourthpi := Pi / 4.0.
	Twopi := Pi * 2.0.
	RadiansPerDegree := Pi / 180.0.
	Ln2 := 0.6931471805599453.
	Sqrt2 := 1.4142135623730951.
	SinCoefficients := #(-0.166667 0.00833333 -1.98409e-4 2.7526e-6 -2.39e-8 ).
	TanCoefficients := #(0.333331 0.133392 0.0533741 0.0245651 0.00290052 0.00951681 ).
	ExpPCoefficients := #(28.8756 2525.04 ).
	ExpQCoefficients := #(1.0 375.022 7285.73 ).
	LnCoefficients := #(0.237625 0.285254 0.400006 0.666667 2.0 )
]

{ #category : '*Kernel-Protocol' }
Float class >> e [
	"The base of the natural logarithms.

	 Pharo has it and 1983 does not, and `Float e' is how ported source
	 spells the constant rather than recomputing `1.0 exp' at every use."
	^2.718281828459045
]

{ #category : '*Kernel-Protocol' }
Float class >> infinity [
	"The positive infinity.

	 Built by overflowing rather than written as a literal, because a
	 literal that overflows is exactly the spelling this system should not
	 encourage: `1e1000' reads as inf with nothing said, which is a
	 separate complaint.  ldexp of one by two thousand is unambiguous and
	 says what it is doing."
	^1.0 timesTwoPower: 2000
]

{ #category : '*Kernel-Protocol' }
Float class >> nan [
	"A quiet NaN.

	 infinity - infinity, because this system's Float division refuses a
	 zero divisor -- 0.0/0.0 raises ZeroDivide rather than answering one --
	 so the usual spelling is not available here."
	^self infinity - self infinity
]

{ #category : '*Kernel-Protocol' }
Float >> isNaN [
	"Whether I am the one Float that is not equal to itself.

	 That IS the definition, not a trick: IEEE 754 says every comparison
	 involving a NaN is false, so `self ~= self' is true for a NaN and for
	 nothing else.  Written this way rather than against Float nan because
	 there are many NaNs and none of them is equal to any other."
	^self ~= self
]

{ #category : '*Kernel-Protocol' }
Float >> isFinite [
	"Whether I am an ordinary number: not an infinity, not a NaN.

	 self - self is 0.0 for every finite Float, a NaN for either infinity,
	 and a NaN for a NaN -- so one subtraction and one comparison answer
	 both questions at once."
	^(self - self) = 0.0
]

{ #category : '*Kernel-Protocol' }
Float >> isInfinite [
	"Whether I am one of the two infinities."
	^self isFinite not and: [self isNaN not]
]

{ #category : '*Kernel-Protocol' }
Float >> truncated [
	"Me without my fractional part, as an Integer, EXACTLY.

	 Primitive 51 answers when the result fits a SmallInteger and fails
	 above it, and 1983's fallback for the rest is

		^(self quo: 16383.0) * 16383 + (self rem: 16383.0) truncated

	 which has two faults, both measured.  It is WRONG: quo: and rem: on a
	 Float are themselves (self / arg) truncated, so every level does its
	 arithmetic in Float and the multiply by 16383 loses bits the double
	 still had.  `1.0e19 truncated' answered 9999999999999999428 for a value
	 that is exactly 10000000000000000000, and every receiver past
	 SmallInteger maxVal was wrong the same way.  And it is EXPONENTIAL:
	 quo: and rem: each recurse, so the call count doubles per level while
	 the level count grows with the exponent -- 1.0e80 took 3.6 seconds,
	 1.0e90 took 20, and 1.0e100 did not finish in two minutes.  rounded,
	 floor, ceiling and asInteger all go through here and inherited both.

	 A double is an integer times a power of two and nothing else, so no
	 iteration is needed.  exponent answers the e in `self = m * 2^e with
	 1 <= |m| < 2', scaling by a power of two is exact, and scaling so that
	 the value lands in [2^52, 2^53) leaves a mantissa that always fits a
	 SmallInteger -- which means the recursive truncated below is one level
	 deep and takes the primitive.  Shifting that integer back is exact
	 arithmetic in LargeInteger, where it belongs.

	 The two arms differ only in which way the shift goes.  Above 2^52 the
	 value has no fractional part left to drop, so a left shift is the whole
	 answer; below it, quo: truncates toward zero, which is what this method
	 means by truncation and what distinguishes it from floor."

	<primitive: 51>
	| exponent mantissa |
	self isFinite ifFalse: [
		^self error: (self isNaN
			ifTrue: ['a NaN has no truncated value']
			ifFalse: ['an infinity has no truncated value'])].
	self = 0.0 ifTrue: [^0].
	exponent := self exponent.
	mantissa := (self timesTwoPower: 52 - exponent) truncated.
	^exponent >= 52
		ifTrue: [mantissa bitShift: exponent - 52]
		ifFalse: [mantissa quo: (1 bitShift: 52 - exponent)]
]

{ #category : '*Kernel-Protocol' }
Float >> sqrt [
	"My square root.

	 1983 does five Newton steps from an exponent-halved guess, which does
	 converge to the last bit for an ordinary receiver -- this was checked
	 before it was replaced, and `2.0 sqrt' was already right.  55 is the
	 Blue Book's own number for the operation and is correctly rounded for
	 every receiver rather than for most of them, and it does not iterate.

	 The primitive declines a negative receiver and a NaN, so the 1983
	 fallback raises exactly the sentence it always raised."
	<primitive: 55>
	| guess |
	self <= 0.0 ifTrue: [
		self = 0.0 ifTrue: [^0.0].
		^self error: 'sqrt invalid for x < 0'].
	guess := self timesTwoPower: 0 - (self exponent // 2).
	5 timesRepeat: [guess := self - (guess * guess) / (guess * 2.0) + guess].
	^guess
]

{ #category : '*Kernel-Protocol' }
Float >> sin [
	"My sine, my receiver being radians.

	 Primitive 56, the Blue Book's number.  The series it replaces carries
	 five-digit coefficients from Computer Approximations and reduces the
	 argument with a six-digit Pi, so `1.0 sin' answered 0.8414706497000001
	 where the answer is 0.8414709848078965.  The fallback is the 1983
	 series, unchanged, for a receiver the primitive somehow declines."
	<primitive: 56>
	| x x2 sum |
	self < 0.0 ifTrue: [^self negated sin negated].
	self > Twopi ifTrue: [^(self \\ Twopi) sin].
	self > Pi ifTrue: [^(self - Pi) sin negated].
	self > Halfpi ifTrue: [^(Pi - self) sin].
	sum := x := self.
	x2 := x * x.
	SinCoefficients do: [:const | sum := const * (x := x * x2) + sum].
	^sum
]

{ #category : '*Kernel-Protocol' }
Float >> cos [
	"My cosine, my receiver being radians.

	 Primitive 224, which is this system's own number: neither the Blue Book
	 nor Squeak has one, because all of them derive cos from sin.  That
	 derivation was measured and is why this is a primitive -- see the note
	 beside the primitives in src/interp/prim.c.  The fallback is 1983's
	 derivation, which is that derivation."
	<primitive: 224>
	self < 0.0 ifTrue: [^(self + Halfpi) sin].
	^(Halfpi - self) sin
]

{ #category : '*Kernel-Protocol' }
Float >> tan [
	"My tangent, my receiver being radians.  Primitive 225, ours.

	 The fallback is 1983's series, which normalises with a six-digit Pi and
	 answered 1.5574124976270378 for `1.0 tan' where the answer is
	 1.5574077246549023."
	<primitive: 225>
	| x x2 sum |
	self < 0.0 ifTrue: [^self negated tan negated].
	self > Pi ifTrue: [^(self \\ Pi) tan].
	self > Halfpi ifTrue: [^(Pi - self) tan negated].
	self > Fourthpi ifTrue: [^1.0 / (Halfpi - self) tan].
	sum := x := self.
	x2 := x * x.
	TanCoefficients do: [:const | sum := const * (x := x * x2) + sum].
	^sum
]

{ #category : '*Kernel-Protocol' }
Float >> arcTan [
	"The angle in radians whose tangent I am.  Primitive 57, the Blue Book's.

	 The fallback is 1983's series, which stops when its term falls below
	 1.0e-4 -- a threshold chosen for a twenty-four bit mantissa -- and so
	 answered 0.7853975 for `1.0 arcTan' where the answer is
	 0.7853981633974483."
	<primitive: 57>
	| theta term y eps i |
	self = 1.0 ifTrue: [^Fourthpi].
	self = -1.0 ifTrue: [^Fourthpi negated].
	self * self > 1.0
		ifTrue: [
			theta := Halfpi.
			y := -1.0 / (self * self).
			term := -1.0 / self abs]
		ifFalse: [
			theta := 0.0.
			y := 0.0 - (self * self).
			term := self abs].
	i := 1.
	eps := 1.0e-4.
	[term abs > eps] whileTrue: [
		theta := theta + term.
		term := term * y * i asFloat / (i + 2) asFloat.
		i := i + 2].
	^self sign asFloat * theta
]

{ #category : '*Kernel-Protocol' }
Float >> arcSin [
	"The angle in radians whose sine I am.  Primitive 226, ours.

	 The primitive declines a receiver outside -1.0 to 1.0, and the fallback
	 -- 1983's, which is arcTan of self / (1 - self squared) sqrt -- raises
	 for it exactly as before.  That identity is also why this is a
	 primitive: it is exact in the middle of the domain and loses six digits
	 near the ends, where 0.99999999 arcSin came out 1.570654905438225 for
	 1.5706549054381862."
	<primitive: 226>
	self abs > 1.0 ifTrue: [^self error: 'Value out of range'].
	self abs = 1.0 ifTrue: [^self > 0.0 ifTrue: [Halfpi] ifFalse: [Halfpi negated]].
	^(self / (1 - (self * self)) sqrt) arcTan
]

{ #category : '*Kernel-Protocol' }
Float >> arcCos [
	"The angle in radians whose cosine I am.  Primitive 227, ours.

	 1983's `Halfpi - self arcSin' is an ulp out at 0.5 -- 1.0471975511965976
	 for 1.0471975511965979 -- and stays the fallback."
	<primitive: 227>
	^Halfpi - self arcSin
]

{ #category : '*Kernel-Protocol' }
Float >> hash [
	"A hash that agrees with = across the numeric tower.

	 The rule a hash has to keep is that equal objects hash equally, and
	 1983's Float hash -- the high bits of the representation -- kept it
	 only among Floats.  `1 = 1.0' is true and `1 hash = 1.0 hash' was
	 false, so `(Set with: 1) includes: 1.0' answered false for a number
	 the Set contains, and a Dictionary keyed by numbers whose class the
	 caller does not control could not be looked up in at all.

	 A Float with no fractional part hashes as the INTEGER it equals, which
	 is what makes 1 and 1.0 agree, and 2^70 and its Float agree with them.
	 One with a fractional part keeps the 1983 hash, and Fraction>>hash is
	 defined as this one applied to asFloat, so `(1/2) hash = 0.5 hash' --
	 they are equal and now they hash equally.

	 +0.0 and -0.0 are equal and their representations differ, which the
	 integral arm settles: both truncate to 0.

	 WHAT IS STILL NOT FIXED, and why it is not fixed here.  Comparing an
	 Integer with a Float coerces the Integer, so
	 `(2 raisedTo: 70) + 1 = (2 raisedTo: 70) asFloat' is TRUE -- the
	 Integer rounds to the same double -- while the two hash differently,
	 because one hashes as itself and the other as 2^70.  Making those
	 agree means making = compare EXACTLY rather than by coercion, which is
	 a change to Number>>retry:coercing: and to what the Blue Book says
	 arithmetic between representations means.  That is a decision about
	 the numeric tower and not about hashing, so it is named here rather
	 than made here.

	 A NaN or an infinity has no integer and no useful bits; they answer
	 zero and collide, which is what a hash may always do."
	self isFinite ifFalse: [^0].
	self fractionPart = 0.0 ifTrue: [^self truncated hash].
	^(self basicAt: 1) bitAnd: 16383
]

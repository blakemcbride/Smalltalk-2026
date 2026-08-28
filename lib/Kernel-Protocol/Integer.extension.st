Extension { #name : 'Integer' }

{ #category : '*Kernel-Protocol' }
Integer >> isInteger [
	^true
]

{ #category : '*Kernel-Protocol' }
Integer >> hashMultiply [
	"The scrambling step the Smalltalk hashes are built on, in Squeak's
	 exact arithmetic -- 28 bits, times 1664525 -- because ported code
	 compares hashes against values computed elsewhere.

	 The fallback is the same arithmetic written out, so a receiver the
	 primitive declines still answers rather than failing."
	<primitive: 159>
	^(self * 1664525) bitAnd: 16r0FFFFFFF
]

{ #category : '*Kernel-Protocol' }
Integer class >> readFrom: aStreamOrString ifFail: aBlock [
	"Read an integer, answering aBlock's value if there is not one there.

	 1983's readFrom:radix: says in its own comment that it answers zero,
	 not an error, when there are no digits -- which is the right choice for
	 a reader and the wrong one for a parser, because '' and '0' become
	 indistinguishable.  DateParser needs to tell them apart to report which
	 field of a pattern was malformed, so the emptiness is checked here
	 rather than inferred from a zero afterwards."
	| stream |
	stream := aStreamOrString isString
		ifTrue: [aStreamOrString readStream]
		ifFalse: [aStreamOrString].
	stream atEnd ifTrue: [^aBlock value].
	(stream peek isDigit or: [stream peek = $-])
		ifFalse: [^aBlock value].
	^self readFrom: stream
]

{ #category : '*Kernel-Protocol' }
Integer >> printOn: aStream base: base length: minimum padded: zeroPadded [
	"Print me in that base, right-aligned in at least that many characters.

	 The pad is zeros or spaces by the flag, and a minus sign counts toward
	 the width -- it is part of the number, not decoration in front of it.
	 Date and time printing is nothing but this: two digits for the hour,
	 two for the minute, four for the year, each of them zero-padded."
	| text pad |
	text := self printStringRadix: base.
	base = 10 ifTrue: [text := self printString].
	pad := minimum - text size.
	pad > 0 ifTrue: [
		zeroPadded
			ifTrue: [
				"Zeros go AFTER the sign, or -1 padded to three prints as
				 0-1 rather than -01."
				self negative
					ifTrue: [
						aStream nextPut: $-.
						pad timesRepeat: [aStream nextPut: $0].
						aStream nextPutAll: (text copyFrom: 2 to: text size).
						^self]
					ifFalse: [pad timesRepeat: [aStream nextPut: $0]]]
			ifFalse: [pad timesRepeat: [aStream nextPut: $ ]]].
	aStream nextPutAll: text
]

{ #category : '*Kernel-Protocol' }
Integer >> printStringPadded: minimum [
	"My digits, zero-filled on the left to at least that width."
	| stream |
	stream := WriteStream on: (String new: minimum).
	self printOn: stream base: 10 length: minimum padded: true.
	^stream contents
]

{ #category : '*Kernel-Protocol' }
Integer >> numberOfDigits [
	"How many digits I take in base ten, sign not counted.

	 Zero has one digit.  Time class>>nanoSecond: divides by ten raised to
	 this, so answering zero there would scale by a factor of one and put
	 the fraction in the wrong column entirely."
	| n count |
	n := self abs.
	count := 1.
	[n >= 10] whileTrue: [n := n // 10. count := count + 1].
	^count
]

{ #category : '*Kernel-Protocol' }
Integer >> printWithCommasOn: aStream [
	"My digits in groups of three, separated by commas, for reading by eye."
	| text size |
	self negative ifTrue: [aStream nextPut: $-].
	text := self abs printString.
	size := text size.
	1 to: size do: [:i |
		aStream nextPut: (text at: i).
		(size - i) > 0 ifTrue: [
			((size - i) \\ 3) = 0 ifTrue: [aStream nextPut: $,]]]
]

{ #category : '*Kernel-Protocol' }
Integer >> << shiftAmount [
	"Shift left.  Pharo's spelling of bitShift:, and the one that packed
	 formats read naturally in -- a DOS timestamp is built as
	 year << 25 | (month << 21) | ... and says so."
	^self bitShift: shiftAmount
]

{ #category : '*Kernel-Protocol' }
Integer >> >> shiftAmount [
	"Shift right, the inverse of <<."
	^self bitShift: shiftAmount negated
]

{ #category : '*Kernel-Protocol' }
Integer >> & anInteger [
	"Bitwise and.

	 Boolean already answers & as logical and, and that is a different
	 operation on a different class, not an overload of this one -- an
	 Integer receiver can only mean bits.  DosTimestamp>>fromDateAndTime:
	 reads packed fields with `encodedDate >> 5 & 2r1111', which is exactly
	 what the shape is for."
	^self bitAnd: anInteger
]

{ #category : '*Kernel-Protocol' }
Integer >> | anInteger [
	"Bitwise or, the counterpart of &."
	^self bitOr: anInteger
]

{ #category : '*Kernel-Protocol' }
Integer >> isPrime [
	"Trial division up to my square root, which is all HashTableSizes needs
	 of it -- the table it builds is a fixed list of hash-table capacities,
	 computed once at class-initialization time."
	| limit d |
	self < 2 ifTrue: [^false].
	self = 2 ifTrue: [^true].
	self even ifTrue: [^false].
	limit := self sqrt truncated.
	d := 3.
	[d <= limit] whileTrue: [
		self \\ d = 0 ifTrue: [^false].
		d := d + 2].
	^true
]

{ #category : '*Kernel-Protocol' }
Integer >> digitLogic: arg op: op length: len [
	"bitAnd:, bitOr: and bitXor:, byte by byte, over the two's-complement
	 forms of the receiver and the argument.

	 THE BUG THIS EXISTS FOR.  1983's version drops every interior zero byte
	 of a NEGATIVE result.  `(0 - (2 raisedTo: 70)) bitOr: 1' answered
	 -1162144876643701752063 where the answer is -1180591620717411303423 --
	 off by 1.8e19, about 1.5% of the value, with nothing said.  Comparing
	 900 random bit operations against Python's arbitrary precision found 33
	 wrong, and every one of them had a LargeInteger operand and a negative
	 result.  The same 300 operand pairs came back exact for + - * // \\
	 quo: rem: gcd: < and bitShift:, so the fault was these three operators
	 and nothing else.

	 WHY.  The loop works in two's complement and stores in sign-and-
	 magnitude, so a negative result has to be negated on the way out: from
	 the low end, the first non-zero byte becomes 256 - b and every byte
	 above it becomes 255 - b.  1983 tests `b = 0' FIRST and stores a plain
	 zero, which is right below the first non-zero byte and wrong above it,
	 where a zero byte of the two's-complement form is a 255 of the
	 magnitude.  The 0x00FF0000-shaped differences the comparison produced
	 are exactly that.  rdigits was not extended over such a byte either, so
	 the result was then trimmed to a length that ended below it.

	 Written here rather than in sources/ because nothing in sources/ is
	 ever edited; this is the 1983 method with the store rearranged to
	 compute the stored byte first and to raise rdigits only when that byte
	 is non-zero, which also trims a leading 255 - 255 correctly."

	| result neg1 neg2 rneg z1 z2 rz b1 b2 b stored rdigits |
	neg1 := self negative.
	neg2 := arg negative.
	rneg := ((neg1 ifTrue: [-1] ifFalse: [0])
		perform: op
		with: (neg2 ifTrue: [-1] ifFalse: [0])) < 0.
	result := Integer new: len neg: rneg.
	rz := z1 := z2 := true.
	rdigits := 1.
	1 to: result digitLength do: [:i |
		b1 := self digitAt: i.
		neg1 ifTrue: [
			b1 := z1
				ifTrue: [b1 = 0 ifTrue: [0] ifFalse: [z1 := false. 256 - b1]]
				ifFalse: [255 - b1]].
		b2 := arg digitAt: i.
		neg2 ifTrue: [
			b2 := z2
				ifTrue: [b2 = 0 ifTrue: [0] ifFalse: [z2 := false. 256 - b2]]
				ifFalse: [255 - b2]].
		b := b1 perform: op with: b2.
		stored := rneg
			ifTrue: [b = 0
				ifTrue: [rz ifTrue: [0] ifFalse: [255]]
				ifFalse: [rz ifTrue: [rz := false. 256 - b] ifFalse: [255 - b]]]
			ifFalse: [b].
		stored = 0 ifFalse: [rdigits := i].
		result digitAt: i put: stored].
	rdigits ~= result digitLength ifTrue: [^(result growto: rdigits) truncated].
	^result truncated
]

{ #category : '*Kernel-Protocol' }
Integer >> printString: aRadix [
	"My digits in that base, with no radix prefix.

	 Blue Book protocol that this image did not have: 1983 spells it
	 printStringRadix:, and every dialect since spells it printString:.
	 `255 printString: 16' is 'FF'."
	^self printStringRadix: aRadix
]

{ #category : '*Kernel-Protocol' }
Integer >> radix: aRadix [
	"My digits in that base, WITH the radix prefix -- '16rFF'.

	 The pair to printString:, and the difference between them is the same
	 one 1983 draws between printStringRadix: and storeStringRadix:: this
	 answers text the compiler reads back as the same number."
	^self storeStringRadix: aRadix
]

{ #category : '*Kernel-Protocol' }
Integer >> printPaddedWith: aCharacter to: aWidth [
	"My digits in base ten, at least aWidth of them, padded on the left.

	 Blue Book protocol.  A receiver too wide to fit is NOT truncated --
	 the width is a minimum, which is what every caller formatting a table
	 or a timestamp means by it.

	 A zero pad goes AFTER the minus sign, or -1 padded to three prints as
	 `0-1' rather than `-01' -- the same rule printOn:base:length:padded:
	 above already states, and the reason this does not simply prepend."
	| text pad |
	text := self printString.
	pad := aWidth - text size.
	pad <= 0 ifTrue: [^text].
	(aCharacter = $0 and: [self negative]) ifTrue: [
		^'-' , (String new: pad withAll: $0) , (text copyFrom: 2 to: text size)].
	^(String new: pad withAll: aCharacter) , text
]

{ #category : '*Kernel-Protocol' }
Integer >> hash [
	"A hash over every byte of a LargeInteger.

	 SmallInteger>>hash is `self' and never reaches here.  1983's Integer
	 hash is `(self lastDigit bitShift: 8) + (self digitAt: 1)' -- the top
	 byte and the bottom byte, 65,536 values at most and 256 for a run of
	 consecutive numbers, because the top byte does not change.  That was
	 a fair hash when LargeIntegers began at 2^14 and a Set of them was a
	 curiosity; here they begin at 2^62 and a Set of five thousand numbers
	 near 2^70 took eighty-eight seconds to build (Bugs3 B33) --
	 five thousand elements in 256 buckets, every probe walking a chain.

	 The function is String>>hash's, FNV-1a, over the digit bytes from the
	 low end, with the same fold at the end and the same twenty-eight bit
	 width, so that what it answers combines with the rest of the library's
	 hashes on equal terms.  The sign goes in as the seed: a negative number
	 has the same magnitude bytes as its negation and must not hash the
	 same, because it is not equal to it.  See SequenceableCollection>>hash
	 in lib/Collections-Protocol for why the arithmetic never leaves
	 SmallInteger.

	 What = has to be kept consistent with is Float: an integral Float
	 hashes as the Integer it equals (Float>>hash), and since Bugs3 B36 a
	 Float is equal to an Integer only when it is exactly that integer, so
	 hashing the integer's own bytes is the whole of the agreement.  A
	 number of 2^53 or more that a double cannot hold is not equal to any
	 Float, and its hash owes nothing to one."
	| h |
	h := self negative ifTrue: [16r5BD1E995] ifFalse: [2166136261].
	1 to: self digitLength do: [:i |
		h := ((h bitXor: (self digitAt: i)) * 16777619) bitAnd: 16rFFFFFFFF].
	^(h bitXor: (h bitShift: -16)) bitAnd: 16rFFFFFFF
]

{ #category : '*Kernel-Protocol' }
Integer >> asFloat [
	"The nearest Float to me, rounding to nearest and ties to even.

	 SmallInteger's primitive 40 is a C cast and takes precedence; this is
	 the LargeInteger path.  1983's

		1 to: self size do: [:i |
			sum _ (self digitAt: i) * factor + sum.
			factor _ factor * 256.0]

	 adds the bytes up in Float arithmetic, and every one of the `size'
	 additions rounds, so the answer was rounded up to `size' times rather
	 than once: 14 of 200 random conversions were a unit in the last place
	 off against Python's, and 35249751169645885018 asFloat printed as
	 3.5249751169645887e19 where the nearest double is
	 3.5249751169645883e19 (Bugs3 B37).  A conversion that is off by an
	 ulp is not a rounding error; it is a wrong answer that prints like a
	 right one.

	 So the rounding is done ONCE, in exact integer arithmetic, the way
	 Fraction>>asFloat does it: keep the top fifty-four bits -- fifty-three
	 for the mantissa and one to round on -- and let everything below them
	 become a sticky bit that decides a tie.  The fifty-three bits that
	 survive fit a SmallInteger, so the one conversion to Float is exact,
	 and timesTwoPower: puts the exponent back without touching the
	 mantissa.  A number of fifty-three bits or fewer is representable as
	 it stands and takes the primitive through truncated, which normalises
	 a LargeInteger that happens to be small enough into a SmallInteger.

	 A negative receiver converts its magnitude and negates the Float,
	 which is exact.  A receiver too large for any double overflows to an
	 infinity in timesTwoPower:, which is what IEEE 754 says happens; ln,
	 sqrt and log do not go through here for such a number -- see
	 Integer>>ln."
	| magnitude bits shift top guard sticky |
	self negative ifTrue: [^self negated asFloat negated].
	bits := self highBit.
	bits <= 53 ifTrue: [^self truncated asFloat].
	shift := bits - 54.
	top := (self bitShift: shift negated) truncated.
	sticky := (top bitShift: shift) < self.
	guard := top bitAnd: 1.
	top := top bitShift: -1.
	(guard = 1 and: [sticky or: [top odd]]) ifTrue: [top := top + 1].
	^top asFloat timesTwoPower: shift + 1
]

{ #category : '*Kernel-Protocol' }
Integer >> gcd: anInteger [
	"The greatest common divisor of me and anInteger.

	 Roland Silver's algorithm as 1983 printed it, with one line added.
	 1983 opens with an `easy test' -- `m \\ n = 0 ifTrue: [^n]' -- and
	 when the smaller of the two magnitudes is zero that test divides by
	 it, so `12 gcd: 0' raised ZeroDivide where the answer is 12, and
	 `0 gcd: 0' where the answer is 0 (Bugs3 B40).  A gcd with zero is
	 ordinary: Fraction>>reduced sends it for every ratio it makes, and
	 lcm: goes through it.  The added line answers the other number when
	 one is zero, before the easy test can divide by it; the rest is
	 1983's, unchanged."
	| m n d t |
	m := self abs max: anInteger abs.
	n := self abs min: anInteger abs.
	n = 0 ifTrue: [^m].
	m \\ n = 0 ifTrue: [^n].
	d := 0.
	[n even and: [m even]] whileTrue: [
		d := d + 1.
		n := n bitShift: -1.
		m := m bitShift: -1].
	[n even] whileTrue: [n := n bitShift: -1].
	[m even] whileTrue: [m := m bitShift: -1].
	[m = n] whileFalse: [
		m > n
			ifTrue: [m := m - n]
			ifFalse: [
				t := m.
				m := n - m.
				n := t].
		[m even] whileTrue: [m := m bitShift: -1]].
	d = 0 ifTrue: [^m].
	^m bitShift: d
]

{ #category : '*Kernel-Protocol' }
Integer >> lcm: anInteger [
	"The least common multiple of me and anInteger.

	 1983's `self // (self gcd: n) * n' is right whenever neither of us is
	 zero.  `4 lcm: 0' reached gcd: with a zero and raised ZeroDivide
	 (Bugs3 B40); with gcd: fixed it would divide 4 by 4 and answer 0,
	 which is the answer, and `0 lcm: 0' would then divide zero by zero.
	 The multiple of zero and anything is zero, and saying so first is
	 clearer than relying on the arithmetic to arrive there."
	(self = 0 or: [anInteger = 0]) ifTrue: [^0].
	^self // (self gcd: anInteger) * anInteger
]

{ #category : '*Kernel-Protocol' }
Integer >> printOn: aStream base: b [
	"Print me in base b, which must be between 2 and 36.

	 1983's comment promises `2<=b<=256' and its body checks nothing, and
	 Character class>>digitValue: has thirty-six glyphs, so a base above
	 36 indexed past them and printed whatever came next: `40 printString:
	 37' answered '13', `12 printString: 257' answered 'C', and a base of
	 1 or 0 failed three calls down in a message about indices or a
	 ZeroDivide (Bugs3 B41).  A base that cannot be printed is refused
	 here, in one sentence that names it; SmallInteger>>printOn:base: in
	 lib/Concurrency, which does not come through here, refuses the same
	 way.

	 The rest is 1983's LargeInteger digit loop, copied because nothing in
	 sources/ is ever edited and there is no way to add a check in front of
	 a method without owning the method."
	| digits source dest i j pos t rem |
	(b isInteger and: [b between: 2 and: 36])
		ifFalse: [^self error: 'a number can be printed in a base from 2 to 36, not ' , b printString].
	i := self digitLength.
	digits := Array new: i * 8.
	pos := 0.
	dest := i <= 1
		ifTrue: [self]
		ifFalse: [LargePositiveInteger new: i].
	source := self.
	[i > 1] whileTrue: [
		rem := 0.
		j := i.
		[j > 0] whileTrue: [
			t := (rem bitShift: 8) + (source digitAt: j).
			dest digitAt: j put: t // b.
			rem := t \\ b.
			j := j - 1].
		pos := pos + 1.
		digits at: pos put: rem.
		source := dest.
		(source digitAt: i) = 0 ifTrue: [i := i - 1]].
	(dest digitAt: 1) printOn: aStream base: b.
	[pos > 0] whileTrue: [
		aStream nextPut: (Character digitValue: (digits at: pos)).
		pos := pos - 1]
]

{ #category : '*Kernel-Protocol' }
Integer >> ln [
	"My natural logarithm, for a receiver of any size.

	 Number>>ln is `self asFloat ln', and an Integer of 2^1024 or more has
	 no Float -- asFloat overflows to an infinity and ln of that is an
	 infinity, so `(2 raisedTo: 1100) ln' answered inf for a number whose
	 logarithm is 762.46 (Bugs3 B39).  A LargeInteger past 1e308 is not
	 exotic here: factorials, powers and cryptographic moduli all get
	 there, and a wrong answer with nothing said is the worst kind.

	 ln (m * 2^k) is ln m + k ln 2.  Shifting the receiver down until sixty
	 bits are left keeps every bit a double can use, and k ln 2 is exact
	 to the precision of ln 2.  The threshold is set well below the
	 overflow so that the shifted path is never a hair from it; a receiver
	 under 2^1000 converts as before, and a non-positive one reaches
	 Float>>ln and raises exactly as it always did."
	| k |
	(self > 0 and: [self highBit > 1000]) ifFalse: [^self asFloat ln].
	k := self highBit - 60.
	^(self bitShift: k negated) asFloat ln + (k * 0.6931471805599453)
]

{ #category : '*Kernel-Protocol' }
Integer >> log [
	"My base-ten logarithm, for a receiver of any size.

	 Number>>log converts to a Float first, and a receiver past 1e308 has
	 none; see Integer>>ln, which has the same fault and the same fix.
	 A receiver a double can hold goes to Float>>log, which is the
	 machine's log10 and answers 3.0 for 1000 rather than
	 2.9999999999999996 (Bugs3 B39)."
	(self > 0 and: [self highBit > 1000]) ifFalse: [^self asFloat log].
	^self ln / 2.302585092994046
]

{ #category : '*Kernel-Protocol' }
Integer >> sqrt [
	"My square root as a Float, for a receiver of any size.

	 Number>>sqrt is `self asFloat sqrt', and asFloat of an Integer past
	 1e308 is an infinity, so `(2 raisedTo: 1100) sqrt' answered inf for a
	 number whose root is 3.7e165 -- a Float that exists (Bugs3 B39).

	 sqrt (m * 2^(2k)) is 2^k sqrt m.  The receiver is shifted down by an
	 EVEN number of bits so that the power taken out has an exact root,
	 the sixty bits that remain are converted and rooted as usual, and
	 timesTwoPower: puts 2^k back exactly.  The truncated low bits cost at
	 most an ulp in the root, which is the precision a Float root of such
	 a number can have.  A receiver under 2^1000 goes the way it always
	 went, and a negative one raises in Float>>sqrt as before."
	| k |
	(self > 0 and: [self highBit > 1000]) ifFalse: [^self asFloat sqrt].
	k := self highBit - 60 // 2.
	^((self bitShift: (2 * k) negated) asFloat sqrt) timesTwoPower: k
]

{ #category : '*Kernel-Protocol' }
Integer class >> readFrom: aStreamOrString [
	"The Integer at the front of that stream, or of that String.

	 1983 takes a stream only, and `Integer readFrom: '42'' was a
	 doesNotUnderstand of atEnd sent to a String -- while readFrom:ifFail:
	 in this same file, and Number class>>readFrom:, take either (Bugs3
	 B51).  The rest is 1983's: base ten, an optional minus, and zero
	 rather than an error when there are no digits, which readFrom:ifFail:
	 is there to tell apart."
	^self readFrom: (aStreamOrString isString
			ifTrue: [ReadStream on: aStreamOrString]
			ifFalse: [aStreamOrString])
		radix: 10
]

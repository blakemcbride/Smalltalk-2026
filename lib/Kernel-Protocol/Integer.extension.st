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
	 clearer than relying on the arithmetic to arrive there.

	 And the answer is taken as a magnitude.  1983's formula carries the
	 sign of whichever operand it divided, so `-4 lcm: 6' answered -12
	 (Bugs4 NUMBERS-4) -- the only method in the whole differential sweep
	 against Python that disagreed with it, and it disagreed in 63 cases.
	 A common multiple has no sign to speak of: -12 is a common multiple
	 of -4 and 6, but so is 12 and so is -24, and `LEAST' can only mean
	 least in magnitude or the question has no answer at all.  ANSI says
	 non-negative, Python's math.lcm answers 12, and Pharo answers 12."
	(self = 0 or: [anInteger = 0]) ifTrue: [^0].
	^(self // (self gcd: anInteger) * anInteger) abs
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

	 THE BODY IS NO LONGER 1983'S.  Its loop divided the WHOLE magnitude by
	 b to get one digit, a byte at a time in Smalltalk, and did that once
	 per digit -- so printing a number of d digits did d times d/2.4 byte
	 divisions with about seven message sends each.  `(10 raisedTo: 16000)
	 printString' took 26.4 seconds and every doubling of the length
	 multiplied it by four (Bugs4 NUMBERS-3).  printString is on the way
	 out of the image to a person, a log or a socket, so it is one of the
	 two places the bignum layer's quadratic time is visible from outside;
	 Integer class>>valueOfDigits:radix:negated: is the other.

	 The shape of the loop is the same and everything else about it
	 changed.  The magnitude is unpacked ONCE into an Array of limbs of as
	 many bytes as this build's SmallInteger can carry through a
	 division -- four here, so the inner loop runs a quarter as often --
	 and each pass divides by b raised to as many digits as fit beside a
	 limb rather than by b itself, so there are a ninth as many passes in
	 base ten.  Thirty-odd times fewer inner iterations, and every one of
	 them is SmallInteger arithmetic over an Array rather than digitAt:put:
	 into a LargeInteger.  See Integer class>>limbSetupForBase: for how the
	 two numbers are chosen, which is done from SmallInteger maxVal rather
	 than written down, because that is a property of the build.

	 It is still O(d squared): a divide-and-conquer split would need a
	 large-by-large division, and this image's is 1983's Smalltalk
	 digitDiv:neg:, which would cost more than it saved.  What this buys is
	 the constant, and the constant was the whole of the complaint.

	 The most significant chunk is written without its leading zeros and
	 every other chunk with them, which is what makes the pieces join up
	 into one number.  It is written by hand rather than by sending
	 printOn:base: to it: that would be a send to a SmallInteger, and a
	 SmallInteger reaching THIS method -- which it does in any profile that
	 does not load lib/Concurrency -- would recurse for ever."
	| setup limbBits limbBase chunk chunkDigits limbBytes nLimbs limbs
	  high rem t j chunks nChunks buf v i |
	(b isInteger and: [b between: 2 and: 36])
		ifFalse: [^self error: 'a number can be printed in a base from 2 to 36, not ' , b printString].
	setup := Integer limbSetupForBase: b.
	limbBits := setup at: 1.
	limbBase := setup at: 2.
	chunk := setup at: 3.
	chunkDigits := setup at: 4.
	limbBytes := limbBits // 8.
	nLimbs := (self digitLength + limbBytes - 1) // limbBytes max: 1.
	limbs := Array new: nLimbs.
	1 to: nLimbs do: [:k |
		v := 0.
		limbBytes to: 1 by: -1 do: [:t2 |
			v := (v bitShift: 8) + (self digitAt: (k - 1) * limbBytes + t2)].
		limbs at: k put: v].
	high := nLimbs.
	[high > 0 and: [(limbs at: high) = 0]] whileTrue: [high := high - 1].
	high = 0 ifTrue: [^aStream nextPut: $0].
	chunks := OrderedCollection new: 16.
	[high > 0] whileTrue: [
		rem := 0.
		j := high.
		[j > 0] whileTrue: [
			t := rem * limbBase + (limbs at: j).
			limbs at: j put: t // chunk.
			rem := t \\ chunk.
			j := j - 1].
		chunks addLast: rem.
		[high > 0 and: [(limbs at: high) = 0]] whileTrue: [high := high - 1]].
	nChunks := chunks size.
	buf := String new: chunkDigits.
	v := chunks at: nChunks.
	i := chunkDigits.
	[v > 0] whileTrue: [
		buf at: i put: (Character digitValue: v \\ b).
		v := v // b.
		i := i - 1].
	aStream nextPutAll: (buf copyFrom: i + 1 to: chunkDigits).
	nChunks - 1 to: 1 by: -1 do: [:k |
		v := chunks at: k.
		chunkDigits to: 1 by: -1 do: [:d |
			buf at: d put: (Character digitValue: v \\ b).
			v := v // b].
		aStream nextPutAll: buf]
]

{ #category : '*Kernel-Protocol' }
Integer class >> limbSetupForBase: b [
	"How to take a magnitude apart for base b without ever leaving
	 SmallInteger arithmetic: an Array of the limb width in BITS, the limb
	 base, the power of b that is converted in one pass, and how many
	 digits of b that power is.

	 Both conversions between a magnitude and its digits -- printOn:base:
	 and valueOfDigits:radix:negated: -- are a loop over limbs inside a
	 loop over chunks of digits, and both want the same two numbers as
	 large as they can be.  The constraint is one line: the working value
	 is `limb + limbBase * (something below chunk)', so limbBase times
	 chunk must fit a SmallInteger, or the arithmetic promotes to
	 LargeInteger and the method is doing by hand what it was written to
	 avoid.

	 That product is a property of the BUILD, not of this file.  A 62-bit
	 SmallInteger, which is what this system has, takes limbs of four
	 bytes and nine decimal digits a pass; a 30-bit one would take two
	 bytes and four digits.  Writing 4 and 9 down would be right here and
	 silently wrong -- slow, not incorrect -- somewhere else, so they are
	 worked out.  The candidates are whole numbers of bytes because the
	 magnitude is read and written a byte at a time through digitAt:, and
	 the best of them is the one with the largest digits-times-bits, which
	 is the work each pass gets done.

	 Nothing is cached.  The loop runs a few dozen times and the callers
	 run it once each; a class variable holding it would be shared mutable
	 state between workers for a value that is cheaper to compute than to
	 lock.

	 If no candidate fits -- a build whose SmallInteger cannot hold 256
	 times the base -- the smallest is answered anyway.  The arithmetic
	 then promotes to LargeInteger and is slow, which is what it was
	 before; nothing gives a wrong answer."
	| maxV limbBase chunk digits score bestBits bestChunk bestDigits |
	maxV := SmallInteger maxVal + 1.
	score := 0.
	bestBits := 8.
	bestChunk := b.
	bestDigits := 1.
	#(8 16 24 32) do: [:bits |
		limbBase := 1 bitShift: bits.
		limbBase * b <= maxV ifTrue: [
			chunk := b.
			digits := 1.
			[chunk * b * limbBase <= maxV] whileTrue: [
				chunk := chunk * b.
				digits := digits + 1].
			digits * bits > score ifTrue: [
				score := digits * bits.
				bestBits := bits.
				bestChunk := chunk.
				bestDigits := digits]]].
	^Array with: bestBits with: (1 bitShift: bestBits) with: bestChunk with: bestDigits
]

{ #category : '*Kernel-Protocol' }
Integer class >> readFrom: aStream radix: radix [
	"The Integer at the front of that stream, written in that base.

	 1983's loop is `value := value * radix + digit', once per digit, and
	 every one of those multiplications is a LargeInteger multiplied by a
	 SmallInteger through Integer>>digitMultiply:neg:, which walks the
	 whole growing magnitude a byte at a time in Smalltalk and allocates a
	 new LargeInteger for the answer.  Reading a number of d digits
	 therefore cost d passes over d/2.4 bytes: `('1', (String new: 8000
	 withAll: $0)) asNumber' took 59 seconds, the worst constant in the
	 whole bignum layer (Bugs4 NUMBERS-3).  This is the path a number on a
	 socket takes -- Number class>>readFrom: sends it, String>>asNumber
	 and String>>asInteger reach it, and NUMBERS-2 is about a form field
	 doing exactly that before anything has authenticated.

	 The digits are gathered first and converted afterwards, by
	 valueOfDigits:radix:negated:, which does the arithmetic in an Array
	 of SmallInteger limbs.  Gathering them is one pass over the stream
	 and one String; it is what lets the conversion see how long the
	 number is before it starts, which is what a chunked conversion needs.

	 The String is grown by doubling rather than written through a
	 WriteStream, and a short run of digits is converted by
	 valueOfDigits:radix:negated:'s own plain loop rather than by setting
	 up limbs.  Both are there because SHORT numbers are what this method
	 reads nearly every time -- Time class>>readFrom: reads three
	 two-digit fields per call -- and a fixed cost of a couple of hundred
	 sends to save nine tenths of the work on a number that has no work
	 in it is a loss.  It was measured as one: test_image's `every minute
	 of the day survives printString and readFromString:' check ran out
	 of its twenty-million-bytecode budget when the setup was
	 unconditional.

	 Everything else is 1983's, deliberately: base ten by default, an
	 optional leading minus, the first character that is not a digit of
	 the radix pushed back on the stream, and ZERO rather than an error
	 when there are no digits at all -- readFrom:ifFail: in this same file
	 exists to tell those two apart, and Number class>>readFrom: relies on
	 the pushback to find the decimal point.

	 A String is taken as well as a stream, for the reason readFrom: takes
	 one (Bugs3 B51): `Integer readFrom: '42'' is what ported code writes.
	 B51 gave readFrom: and readFrom:ifFail: that courtesy and left this
	 one, two lines below them, sending peekFor: to a String."
	| stream neg buf count c dv |
	stream := aStream isString ifTrue: [ReadStream on: aStream] ifFalse: [aStream].
	neg := stream peekFor: $-.
	buf := String new: 24.
	count := 0.
	[stream atEnd] whileFalse: [
		c := stream next.
		dv := c digitValue.
		(dv < 0 or: [dv >= radix])
			ifTrue: [
				stream skip: -1.
				^self valueOfDigits: (buf copyFrom: 1 to: count)
					radix: radix negated: neg]
			ifFalse: [
				count = buf size ifTrue: [buf := buf , (String new: buf size)].
				count := count + 1.
				buf at: count put: c]].
	^self valueOfDigits: (buf copyFrom: 1 to: count) radix: radix negated: neg
]

{ #category : '*Kernel-Protocol' }
Integer class >> valueOfDigits: aString radix: b negated: neg [
	"The Integer those digit characters spell in base b, negated when neg.
	 Zero for no digits, which is what 1983's reader answers and what
	 Number class>>readFrom: is written against.

	 The mirror of Integer>>printOn:base:, and the reason is the same: the
	 whole conversion happens in an Array of SmallInteger limbs, chunkDigits
	 digits folded in per pass, so it costs a ninth as many passes over a
	 quarter as many limbs as the digit-at-a-time loop it replaces.  It is
	 still O(d squared) -- with a schoolbook multiply a divide-and-conquer
	 split saves nothing worth the risk -- but the constant is thirty-odd
	 times smaller, which is what the finding measured (Bugs4 NUMBERS-3).

	 The limb array is sized from an upper bound on the answer's bit count:
	 `b highBit' is at least the base-two logarithm of b for every b, so
	 the estimate can be short of nothing, and two spare limbs cover the
	 rounding and the final carry.  The invariant that keeps every value a
	 SmallInteger is limbSetupForBase:'s: the carry stays below chunk, so
	 the working value stays below limbBase times chunk.

	 The answer is built at its exact byte length rather than padded and
	 trimmed.  LargePositiveInteger>>truncated drops a high zero byte by
	 COPYING the whole number and recurring, so handing it a magnitude
	 with a dozen spare bytes would be a dozen copies; what it is asked
	 for here is the other half of its job, normalising a magnitude that
	 fits into a SmallInteger.

	 Nine digits or fewer keep the plain loop.  Nine digits of base 36 is
	 under 2^47, so on this build the whole thing stays a SmallInteger and
	 costs nine multiplications; setting up limbs for it would cost more
	 than the number is worth, and nearly every number anybody reads is
	 this one.  On a build too narrow to hold it the loop promotes to
	 LargeInteger and is exactly 1983's, which is to say correct and
	 slow."
	| setup limbBits limbBase chunk chunkDigits limbBytes limbs nLimbs used
	  i end v carry t j scale result nBytes topBytes idx |
	aString isEmpty ifTrue: [^0].
	aString size <= 9 ifTrue: [
		v := 0.
		1 to: aString size do: [:k | v := v * b + (aString at: k) digitValue].
		neg ifTrue: [^v negated truncated].
		^v].
	setup := self limbSetupForBase: b.
	limbBits := setup at: 1.
	limbBase := setup at: 2.
	chunk := setup at: 3.
	chunkDigits := setup at: 4.
	limbBytes := limbBits // 8.
	nLimbs := (aString size * b highBit // limbBits) + 2.
	limbs := Array new: nLimbs.
	1 to: nLimbs do: [:k | limbs at: k put: 0].
	used := 0.
	i := 1.
	[i <= aString size] whileTrue: [
		end := i + chunkDigits - 1 min: aString size.
		v := 0.
		i to: end do: [:k | v := v * b + (aString at: k) digitValue].
		scale := end - i + 1 = chunkDigits
			ifTrue: [chunk]
			ifFalse: [b raisedTo: end - i + 1].
		carry := v.
		j := 1.
		[j <= used] whileTrue: [
			t := (limbs at: j) * scale + carry.
			limbs at: j put: (t bitAnd: limbBase - 1).
			carry := t bitShift: limbBits negated.
			j := j + 1].
		[carry > 0] whileTrue: [
			used := used + 1.
			limbs at: used put: (carry bitAnd: limbBase - 1).
			carry := carry bitShift: limbBits negated].
		i := end + 1].
	used = 0 ifTrue: [^0].
	v := limbs at: used.
	topBytes := 0.
	[v > 0] whileTrue: [
		topBytes := topBytes + 1.
		v := v bitShift: -8].
	nBytes := (used - 1) * limbBytes + topBytes.
	result := Integer new: nBytes neg: neg.
	1 to: used do: [:k |
		v := limbs at: k.
		1 to: limbBytes do: [:t2 |
			idx := (k - 1) * limbBytes + t2.
			idx <= nBytes ifTrue: [result digitAt: idx put: (v bitAnd: 255)].
			v := v bitShift: -8]].
	^result truncated
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

{ #category : '*Kernel-Protocol' }
Integer >> floorLog: radix [
	"The floor of my logarithm in that base, EXACTLY, for a receiver of
	 any size.

	 Number>>floorLog: is `self asFloat floorLog: radix', and an Integer of
	 2^1024 or more has no Float: asFloat overflows to an infinity, and
	 Float>>floorLog: of an infinity used to recurse for ever squaring an
	 Integer radix -- 10, 100, 10^4, 10^8 -- until the worker was doing a
	 billion-bit multiply and never came back (Bugs4 NUMBERS-1).  Float
	 refuses a non-finite receiver now, so that line would raise rather
	 than hang; but `(2 raisedTo: 4000) floorLog: 10' has a perfectly good
	 answer, 1204, and raising is not it.  This is the same fault and the
	 same fix as Integer>>ln, Integer>>log and Integer>>sqrt (Bugs3 B39):
	 an Integer answers its own question rather than handing it to a
	 double that cannot hold it.

	 Base two is highBit - 1 and nothing else, exactly, at any size.  For
	 any other whole base the logarithm is estimated in Floats -- through
	 Integer>>ln, which is itself exact enough at any size -- and then
	 CORRECTED against exact powers, so the answer never depends on the
	 estimate being right to the last bit.  The correction is a step or
	 two: raisedTo: is repeated squaring and the candidate is bounded by
	 my own bit count, so it can never build a number wildly larger than
	 I am.

	 A base written as a Float but standing for a whole number --
	 `floorLog: 10.0' -- is taken as that whole number, so that it gets
	 the exact path too rather than falling back to a double that cannot
	 hold the receiver; a Fraction that stands for one is already an
	 Integer, since Fraction reduces itself.  A non-positive receiver, or
	 a base that is genuinely fractional, goes to Float>>floorLog: as it always
	 did, including 1983's answer of zero for a receiver smaller than the
	 base, which is what `self assumed positive' in its comment means."
	| base estimate |
	base := (radix isFloat and: [radix isFinite and: [radix fractionPart = 0.0]])
		ifTrue: [radix truncated]
		ifFalse: [radix].
	(self > 0 and: [base isInteger and: [base > 1]])
		ifFalse: [^self asFloat floorLog: radix].
	base = 2 ifTrue: [^self highBit - 1].
	estimate := (self ln / base ln) floor.
	estimate < 0 ifTrue: [estimate := 0].
	[estimate > 0 and: [(base raisedTo: estimate) > self]]
		whileTrue: [estimate := estimate - 1].
	[(base raisedTo: estimate + 1) <= self]
		whileTrue: [estimate := estimate + 1].
	^estimate
]

{ #category : '*Kernel-Protocol' }
Integer >> digitMultiply: arg neg: ng [
	"My magnitude times arg's, signed by ng.  The engine under every
	 Integer multiplication that the SmallInteger primitive declines.

	 1983 multiplies a BYTE at a time and splits each byte into two nibbles
	 to do it, because the Blue Book's SmallInteger is thirteen bits and a
	 byte times a byte plus a carry does not fit one:

		high := (arg digitAt: j) * xh.
		low  := (arg digitAt: j) * xl + ((high bitAnd: 15) bitShift: 4)
		           + carry + (prod digitAt: k).

	 -- seven sends and two digitAt:s per byte pair, with a digitAt:put:
	 into a LargeInteger for every one of them.  It is O(m by n) and the
	 constant is enormous: 3^5000 times 7^5000, a thousand bytes each, took
	 1.79 seconds, and `10 raisedTo: 16000' -- which is fifteen squarings
	 and multiplies -- took 11.3 (Bugs4 NUMBERS-3).  That is the whole
	 bignum layer's floor: raisedTo:, printString before it was rewritten,
	 Fraction arithmetic and every cryptographic modulus stand on it.

	 This build's SmallInteger is not thirteen bits.  It is asked how many
	 it has -- SmallInteger maxVal, not a number written down -- and while
	 a product of two 24-bit limbs plus a carry and an accumulator fits it,
	 the multiplication is done THREE BYTES at a time in an Array of
	 SmallIntegers.  Nine times fewer inner iterations than a byte at a
	 time, each of them plain SmallInteger arithmetic over an Array rather
	 than nibble arithmetic through digitAt:put:.  Twenty-four bits is the
	 largest byte-aligned limb whose square leaves room for the carry
	 inside a 62-bit SmallInteger; on a build too narrow for even that, the
	 arithmetic promotes to LargeInteger and is merely slow, which is what
	 it was before.

	 It is still SCHOOLBOOK -- O(m by n) -- and that is a decision.
	 Karatsuba over 1983's byte multiply was measured on paper at three to
	 four times for a thousand-byte operand, against nine here, and it puts
	 a recursion with three sub-multiplies and four large additions under
	 the most fundamental operation in the numeric tower.  This rewrite is
	 provable by a different route: it computes exactly what schoolbook
	 computes, and it was checked against Python over a wide range of
	 lengths, signs and bit patterns before it was kept.

	 Small operands keep 1983's loop, in smallDigitMultiply:neg: below.
	 Unpacking to limbs and packing back is O(m + n), which is free beside
	 a large multiplication and is not free beside an eight-byte one --
	 and an eight-byte one is what SmallInteger>>* falls back to on
	 overflow, which is the commonest arrival here of all."
	| m n am bm ml nl out outLen carry t k high v topBytes nBytes prod idx |
	m := self digitLength.
	n := arg digitLength.
	((arg digitAt: 1) = 0 and: [n = 1]) ifTrue: [^0].
	m * n <= 64 ifTrue: [^self smallDigitMultiply: arg neg: ng].
	ml := (m + 2) // 3.
	nl := (n + 2) // 3.
	am := Array new: ml.
	1 to: ml do: [:x |
		am at: x put: (((self digitAt: (x - 1) * 3 + 3) bitShift: 16)
			+ ((self digitAt: (x - 1) * 3 + 2) bitShift: 8)
			+ (self digitAt: (x - 1) * 3 + 1))].
	bm := Array new: nl.
	1 to: nl do: [:x |
		bm at: x put: (((arg digitAt: (x - 1) * 3 + 3) bitShift: 16)
			+ ((arg digitAt: (x - 1) * 3 + 2) bitShift: 8)
			+ (arg digitAt: (x - 1) * 3 + 1))].
	outLen := ml + nl.
	out := Array new: outLen.
	1 to: outLen do: [:x | out at: x put: 0].
	1 to: ml do: [:i |
		(am at: i) = 0 ifFalse: [
			v := am at: i.
			carry := 0.
			1 to: nl do: [:j |
				t := (out at: i + j - 1) + (v * (bm at: j)) + carry.
				out at: i + j - 1 put: (t bitAnd: 16777215).
				carry := t bitShift: -24].
			k := i + nl.
			[carry > 0] whileTrue: [
				t := (out at: k) + carry.
				out at: k put: (t bitAnd: 16777215).
				carry := t bitShift: -24.
				k := k + 1]]].
	high := outLen.
	[high > 0 and: [(out at: high) = 0]] whileTrue: [high := high - 1].
	high = 0 ifTrue: [^0].
	v := out at: high.
	topBytes := 0.
	[v > 0] whileTrue: [
		topBytes := topBytes + 1.
		v := v bitShift: -8].
	nBytes := (high - 1) * 3 + topBytes.
	prod := Integer new: nBytes neg: ng.
	1 to: high do: [:x |
		v := out at: x.
		1 to: 3 do: [:t2 |
			idx := (x - 1) * 3 + t2.
			idx <= nBytes ifTrue: [prod digitAt: idx put: (v bitAnd: 255)].
			v := v bitShift: -8]].
	^prod truncated
]

{ #category : '*Kernel-Protocol' }
Integer >> smallDigitMultiply: arg neg: ng [
	"1983's digitMultiply:neg:, character for character, for operands
	 small enough that unpacking them into limbs would cost more than it
	 saves.  Copied rather than inherited because sources/ is never edited
	 and there is no way to keep a method and add a case in front of it.
	 See digitMultiply:neg: for why the other case exists."
	| prod pl carry digit k xh xl low high |
	((arg digitAt: 1) = 0 and: [arg digitLength = 1]) ifTrue: [^0].
	pl := self digitLength + arg digitLength.
	prod := Integer new: pl neg: ng.
	1 to: self digitLength do: [:i |
		(digit := self digitAt: i) ~= 0 ifTrue: [
			k := i.
			carry := 0.
			xh := digit bitShift: -4.
			xl := digit bitAnd: 15.
			1 to: arg digitLength do: [:j |
				high := (arg digitAt: j) * xh.
				low := (arg digitAt: j) * xl
					+ ((high bitAnd: 15) bitShift: 4) + carry + (prod digitAt: k).
				carry := (high bitShift: -4) + (low bitShift: -8).
				prod digitAt: k put: (low bitAnd: 255).
				k := k + 1].
			prod digitAt: k put: carry]].
	(prod digitAt: pl) = 0 ifTrue: [^(prod growby: -1) truncated].
	^prod truncated
]

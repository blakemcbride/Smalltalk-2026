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

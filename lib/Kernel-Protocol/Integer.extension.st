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

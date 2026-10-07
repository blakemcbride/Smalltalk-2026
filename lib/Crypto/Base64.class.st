"
Bytes as text and back, RFC 4648 section 4: the alphabet A-Z a-z 0-9 + /,
three bytes to four characters, and = to fill the last group.

    Base64 encode: 'foob'                 'Zm9vYg=='
    Base64 encodeWithoutPadding: 'foob'   'Zm9vYg'
    Base64 decode: 'Zm9vYg=='             'foob'
    Base64 decode: 'Zm9vYg'               'foob' -- padding is optional on the way in
    Base64 decode: 'Zg?' ifInvalid: [nil] nil

It is here because the stored password format (PasswordHash) carries its
salt and its key this way, without padding, as Java's
Base64.getEncoder().withoutPadding() writes them; a decoder that accepts
both forms reads what Java wrote and what anybody else did.  No other
alphabet: the URL-safe variant with - and _ is a different encoding and a
value in it is refused rather than guessed at.

The decode table is a class variable filled by the class initializer,
eagerly, and read only -- HttpCodec's arrangement, for HttpCodec's
reason: every worker reads it at once and nothing ever writes it again.
"
Class {
	#name : 'Base64',
	#superclass : 'Object',
	#classVars : [ 'Alphabet', 'Decode' ],
	#category : 'Crypto'
}

{ #category : 'class initialization' }
Base64 class >> initialize [
	"Base64 initialize"

	Alphabet := 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'.
	Decode := Array new: 256.
	1 to: 64 do: [:k | Decode at: (Alphabet at: k) asInteger + 1 put: k - 1]
]

{ #category : 'encoding' }
Base64 class >> encode: aByteString [
	"The bytes as Base64 text, padded with = to a multiple of four."

	^self encode: aByteString padded: true
]

{ #category : 'encoding' }
Base64 class >> encodeWithoutPadding: aByteString [
	"The bytes as Base64 text with no = at the end: the form the password
	 format stores."

	^self encode: aByteString padded: false
]

{ #category : 'encoding' }
Base64 class >> encode: bytes padded: aBoolean [
	| out n i b0 b1 b2 |
	n := bytes size.
	out := WriteStream on: (String new: n * 4 // 3 + 4).
	i := 1.
	[i + 2 <= n] whileTrue: [
		b0 := (bytes at: i) asInteger.
		b1 := (bytes at: i + 1) asInteger.
		b2 := (bytes at: i + 2) asInteger.
		out nextPut: (Alphabet at: (b0 bitShift: -2) + 1);
			nextPut: (Alphabet at: (((b0 bitAnd: 3) bitShift: 4) bitOr: (b1 bitShift: -4)) + 1);
			nextPut: (Alphabet at: (((b1 bitAnd: 15) bitShift: 2) bitOr: (b2 bitShift: -6)) + 1);
			nextPut: (Alphabet at: (b2 bitAnd: 63) + 1).
		i := i + 3].
	n - i = 1 ifTrue: [
		"two bytes left: three characters and one ="
		b0 := (bytes at: i) asInteger.
		b1 := (bytes at: i + 1) asInteger.
		out nextPut: (Alphabet at: (b0 bitShift: -2) + 1);
			nextPut: (Alphabet at: (((b0 bitAnd: 3) bitShift: 4) bitOr: (b1 bitShift: -4)) + 1);
			nextPut: (Alphabet at: ((b1 bitAnd: 15) bitShift: 2) + 1).
		aBoolean ifTrue: [out nextPut: $=]].
	n - i = 0 ifTrue: [
		"one byte left: two characters and two ="
		b0 := (bytes at: i) asInteger.
		out nextPut: (Alphabet at: (b0 bitShift: -2) + 1);
			nextPut: (Alphabet at: ((b0 bitAnd: 3) bitShift: 4) + 1).
		aBoolean ifTrue: [out nextPut: $=; nextPut: $=]].
	^out contents
]

{ #category : 'decoding' }
Base64 class >> decode: aString [
	"The bytes the text names.  Raises CryptoError for text that is not
	 Base64: a character outside the alphabet, a length that no encoding
	 produces, = anywhere but the end."

	^self decode: aString ifInvalid: [CryptoError new signal: 'not Base64: ', aString printString]
]

{ #category : 'decoding' }
Base64 class >> decode: aString ifInvalid: aBlock [
	"The bytes the text names, or the value of aBlock when it is not Base64.
	 Padding is accepted and not required.

	 Padding that is there must be the padding the length calls for, and
	 the bits a short last group leaves over must be zero (Bugs5
	 FILES-12).  Up to two = were stripped and forgotten, which accepted
	 what decode: says it refuses: `=' alone decoded to the empty string,
	 `Zm9v=' and `Zm9v==' to `foo', and `Zh==' -- whose h carries four bits
	 no byte has room for -- to the same byte as `Zg=='.  RFC 4648 section
	 3.5 lets a decoder refuse both, and one that takes them gives two
	 texts for one value, which is exactly what a stored hash compared as
	 text must not have."

	| n out i v0 v1 v2 v3 left pad |
	n := aString size.
	"Up to two = at the end are padding; strip them, and count them."
	(n > 0 and: [(aString at: n) = $=]) ifTrue: [n := n - 1].
	(n > 0 and: [(aString at: n) = $=]) ifTrue: [n := n - 1].
	pad := aString size - n.
	n \\ 4 = 1 ifTrue: [^aBlock value].
	"Padding fills the last group to four, so there is none after a whole
	 group and exactly 4 - (n \\ 4) after a short one."
	(pad > 0 and: [pad ~= ((4 - (n \\ 4)) \\ 4)]) ifTrue: [^aBlock value].
	out := WriteStream on: (String new: n * 3 // 4).
	i := 1.
	[i <= n] whileTrue: [
		left := n - i + 1.
		v0 := self valueOf: (aString at: i) ifInvalid: [^aBlock value].
		v1 := self valueOf: (aString at: i + 1) ifInvalid: [^aBlock value].
		out nextPut: (Character value: ((v0 bitShift: 2) bitOr: (v1 bitShift: -4))).
		"A last group of two characters holds one byte and four bits over;
		 of three, two bytes and two bits over.  Those must be zero."
		(left = 2 and: [(v1 bitAnd: 15) ~= 0]) ifTrue: [^aBlock value].
		left >= 3 ifTrue: [
			v2 := self valueOf: (aString at: i + 2) ifInvalid: [^aBlock value].
			out nextPut: (Character value: (((v1 bitAnd: 15) bitShift: 4) bitOr: (v2 bitShift: -2))).
			(left = 3 and: [(v2 bitAnd: 3) ~= 0]) ifTrue: [^aBlock value].
			left >= 4 ifTrue: [
				v3 := self valueOf: (aString at: i + 3) ifInvalid: [^aBlock value].
				out nextPut: (Character value: (((v2 bitAnd: 3) bitShift: 6) bitOr: v3))]].
		i := i + 4].
	^out contents
]

{ #category : 'private' }
Base64 class >> valueOf: aCharacter ifInvalid: aBlock [
	| v |
	v := Decode at: aCharacter asInteger + 1.
	^v isNil ifTrue: [aBlock value] ifFalse: [v]
]

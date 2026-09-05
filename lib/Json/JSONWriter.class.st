"
Writing JSON.

Everything reaches this class through asJsonValue first, so by the time a
value is written it is a JSONObject, a JSONArray, a String, a Number, true,
false or nil, and the switch below is total.  That is the whole reason the
conversion is a separate step: an Array of Rectangles fails where somebody
put a Rectangle in an Array, naming the Rectangle, rather than here where
all that can be said is that something in the tree could not be written.

NUMBERS.  An Integer prints as itself.  A Fraction prints as the exact
decimal it is, when it has one -- 3/2 is 1.5 and 1/10 is 0.1, which is how
a document read by JSONParser comes back out unchanged.  A Fraction with no
finite decimal, which only a caller can have produced because no JSON
document contains one, is written as its Float.  1/3 has to become
0.3333333333333333 or nothing, and nothing is worse.

A Float prints with Float>>printString, which produces JSON's grammar for
every finite value: 1.0e-5 and 1.0e100 are both numbers to RFC 8259, whose
exponent is an optional sign and digits.  A Float that is not finite is
refused, because JSON has no spelling for infinity or for a NaN and every
library that writes one anyway produces a document nothing else will read.

STRINGS ARE UTF-8, and are refused when they are not, for the same reason
and with the same argument.  A String here is bytes; RFC 8259 has one
encoding and no field to declare another.  See writeString:.

ONE WRITER IS ONE DOCUMENT, like the parser: my state is a stream, an
indent and a level, and asJsonString makes one of me per call.

What I read may be shared, though, and that is why writeObject: goes
through keysAndValuesDo: rather than reaching into the object: that answers
a snapshot taken under the object's own lock, so each object is written as
it was at one instant.  A document being changed while it is written comes
out with each of its objects consistent and no promise that they are
consistent with each other, which is the most anybody can be given without
locking the whole tree -- and locking a whole tree to write it is how a
writer becomes the thing that stops the machine.

PRETTY PRINTING breaks lines with Character nl, 10, and not with the
carriage return this image's text panes use.  That follows
DbSchemaGraph>>storeOn:, and for the same reason: text that leaves this
system goes out with the ending the rest of the world reads, and
lib/Files-Fixes translates on the way to a file if a file is where it is
going.  JSON does not care either way -- whitespace between tokens is
insignificant -- so the only party this affects is the person reading the
output, and they are not on an Alto.
"
Class {
	#name : 'JSONWriter',
	#superclass : 'Object',
	#instVars : [ 'stream', 'indent', 'level' ],
	#category : 'Json'
}

{ #category : 'writing' }
JSONWriter class >> write: aValue [
	"That value as compact JSON text: no spaces anywhere they are not
	 inside a string."

	^String streamContents: [:aStream | (self on: aStream) write: aValue]
]

{ #category : 'writing' }
JSONWriter class >> write: aValue indent: anInteger [
	"That value as JSON text, one member to a line, indented by that many
	 spaces per level.  An indent of zero is the compact form."

	^String streamContents: [:aStream |
		(self on: aStream indent: anInteger) write: aValue]
]

{ #category : 'writing' }
JSONWriter class >> write: aValue on: aStream [
	^(self on: aStream) write: aValue
]

{ #category : 'instance creation' }
JSONWriter class >> on: aStream [
	^self new setStream: aStream indent: 0
]

{ #category : 'instance creation' }
JSONWriter class >> on: aStream indent: anInteger [
	^self new setStream: aStream indent: anInteger
]

{ #category : 'initialization' }
JSONWriter >> initialize [
	indent := 0.
	level := 0
]

{ #category : 'writing' }
JSONWriter >> write: aValue [
	"The entry point, and the only place the conversion happens.

	 asJsonValue answers the receiver for everything that is already a JSON
	 value, so the cost for a tree that came out of the parser is one send
	 at the top and nothing else."

	^self writeValue: aValue asJsonValue
]

{ #category : 'private' }
JSONWriter >> writeValue: aValue [
	aValue isNil ifTrue: [^stream nextPutAll: 'null'].
	aValue == true ifTrue: [^stream nextPutAll: 'true'].
	aValue == false ifTrue: [^stream nextPutAll: 'false'].
	(aValue isKindOf: JSONObject) ifTrue: [^self writeObject: aValue].
	(aValue isKindOf: JSONArray) ifTrue: [^self writeArray: aValue].
	aValue isString ifTrue: [^self writeString: aValue].
	aValue isNumber ifTrue: [^self writeNumber: aValue].
	"Unreachable while asJsonValue is doing its job, and here because the
	 day it is not is the day this message is worth having."
	^JSONError signal: aValue class name article, ' ', aValue class name,
		' is not a JSON value'
]

{ #category : 'private' }
JSONWriter >> writeObject: anObject [
	| first |
	anObject isEmpty ifTrue: [^stream nextPutAll: '{}'].
	stream nextPut: ${.
	self enter.
	first := true.
	anObject keysAndValuesDo: [:name :value |
		first ifFalse: [stream nextPut: $,].
		first := false.
		self break.
		self writeString: name.
		stream nextPut: $:.
		indent > 0 ifTrue: [stream nextPut: $ ].
		self writeValue: value].
	self leave.
	self break.
	stream nextPut: $}
]

{ #category : 'private' }
JSONWriter >> writeArray: anArray [
	| first |
	anArray isEmpty ifTrue: [^stream nextPutAll: '[]'].
	stream nextPut: $[.
	self enter.
	first := true.
	anArray do: [:value |
		first ifFalse: [stream nextPut: $,].
		first := false.
		self break.
		self writeValue: value].
	self leave.
	self break.
	stream nextPut: $]
]

{ #category : 'private' }
JSONWriter >> writeString: aString [
	"The quoted form.

	 Only what RFC 8259 requires: the quote, the backslash, and everything
	 below a space.  Not the forward slash, which org.json escapes so that
	 a document can be embedded in an HTML script element without ending it
	 early -- a real hazard, and one that belongs to whatever does the
	 embedding rather than to every document this system writes.

	 A byte above 126 goes out as itself, which is what makes the output
	 UTF-8 when the input was: the parser turned escapes into UTF-8 bytes on
	 the way in, and turning them back into escapes here would double the
	 size of every document not written in English to no purpose.

	 WHEN THE INPUT WAS, and that was taken on trust.  A String in this
	 image is bytes, and a byte from 128 to 255 was written out as itself
	 with nothing asked about it, so a String holding the single Latin-1
	 byte for `e-acute' -- 0xE9, which any C library or file read as
	 Latin-1 produces -- became a JSON document that is not UTF-8 and that
	 no conforming reader will accept, silently (Bugs4).  RFC 8259 says a
	 document exchanged between systems is UTF-8; there is no encoding
	 field and no way to say otherwise.  So each sequence is checked as it
	 is written, which costs nothing for ASCII and one comparison per byte
	 for the rest, and bytes that are not a sequence are refused with the
	 same argument the non-finite Floats above are refused with: writing
	 them anyway produces a document nothing else will read.  A String that
	 really holds bytes rather than text belongs in a JSON document base64,
	 which is what lib/Crypto's Base64 is for."

	| i size character code |
	stream nextPut: $".
	i := 1.
	size := aString size.
	[i <= size] whileTrue: [
		character := aString at: i.
		code := character asInteger.
		code > 127
			ifTrue: [i := self writeUtf8: aString at: i]
			ifFalse: [
				i := i + 1.
				character = $" ifTrue: [stream nextPutAll: '\"'] ifFalse: [
				character = $\ ifTrue: [stream nextPutAll: '\\'] ifFalse: [
				code = 8 ifTrue: [stream nextPutAll: '\b'] ifFalse: [
				code = 9 ifTrue: [stream nextPutAll: '\t'] ifFalse: [
				code = 10 ifTrue: [stream nextPutAll: '\n'] ifFalse: [
				code = 12 ifTrue: [stream nextPutAll: '\f'] ifFalse: [
				code = 13 ifTrue: [stream nextPutAll: '\r'] ifFalse: [
				code < 32
					ifTrue: [self writeUnicodeEscape: code]
					ifFalse: [stream nextPut: character]]]]]]]]]].
	stream nextPut: $"
]

{ #category : 'private' }
JSONWriter >> writeUtf8: aString at: anIndex [
	"The one UTF-8 sequence beginning at anIndex, written as the bytes it
	 is; answer the index after it.  Raise if the bytes there are not a
	 sequence.

	 RFC 3629's grammar and not `a lead byte plus n continuations': the
	 ranges below are what exclude the three things a naive check lets
	 through, each of which is a real security hazard rather than a
	 pedantry.  An OVERLONG form -- C0 80 for a NUL, which is two bytes
	 that decode to a character one byte can hold -- is how a filter that
	 looks for a byte gets past.  A SURROGATE half, ED A0 80, is a code
	 point UTF-8 has no encoding for and UTF-16 readers turn into
	 something else.  And anything above U+10FFFF, F5 and up, is not a
	 character at all."

	| lead length second i code |
	lead := (aString at: anIndex) asInteger.
	lead < 194 ifTrue: [^self notUtf8At: anIndex in: aString].
	lead < 224 ifTrue: [length := 2] ifFalse: [
	lead < 240 ifTrue: [length := 3] ifFalse: [
	lead < 245 ifTrue: [length := 4] ifFalse: [
		^self notUtf8At: anIndex in: aString]]].
	anIndex + length - 1 > aString size ifTrue: [^self notUtf8At: anIndex in: aString].
	second := (aString at: anIndex + 1) asInteger.
	"The second byte's range is narrower than 80..BF for four lead bytes,
	 and that narrowing is the whole of the overlong and surrogate rules."
	(lead = 224 and: [second < 160]) ifTrue: [^self notUtf8At: anIndex in: aString].
	(lead = 237 and: [second > 159]) ifTrue: [^self notUtf8At: anIndex in: aString].
	(lead = 240 and: [second < 144]) ifTrue: [^self notUtf8At: anIndex in: aString].
	(lead = 244 and: [second > 143]) ifTrue: [^self notUtf8At: anIndex in: aString].
	i := anIndex + 1.
	[i < (anIndex + length)] whileTrue: [
		code := (aString at: i) asInteger.
		(code < 128 or: [code > 191]) ifTrue: [^self notUtf8At: anIndex in: aString].
		i := i + 1].
	stream nextPutAll: (aString copyFrom: anIndex to: anIndex + length - 1).
	^anIndex + length
]

{ #category : 'private' }
JSONWriter >> notUtf8At: anIndex in: aString [
	"The refusal, naming the byte and where it is -- which is what somebody
	 holding a String that came out of a file needs in order to find out
	 what encoding the file was in."
	| code |
	code := (aString at: anIndex) asInteger.
	^JSONError signal: 'the byte ', (code printString: 16), ' at position ',
		anIndex printString,
		' of a String is not UTF-8, which is the only encoding JSON has'
]

{ #category : 'private' }
JSONWriter >> writeUnicodeEscape: aCode [
	"The \u form, for the control characters that have no short escape.

	 The four digits are indexed out of a literal rather than printed with
	 printOn:base:, which pads with nothing and would write \u0 for a null."

	| digits |
	digits := '0123456789abcdef'.
	stream nextPutAll: '\u'.
	stream nextPut: (digits at: ((aCode bitShift: -12) bitAnd: 15) + 1).
	stream nextPut: (digits at: ((aCode bitShift: -8) bitAnd: 15) + 1).
	stream nextPut: (digits at: ((aCode bitShift: -4) bitAnd: 15) + 1).
	stream nextPut: (digits at: (aCode bitAnd: 15) + 1)
]

{ #category : 'private' }
JSONWriter >> writeNumber: aNumber [
	aNumber isInteger ifTrue: [^stream nextPutAll: aNumber printString].
	(aNumber isKindOf: Fraction) ifTrue: [^self writeFraction: aNumber].
	^self writeFloat: aNumber
]

{ #category : 'private' }
JSONWriter >> writeFloat: aFloat [
	"A Float, having first established that it is a number at all.

	 `aFloat - aFloat = 0' is false for an infinity and false for a NaN and
	 true for everything else, which is the whole test in one line and needs
	 no isNaN and no isInfinite -- neither of which this image has."

	(aFloat - aFloat) = 0
		ifFalse: [^JSONError signal:
			'JSON has no way to write ', aFloat printString].
	stream nextPutAll: aFloat printString
]

{ #category : 'private' }
JSONWriter >> writeFraction: aFraction [
	"A Fraction as an exact decimal, when it has one.

	 A fraction in lowest terms has a finite decimal expansion exactly when
	 its denominator is a product of twos and fives, and the number of
	 places needed is the larger of the two counts.  Everything a JSON
	 document can contain qualifies -- the parser builds these by dividing
	 by a power of ten -- so the fallback below is for fractions a caller
	 computed, and it is the only lossy line in this class."

	| denominator twos fives places scaled digits point |
	denominator := aFraction denominator.
	twos := 0.
	fives := 0.
	[denominator \\ 2 = 0] whileTrue: [
		denominator := denominator // 2.
		twos := twos + 1].
	[denominator \\ 5 = 0] whileTrue: [
		denominator := denominator // 5.
		fives := fives + 1].
	denominator = 1 ifFalse: [^self writeFloat: aFraction asFloat].
	places := twos max: fives.
	scaled := aFraction numerator * (10 raisedTo: places)
		// aFraction denominator.
	scaled < 0 ifTrue: [stream nextPut: $-].
	digits := scaled abs printString.
	digits size <= places ifTrue: [
		digits := (String new: places - digits size + 1 withAll: $0), digits].
	point := digits size - places.
	stream nextPutAll: (digits copyFrom: 1 to: point).
	places > 0 ifTrue: [
		stream nextPut: $.;
			nextPutAll: (digits copyFrom: point + 1 to: digits size)]
]

{ #category : 'private' }
JSONWriter >> enter [
	"One level further in, or a refusal.

	 The level is kept for the indent, and this costs one comparison to make
	 it do a second job: a JSONObject put inside itself is written forever,
	 and forever here means allocating contexts on the heap until the machine
	 stops rather than overflowing a stack somebody would notice.  Nothing a
	 document can contain reaches 512 -- JSONParser refuses to READ deeper
	 than that -- so anything that gets here was built in the image, and a
	 structure built in the image that is 512 deep is a cycle."

	level := level + 1.
	level > 512 ifTrue: [
		JSONError signal: 'this value nests more than 512 levels deep, ',
			'which usually means it contains itself']
]

{ #category : 'private' }
JSONWriter >> leave [
	level := level - 1
]

{ #category : 'private' }
JSONWriter >> break [
	"A line and the indent for the level, or nothing at all when the output
	 is compact.  Every place a pretty printer would break is a place the
	 compact one writes nothing, so both forms come out of one pass."

	indent = 0 ifTrue: [^self].
	stream nextPut: Character nl.
	(indent * level) timesRepeat: [stream nextPut: $ ]
]

{ #category : 'private' }
JSONWriter >> setStream: aStream indent: anInteger [
	stream := aStream.
	indent := anInteger.
	level := 0
]

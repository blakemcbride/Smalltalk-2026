"
Reading JSON, strictly.

RFC 8259 and nothing else.  Not org.json's grammar, which is what Kiss
inherits and which forgives a great deal: there a name may be unquoted, a
string may be in single quotes, a comma may trail the last member, and a
bare word that is not true, false or null becomes a String.  Every one of
those accepts a document that the next program in the pipeline will reject,
and the complaint then arrives from somewhere that has never heard of this
one.  A parser is the right place to be unpleasant, and the sender of a
malformed document is the party who can fix it.

NUMBERS ARE EXACT.  1.5 reads as the Fraction 3/2, not as a Float, and an
exponent scales by an exact power of ten.  This is the decision lib/Database
made for DECIMAL columns and it is made here for the same reason and with
more force, because JSON is what money travels in now: 0.1 read through a
Float is already not one tenth, and the error surfaces as a penny three
statements downstream rather than as a failure here.  floatAt: is how a
caller asks for a Float, and asking is then a decision somebody made rather
than something the parser did to them.

A whole number answers an Integer whatever it was written as, since
Smalltalk's Fraction reduces: 1e2 is 100, and 3.0 is 3.  Everything
downstream that asks isKindOf: Integer depends on that.

TEXT IS UTF-8.  A String here is bytes, so an escape has to become bytes:
\u0041 answers the one byte that is A, and \u00e9 answers the TWO that are
e-acute in UTF-8.  The answer is therefore a String this system can hold,
compare and write to a file, and not one whose size is the number of
characters the sender thinks it sent.  A surrogate pair is read as the one
character it encodes; a lone surrogate is refused, because there is no
encoding of one.

\n IS A LINE FEED, 10, and not the carriage return this system's Paragraph
breaks lines on.  Translating it would be indefensible in both directions:
the document said 10, and a document written back out after translation
would differ from the one that came in.  Text meant for a text pane needs
translating by whatever puts it there, which is where the FileModel and
TextFileStream in lib/Files-Fixes already do it.

DEPTH IS BOUNDED.  Forty thousand open brackets is a valid document and a
recursive descent parser meets it with forty thousand activations.  A
document arriving over a network is written by somebody else, so maxDepth
stops that at 512 with a message that says what happened.  A document that
legitimately nests deeper is not one this parser was written for; set
maxDepth: higher on purpose.

ONE PARSER IS ONE PARSE.  All of my state is a stream and a depth, so
JSONParser parse: makes one of me per call and two processes parsing two
documents share nothing.  Sharing one parser between two processes is a
program error and this class does not try to make it work -- there is
nothing it could mean.  The objects I BUILD are another matter: a
JSONObject and a JSONArray each carry a lock, so the document is safe to
hand to another process the moment it is answered.

THE EXPONENT IS BOUNDED for the same reason, at 4096.  1e4096 is a
four-thousand-digit integer and exact arithmetic will happily produce it;
1e1000000000 is a twelve-character document asking for a billion digits,
and refusing it is the only way not to answer.
"
Class {
	#name : 'JSONParser',
	#superclass : 'Object',
	#instVars : [ 'stream', 'depth', 'maxDepth' ],
	#category : 'Json'
}

{ #category : 'parsing' }
JSONParser class >> parse: aStringOrStream [
	"The one JSON value that text is, whatever kind it is.

	 Text with anything after the value is an error -- see parse -- because
	 `{""a"":1} {""b"":2}' is two documents and answering the first of them
	 silently is how half a message gets processed."

	^(self on: aStringOrStream) parse
]

{ #category : 'parsing' }
JSONParser class >> parseObject: aStringOrStream [
	| value |
	value := self parse: aStringOrStream.
	(value isKindOf: JSONObject) ifFalse: [
		^JSONError signal: 'the document is not a JSON object but ',
			value class name article, ' ', value class name].
	^value
]

{ #category : 'parsing' }
JSONParser class >> parseArray: aStringOrStream [
	| value |
	value := self parse: aStringOrStream.
	(value isKindOf: JSONArray) ifFalse: [
		^JSONError signal: 'the document is not a JSON array but ',
			value class name article, ' ', value class name].
	^value
]

{ #category : 'instance creation' }
JSONParser class >> on: aStringOrStream [
	^self new setStream: aStringOrStream
]

{ #category : 'initialization' }
JSONParser >> initialize [
	depth := 0.
	maxDepth := 512
]

{ #category : 'accessing' }
JSONParser >> maxDepth [
	^maxDepth
]

{ #category : 'accessing' }
JSONParser >> maxDepth: anInteger [
	maxDepth := anInteger
]

{ #category : 'parsing' }
JSONParser >> parse [
	"The whole of what I was given, as one JSON value."

	| value |
	value := self nextValue.
	self skipWhitespace.
	stream atEnd ifFalse: [
		self error: 'there is more text after the value'].
	^value
]

{ #category : 'parsing' }
JSONParser >> nextValue [
	"One value, leaving the stream where it ends.

	 Separate from parse so that a stream carrying several documents one
	 after another can be read a document at a time, which is what a log
	 file of them is."

	| character |
	self skipWhitespace.
	stream atEnd ifTrue: [
		self error: 'the text ended where a value was expected'].
	character := stream peek.
	character = ${ ifTrue: [^self nextObject].
	character = $[ ifTrue: [^self nextArray].
	character = $" ifTrue: [^self nextString].
	character = $t ifTrue: [^self nextWord: 'true' value: true].
	character = $f ifTrue: [^self nextWord: 'false' value: false].
	character = $n ifTrue: [^self nextWord: 'null' value: nil].
	(character = $- or: [character isDigit]) ifTrue: [^self nextNumber].
	stream next.
	^self error: 'a value cannot begin with ', character printString
]

{ #category : 'parsing' }
JSONParser >> nextObject [
	| object name |
	object := JSONObject new.
	self enter.
	stream next.
	self skipWhitespace.
	(stream peekFor: $}) ifTrue: [self leave. ^object].
	[self skipWhitespace.
	 (stream atEnd not and: [stream peek = $"])
		ifFalse: [self error: 'a name must be a quoted string'].
	 name := self nextString.
	 self skipWhitespace.
	 (stream peekFor: $:)
		ifFalse: [self error: 'a name must be followed by a colon'].
	 "Last one wins, which is what every other reader does with a repeated
	  name.  RFC 8259 says the behaviour is undefined and warns against
	  sending such a document at all; agreeing with the majority is worth
	  more here than being clever."
	 object at: name put: self nextValue.
	 self skipWhitespace.
	 stream peekFor: $,] whileTrue.
	(stream peekFor: $})
		ifFalse: [self error: 'expected a comma or a closing brace'].
	self leave.
	^object
]

{ #category : 'parsing' }
JSONParser >> nextArray [
	| array |
	array := JSONArray new.
	self enter.
	stream next.
	self skipWhitespace.
	(stream peekFor: $]) ifTrue: [self leave. ^array].
	[array add: self nextValue.
	 self skipWhitespace.
	 stream peekFor: $,] whileTrue.
	(stream peekFor: $])
		ifFalse: [self error: 'expected a comma or a closing bracket'].
	self leave.
	^array
]

{ #category : 'parsing' }
JSONParser >> nextString [
	| out character |
	stream next.
	out := WriteStream on: (String new: 16).
	[stream atEnd ifTrue: [self error: 'the text ended inside a string'].
	 character := stream next.
	 character = $"] whileFalse: [
		character = $\
			ifTrue: [self nextEscapeOn: out]
			ifFalse: [
				character asInteger < 32 ifTrue: [
					self error: 'a control character in a string must be ',
						'written as an escape'].
				out nextPut: character]].
	^out contents
]

{ #category : 'parsing' }
JSONParser >> nextNumber [
	"The strict grammar: an optional minus, then either 0 or a digit from 1
	 to 9 and the rest, then an optional fraction, then an optional
	 exponent.

	 The leading zero is refused on purpose.  01 is not JSON, and every
	 language that reads it anyway disagrees with the next one about
	 whether it is one or an octal -- which is a real class of bug in
	 configuration files and costs nothing to shut out here."

	| negative whole fraction scale exponent value |
	negative := stream peekFor: $-.
	(stream atEnd not and: [stream peek isDigit])
		ifFalse: [self error: 'a number needs at least one digit'].
	(stream peek = $0)
		ifTrue: [
			stream next.
			whole := 0.
			(stream atEnd not and: [stream peek isDigit]) ifTrue: [
				self error: 'a number may not have a leading zero']]
		ifFalse: [whole := self nextDigits].
	value := whole.
	(stream peekFor: $.) ifTrue: [
		(stream atEnd not and: [stream peek isDigit])
			ifFalse: [self error: 'a decimal point needs a digit after it'].
		fraction := 0.
		scale := 1.
		[stream atEnd not and: [stream peek isDigit]] whileTrue: [
			fraction := fraction * 10 + stream next digitValue.
			scale := scale * 10].
		value := value + (fraction / scale)].
	(stream atEnd not and: [stream peek = $e or: [stream peek = $E]])
		ifTrue: [
			stream next.
			exponent := self nextExponent.
			value := exponent < 0
				ifTrue: [value / (10 raisedTo: exponent negated)]
				ifFalse: [value * (10 raisedTo: exponent)]].
	^negative ifTrue: [value negated] ifFalse: [value]
]

{ #category : 'private' }
JSONParser >> nextDigits [
	"The run of digits at the stream, as an Integer.  At least one digit is
	 the caller's to have checked."

	| value |
	value := 0.
	[stream atEnd not and: [stream peek isDigit]]
		whileTrue: [value := value * 10 + stream next digitValue].
	^value
]

{ #category : 'private' }
JSONParser >> nextExponent [
	"The signed integer after the e, bounded.

	 4096 is not a limit of the arithmetic -- it is exact and would carry on
	 -- it is a limit on what a twelve-character document may ask this
	 process to compute.  1e1000000000 is such a document."

	| negative value |
	negative := false.
	(stream peekFor: $-) ifTrue: [negative := true]
		ifFalse: [stream peekFor: $+].
	(stream atEnd not and: [stream peek isDigit])
		ifFalse: [self error: 'an exponent needs at least one digit'].
	value := self nextDigits.
	value > 4096 ifTrue: [
		self error: 'an exponent of ', value printString,
			' is larger than this parser will compute'].
	^negative ifTrue: [value negated] ifFalse: [value]
]

{ #category : 'private' }
JSONParser >> nextWord: aWord value: aValue [
	"true, false and null are spelled exactly and in lower case.  Reading
	 them character by character rather than by length means `tru]' fails
	 where it goes wrong rather than reading past the end of the array."

	aWord do: [:each |
		(stream peekFor: each) ifFalse: [
			self error: 'expected ', aWord]].
	^aValue
]

{ #category : 'private' }
JSONParser >> nextEscapeOn: aStream [
	| character |
	stream atEnd ifTrue: [self error: 'the text ended inside an escape'].
	character := stream next.
	character = $" ifTrue: [^aStream nextPut: $"].
	character = $\ ifTrue: [^aStream nextPut: $\].
	character = $/ ifTrue: [^aStream nextPut: $/].
	character = $b ifTrue: [^aStream nextPut: (Character value: 8)].
	character = $f ifTrue: [^aStream nextPut: (Character value: 12)].
	character = $n ifTrue: [^aStream nextPut: (Character value: 10)].
	character = $r ifTrue: [^aStream nextPut: (Character value: 13)].
	character = $t ifTrue: [^aStream nextPut: (Character value: 9)].
	character = $u ifTrue: [^self nextUnicodeOn: aStream].
	^self error: 'a backslash may not be followed by ', character printString
]

{ #category : 'private' }
JSONParser >> nextUnicodeOn: aStream [
	"One \u escape, and the second half of a surrogate pair if this was the
	 first half.

	 JSON inherited UTF-16's surrogates from JavaScript, so a character
	 outside the basic plane -- every emoji, and a great deal of Asian text
	 -- arrives as TWO escapes that mean one character.  Decoding them
	 separately produces two three-byte sequences that are not valid UTF-8
	 and that no other program will read back."

	| code low |
	code := self nextHex4.
	(code between: 16rD800 and: 16rDBFF) ifTrue: [
		((stream peekFor: $\) and: [stream peekFor: $u]) ifFalse: [
			self error: 'a high surrogate must be followed by a low one'].
		low := self nextHex4.
		(low between: 16rDC00 and: 16rDFFF) ifFalse: [
			self error: 'a high surrogate must be followed by a low one'].
		code := 16r10000 + ((code - 16rD800) * 1024) + (low - 16rDC00)].
	(code between: 16rDC00 and: 16rDFFF) ifTrue: [
		self error: 'a low surrogate with no high surrogate before it'].
	^self nextPutCodePoint: code on: aStream
]

{ #category : 'private' }
JSONParser >> nextHex4 [
	"Exactly four hexadecimal digits.  digitValue answers -1 for anything
	 that is not one, and for a lower-case letter too -- 1983's answers 10
	 to 35 for $A to $Z and nothing for $a to $z -- so each is raised to
	 upper case first."

	| value digit |
	value := 0.
	4 timesRepeat: [
		stream atEnd ifTrue: [
			self error: 'the text ended inside a \u escape'].
		digit := stream next asUppercase digitValue.
		(digit between: 0 and: 15) ifFalse: [
			self error: 'a \u escape needs four hexadecimal digits'].
		value := value * 16 + digit].
	^value
]

{ #category : 'private' }
JSONParser >> nextPutCodePoint: aCode on: aStream [
	"That code point as UTF-8, one byte at a time.

	 Written out rather than borrowed because this system has no UTF-8
	 anywhere else to borrow from: a String is bytes and everything that
	 has needed one so far has been ASCII."

	aCode < 128 ifTrue: [^aStream nextPut: (Character value: aCode)].
	aCode < 2048 ifTrue: [
		aStream nextPut: (Character value: 192 + (aCode bitShift: -6)).
		^aStream nextPut: (Character value: 128 + (aCode bitAnd: 63))].
	aCode < 65536 ifTrue: [
		aStream nextPut: (Character value: 224 + (aCode bitShift: -12)).
		aStream nextPut:
			(Character value: 128 + ((aCode bitShift: -6) bitAnd: 63)).
		^aStream nextPut: (Character value: 128 + (aCode bitAnd: 63))].
	aStream nextPut: (Character value: 240 + (aCode bitShift: -18)).
	aStream nextPut: (Character value: 128 + ((aCode bitShift: -12) bitAnd: 63)).
	aStream nextPut: (Character value: 128 + ((aCode bitShift: -6) bitAnd: 63)).
	^aStream nextPut: (Character value: 128 + (aCode bitAnd: 63))
]

{ #category : 'private' }
JSONParser >> skipWhitespace [
	"The four characters JSON calls whitespace and no others.  A vertical
	 tab or a form feed between two members is not JSON, however much it
	 looks like whitespace to a person."

	| character |
	[stream atEnd] whileFalse: [
		character := stream peek.
		(character = $  or: [character asInteger = 9
			or: [character asInteger = 10 or: [character asInteger = 13]]])
				ifFalse: [^self].
		stream next]
]

{ #category : 'private' }
JSONParser >> enter [
	"One level further in, or a refusal."

	depth := depth + 1.
	depth > maxDepth ifTrue: [
		self error: 'the document nests deeper than ', maxDepth printString,
			' levels']
]

{ #category : 'private' }
JSONParser >> leave [
	depth := depth - 1
]

{ #category : 'private' }
JSONParser >> setStream: aStringOrStream [
	"A String is wrapped; anything else is taken to be a stream already,
	 which is what lets a file be parsed without being read into memory."

	stream := aStringOrStream isString
		ifTrue: [ReadStream on: aStringOrStream]
		ifFalse: [aStringOrStream]
]

{ #category : 'private' }
JSONParser >> error: aMessage [
	"Every refusal comes through here, so that every one of them carries
	 the position.  See JSONError."

	^JSONError signal: aMessage, ' at character ', stream position printString
]

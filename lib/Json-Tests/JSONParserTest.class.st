"
What the parser reads and, more of the time, what it refuses.

The refusals are half the suite and they are the half worth having.  A
parser that accepts too much is not caught by any test of a valid document
-- every one of them still passes -- so the only way the leniency this
package deliberately does not have can be checked is by writing down the
documents it must not accept.  Each of these is something org.json reads
happily.

The exactness tests are the other half worth having, for the reason
DbValueTest gives about DECIMAL columns: 0.1 read as a Float is wrong in a
way that stays right to six figures and then loses a penny.
"
Class {
	#name : 'JSONParserTest',
	#superclass : 'TestCase',
	#category : 'Json-Tests'
}

{ #category : 'tests - values' }
JSONParserTest >> testEmptyObjectAndArray [
	self assert: (JSONParser parse: '{}') equals: JSONObject new.
	self assert: (JSONParser parse: '[]') equals: JSONArray new
]

{ #category : 'tests - values' }
JSONParserTest >> testFlatObject [
	| object |
	object := JSONParser parse: '{"a": 1, "b": "two", "c": true}'.
	self assert: object size equals: 3.
	self assert: (object at: 'a') equals: 1.
	self assert: (object at: 'b') equals: 'two'.
	self assert: (object at: 'c') equals: true
]

{ #category : 'tests - values' }
JSONParserTest >> testNesting [
	| object |
	object := JSONParser parse: '{"a": {"b": [1, [2, {"c": null}]]}}'.
	self assert: ((object objectAt: 'a') arrayAt: 'b') size equals: 2.
	self assert: (((object objectAt: 'a') arrayAt: 'b') at: 1) equals: 1.
	self assert: ((((object objectAt: 'a') arrayAt: 'b') arrayAt: 2)
		objectAt: 2) size equals: 1
]

{ #category : 'tests - values' }
JSONParserTest >> testTopLevelValues [
	"RFC 8259 made any value a document; the 2006 grammar allowed only an
	 object or an array.  A reader that still insists on the old rule
	 cannot read the body of half the REST calls written since."

	self assert: (JSONParser parse: '42') equals: 42.
	self assert: (JSONParser parse: '"text"') equals: 'text'.
	self assert: (JSONParser parse: 'true') equals: true.
	self assert: (JSONParser parse: 'false') equals: false.
	self assert: (JSONParser parse: 'null') equals: nil
]

{ #category : 'tests - values' }
JSONParserTest >> testWhitespaceIsIgnored [
	self assert: (JSONParser parse: '  {  "a"  :  [ 1 , 2 ]  }  ')
		equals: (JSONParser parse: '{"a":[1,2]}')
]

{ #category : 'tests - values' }
JSONParserTest >> testDuplicateNameKeepsTheLast [
	"Undefined by the RFC, and every other reader keeps the last."

	self assert: ((JSONParser parse: '{"a":1,"a":2}') at: 'a') equals: 2
]

{ #category : 'tests - numbers' }
JSONParserTest >> testIntegers [
	self assert: (JSONParser parse: '0') equals: 0.
	self assert: (JSONParser parse: '-0') equals: 0.
	self assert: (JSONParser parse: '7') equals: 7.
	self assert: (JSONParser parse: '-7') equals: -7.
	self assert: ((JSONParser parse: '7') isKindOf: Integer)
]

{ #category : 'tests - numbers' }
JSONParserTest >> testLargeIntegersDoNotOverflow [
	"A JSON integer has no width.  Reading one through a double, which is
	 what a C or a JavaScript reader does, loses the low digits of anything
	 past 2^53 -- and an identifier from a database sequence is exactly the
	 kind of number that grows past it."

	self assert: (JSONParser parse: '123456789012345678901234567890')
		equals: 123456789012345678901234567890
]

{ #category : 'tests - numbers' }
JSONParserTest >> testDecimalsAreExact [
	"The reason this parser answers Fractions.  A tenth is not a Float."

	self assert: (JSONParser parse: '0.1') * 10 equals: 1.
	self assert: ((JSONParser parse: '0.1') isKindOf: Fraction).
	self assert: (JSONParser parse: '1.5') equals: 3/2.
	self assert: (JSONParser parse: '-1.25') equals: -5/4
]

{ #category : 'tests - numbers' }
JSONParserTest >> testWholeDecimalsAnswerIntegers [
	"3.0 is three.  A Fraction with a denominator of one would make every
	 isKindOf: Integer downstream answer false; Smalltalk's arithmetic
	 reduces, and this records that the parser leans on it."

	self assert: ((JSONParser parse: '3.0') isKindOf: Integer).
	self assert: ((JSONParser parse: '1e2') isKindOf: Integer).
	self assert: (JSONParser parse: '1e2') equals: 100
]

{ #category : 'tests - numbers' }
JSONParserTest >> testExponents [
	self assert: (JSONParser parse: '1e2') equals: 100.
	self assert: (JSONParser parse: '1E2') equals: 100.
	self assert: (JSONParser parse: '1e+2') equals: 100.
	self assert: (JSONParser parse: '1e-2') equals: 1/100.
	self assert: (JSONParser parse: '1.5e3') equals: 1500.
	self assert: (JSONParser parse: '-2.5e-1') equals: -1/4
]

{ #category : 'tests - refusals' }
JSONParserTest >> testLeadingZeroIsRefused [
	self should: [JSONParser parse: '01'] raise: JSONError.
	self should: [JSONParser parse: '-01'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testIncompleteNumbersAreRefused [
	self should: [JSONParser parse: '1.'] raise: JSONError.
	self should: [JSONParser parse: '.5'] raise: JSONError.
	self should: [JSONParser parse: '1e'] raise: JSONError.
	self should: [JSONParser parse: '-'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testTrailingCommaIsRefused [
	"org.json forgives both of these, and a document with one in it is
	 rejected by every JavaScript parser it will ever reach."

	self should: [JSONParser parse: '{"a":1,}'] raise: JSONError.
	self should: [JSONParser parse: '[1,2,]'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testUnquotedNameIsRefused [
	self should: [JSONParser parse: '{a:1}'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testSingleQuotesAreRefused [
	self should: [JSONParser parse: '{''a'':1}'] raise: JSONError.
	self should: [JSONParser parse: '''a'''] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testTrailingTextIsRefused [
	"Two documents in a row, which is a whole message being processed as
	 half of one if the second is dropped in silence."

	self should: [JSONParser parse: '{"a":1} {"b":2}'] raise: JSONError.
	self should: [JSONParser parse: '1 2'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testTruncatedDocumentIsRefused [
	self should: [JSONParser parse: '{"a":'] raise: JSONError.
	self should: [JSONParser parse: '[1,2'] raise: JSONError.
	self should: [JSONParser parse: '"unfinished'] raise: JSONError.
	self should: [JSONParser parse: ''] raise: JSONError.
	self should: [JSONParser parse: '   '] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testLiteralsAreLowerCase [
	self should: [JSONParser parse: 'True'] raise: JSONError.
	self should: [JSONParser parse: 'NULL'] raise: JSONError.
	self should: [JSONParser parse: 'tru'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testBareWordIsRefused [
	"org.json answers the String 'banana' for this."

	self should: [JSONParser parse: 'banana'] raise: JSONError.
	self should: [JSONParser parse: '{"a":banana}'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testControlCharacterInAStringIsRefused [
	"A raw newline inside a string is not JSON, however ordinary it looks
	 in a source file."

	self should: [JSONParser parse: (String with: $" with: Character nl
		with: $")] raise: JSONError.
	self should: [JSONParser parse: (String with: $" with: Character tab
		with: $")] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testCommentsAreRefused [
	"JSON has no comments.  Several readers accept them anyway, which is
	 how a configuration file becomes unreadable to the next tool."

	self should: [JSONParser parse: '{"a":1} // done'] raise: JSONError.
	self should: [JSONParser parse: '/* hello */ 1'] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONParserTest >> testWrongKindOfDocumentIsRefused [
	self should: [JSONObject fromString: '[1]'] raise: JSONError.
	self should: [JSONArray fromString: '{"a":1}'] raise: JSONError.
	self should: [JSONObject fromString: '42'] raise: JSONError
]

{ #category : 'tests - strings' }
JSONParserTest >> testShortEscapes [
	| text |
	text := JSONParser parse: '"a\"b\\c\/d\be\ff\ng\rh\ti"'.
	self assert: (text at: 2) equals: $".
	self assert: (text at: 4) equals: $\.
	self assert: (text at: 6) equals: $/.
	self assert: (text at: 8) asInteger equals: 8.
	self assert: (text at: 10) asInteger equals: 12.
	self assert: (text at: 12) asInteger equals: 10.
	self assert: (text at: 14) asInteger equals: 13.
	self assert: (text at: 16) asInteger equals: 9
]

{ #category : 'tests - strings' }
JSONParserTest >> testUnknownEscapeIsRefused [
	self should: [JSONParser parse: '"\q"'] raise: JSONError.
	self should: [JSONParser parse: '"\"'] raise: JSONError
]

{ #category : 'tests - strings' }
JSONParserTest >> testUnicodeEscapeBecomesUtf8 [
	"One byte for A and two for e-acute, because a String here is bytes.
	 The size is the assertion that matters: a reader that put the code point
	 in one byte would answer 1 for both, and would then have written a
	 String no other program can read."

	self assert: (JSONParser parse: '"\u0041"') equals: 'A'.
	self assert: (JSONParser parse: '"\u00e9"') size equals: 2.
	self assert: ((JSONParser parse: '"\u00e9"') at: 1) asInteger equals: 195.
	self assert: ((JSONParser parse: '"\u00e9"') at: 2) asInteger equals: 169.
	self assert: (JSONParser parse: '"\u20ac"') size equals: 3
]

{ #category : 'tests - strings' }
JSONParserTest >> testHexDigitsMayBeEitherCase [
	"1983's digitValue answers 10 to 35 for $A to $Z and -1 for $a to $z, so
	 a reader that used it straight would refuse every escape written in the
	 lower case that JSON documents are full of."

	self assert: (JSONParser parse: '"\u00E9"')
		equals: (JSONParser parse: '"\u00e9"')
]

{ #category : 'tests - strings' }
JSONParserTest >> testSurrogatePair [
	"An emoji arrives as two escapes that mean one character.  Decoding them
	 separately produces six bytes that are not valid UTF-8 and that nothing
	 else will read back."

	self assert: (JSONParser parse: '"\ud83d\ude00"') size equals: 4.
	self assert: ((JSONParser parse: '"\ud83d\ude00"') at: 1) asInteger
		equals: 240
]

{ #category : 'tests - strings' }
JSONParserTest >> testLoneSurrogateIsRefused [
	self should: [JSONParser parse: '"\ud83d"'] raise: JSONError.
	self should: [JSONParser parse: '"\ude00"'] raise: JSONError.
	self should: [JSONParser parse: '"\ud83dx"'] raise: JSONError
]

{ #category : 'tests - strings' }
JSONParserTest >> testShortUnicodeEscapeIsRefused [
	self should: [JSONParser parse: '"\u12"'] raise: JSONError.
	self should: [JSONParser parse: '"\uzzzz"'] raise: JSONError
]

{ #category : 'tests - strings' }
JSONParserTest >> testHighBytesPassThrough [
	"Text that is already UTF-8 is not escaped by anybody and must arrive
	 as itself."

	| source |
	source := String with: $" with: (Character value: 195)
		with: (Character value: 169) with: $".
	self assert: (JSONParser parse: source) size equals: 2
]

{ #category : 'tests - limits' }
JSONParserTest >> testDepthIsBounded [
	"Forty thousand open brackets is a valid document and a recursive
	 descent parser meets it with forty thousand activations."

	self should: [JSONParser parse: (String new: 600 withAll: $[)]
		raise: JSONError.
	self shouldnt: [JSONParser parse: (String new: 20 withAll: $[),
		(String new: 20 withAll: $])] raise: JSONError
]

{ #category : 'tests - limits' }
JSONParserTest >> testDepthLimitCanBeRaised [
	| parser |
	parser := JSONParser on: (String new: 600 withAll: $[),
		(String new: 600 withAll: $]).
	parser maxDepth: 1000.
	self shouldnt: [parser parse] raise: JSONError
]

{ #category : 'tests - limits' }
JSONParserTest >> testHugeExponentIsRefused [
	"A twelve-character document asking for a billion digits."

	self should: [JSONParser parse: '1e99999'] raise: JSONError.
	self shouldnt: [JSONParser parse: '1e100'] raise: JSONError
]

{ #category : 'tests - limits' }
JSONParserTest >> testTooManyDigitsAreRefused [
	"Bugs3 B53.  Four thousand sevens took thirteen and a half seconds to
	 read, and a REST request carrying one spent every one of them before
	 the session in the same document had been looked at -- so an
	 unauthenticated client could hold a worker for as long as it liked.
	 The digits are counted before the arithmetic is done, so the refusal
	 costs nothing."

	self should: [JSONParser parse: (String new: 300 withAll: $7)] raise: JSONError.
	self should: [JSONParser parse: '0.', (String new: 300 withAll: $7)] raise: JSONError.
	self should: [JSONParser parse: '-', (String new: 300 withAll: $7)] raise: JSONError.
	self should: [JSONParser parse: '1e', (String new: 300 withAll: $0)] raise: JSONError.
	self shouldnt: [JSONParser parse: (String new: 256 withAll: $7)] raise: JSONError.
	self shouldnt: [JSONParser parse: '0.', (String new: 256 withAll: $7)] raise: JSONError
]

{ #category : 'tests - limits' }
JSONParserTest >> testTheDigitLimitCanBeRaised [
	| parser |
	parser := JSONParser on: (String new: 300 withAll: $7).
	parser maxDigits: 1000.
	self shouldnt: [parser parse] raise: JSONError.
	self assert: JSONParser new maxDigits equals: 256
]

{ #category : 'tests - limits' }
JSONParserTest >> testALongNumberIsStillExact [
	"The digits are read in SmallInteger chunks and combined rather than
	 one at a time; the answer must be the same number it always was, at
	 every length and either side of a chunk boundary."

	| digits |
	self assert: (JSONParser parse: '123456789012345678901234567890')
		equals: 123456789012345678901234567890.
	self assert: (JSONParser parse: '1.0000000000000000001')
		equals: 1 + (1 / 10000000000000000000).
	digits := String new: 200 withAll: $9.
	self assert: (JSONParser parse: digits) equals: (10 raisedTo: 200) - 1.
	self assert: (JSONParser parse: digits) printString equals: digits.
	1 to: 40 do: [:n | | text |
		text := (String new: n withAll: $1).
		self assert: (JSONParser parse: text) printString equals: text]
]

{ #category : 'tests - streams' }
JSONParserTest >> testAStreamCanBeReadAValueAtATime [
	"What a file of one document per line is, and the reason nextValue is
	 separate from parse."

	| parser |
	parser := JSONParser on: (ReadStream on: '[1] [2]').
	self assert: parser nextValue equals: (JSONArray with: 1).
	self assert: parser nextValue equals: (JSONArray with: 2)
]

{ #category : 'tests - errors' }
JSONParserTest >> testTheErrorSaysWhere [
	"The position is the whole reason JSONError exists as a class rather
	 than a plain Error, and a message without one sends the reader
	 looking through the document by hand."

	| message |
	message := [JSONParser parse: '{"a":1,"b":}'. nil]
		on: JSONError do: [:error | error messageText].
	self assert: (message includesSubstring: 'at character 12')
]

{ #category : 'tests - errors' }
JSONParserTest >> testWhatIsNotTextIsAJSONError [
	"Bugs4.  Anything that was not a String was taken for a stream on the
	 strength of it, so nil, an Integer and a ByteArray each got as far as
	 the first `atEnd' and raised a MessageNotUnderstood -- a message about
	 this parser's insides, from a caller who passed the wrong thing, where
	 every other refusal here is a JSONError that says what was wrong."

	self should: [JSONParser parse: nil] raise: JSONError.
	self should: [JSONParser parse: 42] raise: JSONError.
	self should: [JSONParser parse: #[1 2 3]] raise: JSONError.
	self should: [JSONObject fromString: 3] raise: JSONError.
	self assert: ([JSONParser parse: nil. nil] on: JSONError do: [:e | e messageText])
		equals: 'JSON is read from a String or a Stream, not an UndefinedObject'.
	"A real stream is still a stream, and so is a String."
	self assert: (JSONParser parse: (ReadStream on: '[1]')) equals: (JSONArray with: 1).
	self assert: (JSONParser parse: '[1]') equals: (JSONArray with: 1)
]

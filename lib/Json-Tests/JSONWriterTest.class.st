"
What comes out, and that it is the same as what went in.

The round trip tests are the ones that would catch a real regression: a
document read and written and read again has to be the same document, and
that one property covers the escaping, the number formatting and the
ordering at once.  The rest of the suite is here to say WHICH of those
broke when it does.
"
Class {
	#name : 'JSONWriterTest',
	#superclass : 'TestCase',
	#category : 'Json-Tests'
}

{ #category : 'tests - compact' }
JSONWriterTest >> testEmptyValues [
	self assert: JSONObject new asJsonString equals: '{}'.
	self assert: JSONArray new asJsonString equals: '[]'.
	self assert: nil asJsonString equals: 'null'.
	self assert: true asJsonString equals: 'true'.
	self assert: false asJsonString equals: 'false'
]

{ #category : 'tests - compact' }
JSONWriterTest >> testCompactHasNoSpaces [
	"Compact means compact: a space after the colon is what the indented
	 form is for, and a wire format that pays for pretty printing nobody
	 reads is a wire format that costs money at scale."

	self assert: (JSONParser parse: '{ "a" : [ 1 , 2 ] }') asJsonString
		equals: '{"a":[1,2]}'
]

{ #category : 'tests - compact' }
JSONWriterTest >> testNamesKeepTheirOrder [
	"1983's Dictionary iterates in hash order, so this is the test that
	 JSONObject keeps its own list of names.  Without it a document read
	 and written back comes out shuffled and two runs disagree."

	| object |
	object := JSONObject new.
	object at: 'zebra' put: 1; at: 'apple' put: 2; at: 'mango' put: 3.
	self assert: object asJsonString
		equals: '{"zebra":1,"apple":2,"mango":3}'
]

{ #category : 'tests - compact' }
JSONWriterTest >> testAnythingCanBeAskedForItsJson [
	"asJsonString is on Object, so nothing has to be built by hand first."

	self assert: #(1 2 3) asJsonString equals: '[1,2,3]'.
	self assert: 'hello' asJsonString equals: '"hello"'.
	self assert: 42 asJsonString equals: '42'.
	self assert: $x asJsonString equals: '"x"'.
	self assert: #symbol asJsonString equals: '"symbol"'.
	self assert: (1 to: 3) asJsonString equals: '[1,2,3]'
]

{ #category : 'tests - compact' }
JSONWriterTest >> testADictionaryIsAnObjectAndNotAnArray [
	"Collection>>do: over a Dictionary answers its VALUES, so without
	 Dictionary>>asJsonValue a Dictionary would have been written as an
	 array of its values with the names thrown away -- silently, and in
	 hash order."

	| dictionary |
	dictionary := Dictionary new.
	dictionary at: 'a' put: 1.
	self assert: dictionary asJsonString equals: '{"a":1}'
]

{ #category : 'tests - escaping' }
JSONWriterTest >> testQuoteAndBackslash [
	self assert: (String with: $") asJsonString size equals: 4.
	self assert: (String with: $\) asJsonString size equals: 4.
	self assert: 'a"b' asJsonString equals: (String with: $" with: $a
		with: $\ with: $" with: $b with: $")
]

{ #category : 'tests - escaping' }
JSONWriterTest >> testControlCharacters [
	self assert: (String with: Character tab) asJsonString equals: '"\t"'.
	self assert: (String with: Character nl) asJsonString equals: '"\n"'.
	self assert: (String with: Character cr) asJsonString equals: '"\r"'.
	self assert: (String with: (Character value: 8)) asJsonString
		equals: '"\b"'.
	self assert: (String with: (Character value: 12)) asJsonString
		equals: '"\f"'
]

{ #category : 'tests - escaping' }
JSONWriterTest >> testOtherControlCharactersUseTheLongEscape [
	"printOn:base: pads with nothing, so a writer that used it would write
	 \u0 for a null and produce a document nothing can read."

	self assert: (String with: (Character value: 0)) asJsonString
		equals: '"\u0000"'.
	self assert: (String with: (Character value: 31)) asJsonString
		equals: '"\u001f"'
]

{ #category : 'tests - escaping' }
JSONWriterTest >> testTheSolidusIsNotEscaped [
	"org.json writes \/ so that a document can be embedded in an HTML
	 script element without ending it early.  That is a hazard belonging to
	 whatever does the embedding, not to every document this system
	 writes."

	self assert: 'a/b' asJsonString equals: '"a/b"'
]

{ #category : 'tests - escaping' }
JSONWriterTest >> testHighBytesGoOutAsThemselves [
	"The parser turned escapes into UTF-8 bytes on the way in.  Turning
	 them back into escapes here would double the size of every document
	 not written in English, to no purpose: \u00e9 and the two bytes are
	 the same text to every reader."

	| text |
	text := JSONParser parse: '"\u00e9"'.
	self assert: text asJsonString size equals: 4
]

{ #category : 'tests - numbers' }
JSONWriterTest >> testIntegers [
	self assert: 0 asJsonString equals: '0'.
	self assert: -7 asJsonString equals: '-7'.
	self assert: 123456789012345678901234567890 asJsonString
		equals: '123456789012345678901234567890'
]

{ #category : 'tests - numbers' }
JSONWriterTest >> testFractionsBecomeExactDecimals [
	"A Fraction prints as (3/2), which is not JSON, so this class writes
	 the decimal itself.  Every number a document can contain has a finite
	 decimal, because the parser builds them by dividing by a power of
	 ten."

	self assert: (3/2) asJsonString equals: '1.5'.
	self assert: (1/10) asJsonString equals: '0.1'.
	self assert: (-1/10) asJsonString equals: '-0.1'.
	self assert: (1/4) asJsonString equals: '0.25'.
	self assert: (1/8) asJsonString equals: '0.125'.
	self assert: (1/1000) asJsonString equals: '0.001'
]

{ #category : 'tests - numbers' }
JSONWriterTest >> testAFractionWithNoDecimalBecomesAFloat [
	"Only a caller can have produced one; a third has to be written as
	 0.3333333333333333 or not at all, and not at all is worse."

	self assert: (1/3) asJsonString equals: (1/3) asFloat printString
]

{ #category : 'tests - numbers' }
JSONWriterTest >> testFloats [
	self assert: 2.5 asJsonString equals: '2.5'.
	self assert: -0.5 asJsonString equals: '-0.5'
]

{ #category : 'tests - numbers' }
JSONWriterTest >> testInfinityIsRefused [
	"JSON has no spelling for one.  Every library that writes Infinity
	 anyway produces a document nothing else will read, which is a worse
	 answer than the error."

	| huge |
	huge := 1.0e200.
	self should: [(huge * huge) asJsonString] raise: JSONError
]

{ #category : 'tests - indenting' }
JSONWriterTest >> testIndentedForm [
	| text |
	text := (JSONParser parse: '{"a":1,"b":[2]}') asJsonStringIndent: 2.
	self assert: text equals: '{', (String with: Character nl),
		'  "a": 1,', (String with: Character nl),
		'  "b": [', (String with: Character nl),
		'    2', (String with: Character nl),
		'  ]', (String with: Character nl), '}'
]

{ #category : 'tests - indenting' }
JSONWriterTest >> testIndentZeroIsCompact [
	| value |
	value := JSONParser parse: '{"a":[1,2]}'.
	self assert: (value asJsonStringIndent: 0) equals: value asJsonString
]

{ #category : 'tests - indenting' }
JSONWriterTest >> testEmptyContainersStayOnOneLine [
	"An empty object written over three lines is three lines saying
	 nothing, and every other pretty printer agrees."

	self assert: (JSONObject new asJsonStringIndent: 2) equals: '{}'.
	self assert: ((JSONParser parse: '{"a":[]}') asJsonStringIndent: 2)
		equals: '{', (String with: Character nl), '  "a": []',
			(String with: Character nl), '}'
]

{ #category : 'tests - indenting' }
JSONWriterTest >> testLinesBreakWithNlAndNotCr [
	"Character cr is 13 here because that is what it was on the Alto, and
	 nl is 10, which is what everything outside this image reads.  Writing
	 one and meaning the other makes the whole document a single line to a
	 reader that is not this system -- which is the fault
	 DbSchemaGraph>>storeOn: carries the same note about."

	| text |
	text := (JSONParser parse: '{"a":1}') asJsonStringIndent: 2.
	self assert: (text includes: Character nl).
	self deny: (text includes: Character cr)
]

{ #category : 'tests - round trip' }
JSONWriterTest >> testRoundTrip [
	"The one property that covers the escaping, the numbers and the order
	 at once."

	| source |
	source := '{"n":[1,-2.5,0.1,1000000000000000000000],"s":"a\"b\\c",',
		'"t":true,"f":false,"z":null,"o":{"k":[]}}'.
	self assert: (JSONParser parse: source) asJsonString equals: source
]

{ #category : 'tests - round trip' }
JSONWriterTest >> testIndentedFormReadsBackTheSame [
	| value |
	value := JSONParser parse: '{"a":[1,{"b":null}],"c":"x"}'.
	self assert: (JSONParser parse: (value asJsonStringIndent: 4))
		equals: value
]

{ #category : 'tests - refusals' }
JSONWriterTest >> testAnUnwritableObjectIsRefusedWhereItIsPut [
	"Not where the document is written, which is the whole reason the
	 conversion is a message and not a switch inside this class.  In a web
	 application the two moments are a request apart."

	self should: [JSONObject new at: 'a' put: 1@2] raise: JSONError.
	self should: [JSONArray new add: Object new] raise: JSONError
]

{ #category : 'tests - refusals' }
JSONWriterTest >> testAValueThatContainsItselfIsRefused [
	"Written straight through, this allocates contexts on the heap until
	 the machine stops -- there is no stack to overflow first."

	| object |
	object := JSONObject new.
	object at: 'me' put: object.
	self should: [object asJsonString] raise: JSONError
]

{ #category : 'tests - streams' }
JSONWriterTest >> testWritingStraightOntoAStream [
	"For the document too large to want a String of first."

	| out |
	out := WriteStream on: (String new: 16).
	out nextPutAll: 'body='.
	#(1 2) writeJsonOn: out.
	self assert: out contents equals: 'body=[1,2]'
]

{ #category : 'tests - printing' }
JSONWriterTest >> testPrintStringIsTheJson [
	"What the Inspector and the Transcript show.  `a JSONObject' tells a
	 reader nothing they cannot see from the braces."

	self assert: (JSONParser parse: '{"a":1}') printString
		equals: '{"a":1}'.
	self assert: (JSONParser parse: '[1]') printString equals: '[1]'
]

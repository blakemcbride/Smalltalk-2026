"
The three places JSONObject is deliberately not a Dictionary -- what it
will hold, what order it keeps, and what it says about a name that is not
there -- and the typed accessors on top of them.

The null-versus-absent tests are the ones worth reading.  Both answer nil
from at:, and there is no way to tell them apart except the two messages
tested here, so a change that broke that distinction would break nothing
else in this suite.
"
Class {
	#name : 'JSONObjectTest',
	#superclass : 'TestCase',
	#category : 'Json-Tests'
}

{ #category : 'tests - accessing' }
JSONObjectTest >> testAbsentNameAnswersNil [
	"Dictionary>>at: raises; this does not, because in JSON an absent
	 field is how optional is spelled and a reader that raised for one
	 would turn every read into at:ifAbsent:."

	self assert: (JSONObject new at: 'nobody') equals: nil.
	self assert: (JSONObject new at: 'nobody' ifAbsent: ['x']) equals: 'x'
]

{ #category : 'tests - accessing' }
JSONObjectTest >> testNullIsNotTheSameAsAbsent [
	"{""middleName"": null} says the sender knows about middle names and
	 this person has none.  A document without the name says the sender
	 never considered the question.  Some protocols mean different things
	 by those, and nil cannot tell them apart."

	| object |
	object := JSONParser parse: '{"here":null}'.
	self assert: (object at: 'here') equals: nil.
	self assert: (object at: 'gone') equals: nil.
	self assert: (object includesKey: 'here').
	self deny: (object includesKey: 'gone').
	self assert: (object isNullAt: 'here').
	self deny: (object isNullAt: 'gone')
]

{ #category : 'tests - accessing' }
JSONObjectTest >> testAtPutAnswersTheValue [
	"Every collection in the image answers the value from at:put:.
	 org.json answers the object so that puts can be chained, which is how
	 Java writes a cascade; Smalltalk has cascades."

	self assert: (JSONObject new at: 'a' put: 7) equals: 7
]

{ #category : 'tests - accessing' }
JSONObjectTest >> testNamesKeepTheOrderTheyWerePutIn [
	| object |
	object := JSONObject new.
	object at: 'c' put: 1; at: 'a' put: 2; at: 'b' put: 3.
	self assert: object keys asArray equals: #('c' 'a' 'b').
	object at: 'a' put: 9.
	self assert: object keys asArray equals: #('c' 'a' 'b')
]

{ #category : 'tests - accessing' }
JSONObjectTest >> testRemoveKey [
	"Both halves have to go.  A name left in the order list after its value
	 is gone is a name every enumeration trips over."

	| object |
	object := JSONParser parse: '{"a":1,"b":2}'.
	self assert: (object removeKey: 'a') equals: 1.
	self assert: object keys asArray equals: #('b').
	self assert: object size equals: 1.
	self assert: (object removeKey: 'gone') equals: nil.
	self assert: (object removeKey: 'gone' ifAbsent: ['x']) equals: 'x'
]

{ #category : 'tests - accessing' }
JSONObjectTest >> testAtIfAbsentPut [
	| object |
	object := JSONObject new.
	self assert: (object at: 'a' ifAbsentPut: [1]) equals: 1.
	self assert: (object at: 'a' ifAbsentPut: [2]) equals: 1.
	self assert: object size equals: 1
]

{ #category : 'tests - names' }
JSONObjectTest >> testASymbolIsTheSameNameAsItsString [
	| object |
	object := JSONObject new.
	object at: #alpha put: 1.
	self assert: (object at: 'alpha') equals: 1.
	self assert: object keys first class equals: String.
	self assert: object asJsonString equals: '{"alpha":1}'
]

{ #category : 'tests - names' }
JSONObjectTest >> testANameThatIsNotTextIsRefused [
	"JSON has no way to write one, so a number used as a name is a fault
	 to report where it happens rather than one for the writer to find."

	self should: [JSONObject new at: 3 put: 1] raise: JSONError.
	self should: [JSONObject new at: nil put: 1] raise: JSONError
]

{ #category : 'tests - conversion' }
JSONObjectTest >> testValuesAreConvertedGoingIn [
	"A Dictionary becomes an object, an Array becomes an array, and both
	 happen at the put rather than at the write."

	| object |
	object := JSONObject new.
	object at: 'd' put: (Dictionary new at: 'k' put: 1; yourself).
	object at: 'a' put: #(1 2).
	self assert: ((object at: 'd') isKindOf: JSONObject).
	self assert: ((object at: 'a') isKindOf: JSONArray).
	self assert: (object objectAt: 'd') asJsonString equals: '{"k":1}'
]

{ #category : 'tests - conversion' }
JSONObjectTest >> testAnUnconvertibleValueIsRefused [
	self should: [JSONObject new at: 'a' put: Object new] raise: JSONError
]

{ #category : 'tests - conversion' }
JSONObjectTest >> testWithAllTakesADictionary [
	| object |
	object := JSONObject withAll: (Dictionary new at: 'a' put: 1; yourself).
	self assert: object asJsonString equals: '{"a":1}'
]

{ #category : 'tests - conversion' }
JSONObjectTest >> testAsDictionaryIsOneLevelDeep [
	"A nested JSONObject stays one, so the answer is still something the
	 writer can write.  Turning the whole tree into Dictionaries cannot be
	 undone -- the order is gone -- and should be asked for a level at a
	 time."

	| object dictionary |
	object := JSONParser parse: '{"a":1,"b":{"c":2}}'.
	dictionary := object asDictionary.
	self assert: (dictionary isKindOf: Dictionary).
	self assert: (dictionary at: 'a') equals: 1.
	self assert: ((dictionary at: 'b') isKindOf: JSONObject)
]

{ #category : 'tests - typed' }
JSONObjectTest >> testTypedAccessors [
	| object |
	object := JSONParser parse:
		'{"s":"x","i":3,"f":1.5,"b":true,"o":{},"a":[],"z":null}'.
	self assert: (object stringAt: 's') equals: 'x'.
	self assert: (object integerAt: 'i') equals: 3.
	self assert: (object numberAt: 'f') equals: 3/2.
	self assert: (object floatAt: 'f') equals: 1.5.
	self assert: (object booleanAt: 'b') equals: true.
	self assert: ((object objectAt: 'o') isKindOf: JSONObject).
	self assert: ((object arrayAt: 'a') isKindOf: JSONArray).
	self assert: (object stringAt: 'z') equals: nil
]

{ #category : 'tests - typed' }
JSONObjectTest >> testTypedAccessorsAnswerNilOrTheBlockWhenAbsent [
	| object |
	object := JSONObject new.
	self assert: (object stringAt: 'gone') equals: nil.
	self assert: (object integerAt: 'gone' ifAbsent: [0]) equals: 0.
	self assert: (object arrayAt: 'gone' ifAbsent: [JSONArray new])
		equals: JSONArray new
]

{ #category : 'tests - typed' }
JSONObjectTest >> testTheWrongTypeRaisesRatherThanConverting [
	"org.json and Kiss both convert: getString of 42 answers '42' and
	 getLong of ""42"" answers 42.  That is the kind of helpfulness that
	 hides the sender's bug in the reader's code, and a document that says
	 42 and one that says ""42"" are different documents."

	| object |
	object := JSONParser parse: '{"n":42,"s":"42"}'.
	self should: [object stringAt: 'n'] raise: JSONError.
	self should: [object integerAt: 's'] raise: JSONError.
	self should: [object booleanAt: 'n'] raise: JSONError.
	self should: [object objectAt: 'n'] raise: JSONError.
	self should: [object arrayAt: 'n'] raise: JSONError
]

{ #category : 'tests - typed' }
JSONObjectTest >> testAWholeNumberIsAnIntegerHoweverItWasWritten [
	"3.0 is three, and a sender that writes a count that way is wasteful
	 rather than wrong.  3.7 raises, because truncating it in silence is
	 how a quantity becomes wrong by one."

	| object |
	object := JSONParser parse: '{"a":3.0,"b":3.7,"c":3e0}'.
	self assert: (object integerAt: 'a') equals: 3.
	self assert: (object integerAt: 'c') equals: 3.
	self should: [object integerAt: 'b'] raise: JSONError
]

{ #category : 'tests - typed' }
JSONObjectTest >> testTheErrorNamesTheNameAndTheType [
	| message |
	message := [(JSONParser parse: '{"n":42}') stringAt: 'n'. nil]
		on: JSONError do: [:error | error messageText].
	self assert: (message includesSubstring: 'n').
	self assert: (message includesSubstring: 'SmallInteger').
	self assert: (message includesSubstring: 'string')
]

{ #category : 'tests - enumerating' }
JSONObjectTest >> testEnumerating [
	| object names values |
	object := JSONParser parse: '{"a":1,"b":2}'.
	names := OrderedCollection new.
	values := OrderedCollection new.
	object keysDo: [:each | names add: each].
	object do: [:each | values add: each].
	self assert: names asArray equals: #('a' 'b').
	self assert: values asArray equals: #(1 2).
	names := OrderedCollection new.
	object keysAndValuesDo: [:name :value | names add: name, value printString].
	self assert: names asArray equals: #('a1' 'b2')
]

{ #category : 'tests - enumerating' }
JSONObjectTest >> testSizeAndEmptiness [
	self assert: JSONObject new size equals: 0.
	self assert: JSONObject new isEmpty.
	self deny: JSONObject new notEmpty.
	self assertEmpty: JSONObject new.
	self denyEmpty: (JSONParser parse: '{"a":1}')
]

{ #category : 'tests - comparing' }
JSONObjectTest >> testEqualityIgnoresTheOrder [
	"JSON says two objects with the same members are the same object.  The
	 order is kept for writing and ignored for comparing, which is the
	 only combination that makes sense -- a test that had to predict the
	 order of a document it did not write would be testing the parser's
	 bookkeeping."

	self assert: (JSONParser parse: '{"a":1,"b":2}')
		equals: (JSONParser parse: '{"b":2,"a":1}').
	self deny: (JSONParser parse: '{"a":1}')
		= (JSONParser parse: '{"a":2}').
	self deny: (JSONParser parse: '{"a":1}')
		= (JSONParser parse: '{"a":1,"b":2}')
]

{ #category : 'tests - copying' }
JSONObjectTest >> testACopyIsIndependent [
	"Without postCopy the copy holds MY OrderedCollection and MY
	 Dictionary, so putting a name in the copy puts it in the original."

	| object copy |
	object := JSONParser parse: '{"a":1}'.
	copy := object copy.
	copy at: 'b' put: 2.
	self assert: object size equals: 1.
	self assert: copy size equals: 2
]

{ #category : 'tests - locking' }
JSONObjectTest >> testAnEnumerationMayTouchTheObject [
	"The reason do: and friends work from a snapshot rather than holding the
	 lock across the block.

	 A Mutex here is not re-entrant and reports re-entry by raising, so a
	 design that held the lock across the caller's block would turn the most
	 natural line somebody could write -- looking at an object while walking
	 it -- into an error.  This is the test that says so; nothing else in
	 the suite would notice the change, because every other enumeration here
	 is over an object nobody is touching.

	 The snapshot is the other half: the block sees the two members that
	 were there when it started, not the third it adds."

	| object seen |
	object := JSONParser parse: '{"a":1,"b":2}'.
	seen := OrderedCollection new.
	object keysAndValuesDo: [:name :value |
		seen add: name.
		object at: 'c' put: 3.
		object at: name].
	self assert: seen asArray equals: #('a' 'b').
	self assert: object size equals: 3
]

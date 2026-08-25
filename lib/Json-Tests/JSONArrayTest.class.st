"
The array, and the two places it does not behave like the OrderedCollection
it is built on: collect: answers an OrderedCollection rather than a
JSONArray, and an index past the end raises rather than answering nil.

Both are tested here because both are surprises, and a surprise nothing
records is a surprise somebody will remove.
"
Class {
	#name : 'JSONArrayTest',
	#superclass : 'TestCase',
	#category : 'Json-Tests'
}

{ #category : 'tests - accessing' }
JSONArrayTest >> testAddAnswersTheValue [
	self assert: (JSONArray new add: 7) equals: 7
]

{ #category : 'tests - accessing' }
JSONArrayTest >> testAddingAndReading [
	| array |
	array := JSONArray new.
	array add: 1; add: 'two'; add: nil.
	self assert: array size equals: 3.
	self assert: (array at: 1) equals: 1.
	self assert: (array at: 2) equals: 'two'.
	self assert: (array at: 3) equals: nil.
	self assert: array first equals: 1.
	self assert: array last equals: nil
]

{ #category : 'tests - accessing' }
JSONArrayTest >> testAnIndexPastTheEndRaises [
	"A name that is not in an object is ordinary -- that is how JSON spells
	 optional -- but an array has a length, and reading past it is a
	 mistake in the reader's arithmetic.  Answering nil buries that one
	 level down."

	| array |
	array := JSONArray with: 1.
	self should: [array at: 0] raise: JSONError.
	self should: [array at: 2] raise: JSONError.
	self should: [JSONArray new first] raise: JSONError.
	self assert: (array at: 2 ifAbsent: ['x']) equals: 'x'
]

{ #category : 'tests - accessing' }
JSONArrayTest >> testPuttingPastTheEndFillsWithNull [
	"org.json's behaviour, kept: an array built by index from a source with
	 holes has to be filled with something, and null is the only thing a
	 JSON array can be filled with."

	| array |
	array := JSONArray new.
	array at: 3 put: 'c'.
	self assert: array asJsonString equals: '[null,null,"c"]'
]

{ #category : 'tests - accessing' }
JSONArrayTest >> testPuttingAtANonIndexIsRefused [
	"The Integer check comes first so that `at: nil put:' is an error and
	 not a comparison against nil."

	self should: [JSONArray new at: 0 put: 1] raise: JSONError.
	self should: [JSONArray new at: nil put: 1] raise: JSONError
]

{ #category : 'tests - accessing' }
JSONArrayTest >> testRemoving [
	| array |
	array := JSONParser parse: '[1,2,3]'.
	self assert: (array removeAt: 2) equals: 2.
	self assert: array asJsonString equals: '[1,3]'.
	self assert: array removeFirst equals: 1.
	self assert: array removeLast equals: 3.
	self assert: array isEmpty.
	self should: [array removeFirst] raise: JSONError
]

{ #category : 'tests - conversion' }
JSONArrayTest >> testValuesAreConvertedGoingIn [
	| array |
	array := JSONArray new.
	array add: #(1 2); add: (Dictionary new at: 'k' put: 1; yourself).
	self assert: ((array at: 1) isKindOf: JSONArray).
	self assert: ((array at: 2) isKindOf: JSONObject).
	self assert: array asJsonString equals: '[[1,2],{"k":1}]'
]

{ #category : 'tests - conversion' }
JSONArrayTest >> testAnUnconvertibleValueIsRefused [
	self should: [JSONArray new add: Object new] raise: JSONError.
	self should: [JSONArray withAll: (Array with: Object new)]
		raise: JSONError
]

{ #category : 'tests - conversion' }
JSONArrayTest >> testWithAllTakesAnyCollection [
	self assert: (JSONArray withAll: #(1 2)) asJsonString equals: '[1,2]'.
	self assert: (JSONArray withAll: (OrderedCollection with: 1 with: 2))
		asJsonString equals: '[1,2]'.
	self assert: (JSONArray withAll: (1 to: 3)) asJsonString
		equals: '[1,2,3]'
]

{ #category : 'tests - conversion' }
JSONArrayTest >> testAsArrayAndAsOrderedCollection [
	| array |
	array := JSONParser parse: '[1,2]'.
	self assert: array asArray equals: #(1 2).
	self assert: array asOrderedCollection asArray equals: #(1 2).
	array asOrderedCollection add: 3.
	self assert: array size equals: 2
]

{ #category : 'tests - typed' }
JSONArrayTest >> testTypedAccessors [
	| array |
	array := JSONParser parse: '["x",3,1.5,true,{},[],null]'.
	self assert: (array stringAt: 1) equals: 'x'.
	self assert: (array integerAt: 2) equals: 3.
	self assert: (array numberAt: 3) equals: 3/2.
	self assert: (array floatAt: 3) equals: 1.5.
	self assert: (array booleanAt: 4) equals: true.
	self assert: ((array objectAt: 5) isKindOf: JSONObject).
	self assert: ((array arrayAt: 6) isKindOf: JSONArray).
	self assert: (array stringAt: 7) equals: nil
]

{ #category : 'tests - typed' }
JSONArrayTest >> testTypedAccessorsRaiseForTheWrongType [
	| array |
	array := JSONParser parse: '[42]'.
	self should: [array stringAt: 1] raise: JSONError.
	self should: [array booleanAt: 1] raise: JSONError.
	self should: [array stringAt: 9] raise: JSONError.
	self assert: (array stringAt: 9 ifAbsent: ['x']) equals: 'x'
]

{ #category : 'tests - enumerating' }
JSONArrayTest >> testEnumerating [
	| array seen |
	array := JSONParser parse: '[1,2,3]'.
	seen := OrderedCollection new.
	array do: [:each | seen add: each].
	self assert: seen asArray equals: #(1 2 3).
	self assert: (array inject: 0 into: [:a :b | a + b]) equals: 6.
	self assert: (array detect: [:each | each > 2] ifNone: [nil]) equals: 3.
	self assert: (array includes: 2).
	self assert: (array indexOf: 3) equals: 3
]

{ #category : 'tests - enumerating' }
JSONArrayTest >> testSelectAnswersAJsonArray [
	"Every element of the answer was already an element of mine, so no
	 value can arrive that was not a JSON value already."

	| array |
	array := JSONParser parse: '[1,2,3,4]'.
	self assert: ((array select: [:each | each even]) isKindOf: JSONArray).
	self assert: (array select: [:each | each even]) asJsonString
		equals: '[2,4]'.
	self assert: (array reject: [:each | each even]) asJsonString
		equals: '[1,3]'
]

{ #category : 'tests - enumerating' }
JSONArrayTest >> testCollectAnswersAnOrderedCollection [
	"The block may answer anything at all and a JSONArray may hold only
	 JSON values, so answering a JSONArray would make this raise on a
	 perfectly reasonable block.  The conversion is the caller's to ask
	 for."

	| array |
	array := JSONParser parse: '[1,2]'.
	self assert: ((array collect: [:each | each class]) isKindOf:
		OrderedCollection).
	self assert: (array collect: [:each | each * 2]) asArray equals: #(2 4).
	self assert: (array collect: [:each | each * 2]) asJsonString
		equals: '[2,4]'
]

{ #category : 'tests - comparing' }
JSONArrayTest >> testEquality [
	self assert: (JSONParser parse: '[1,[2]]')
		equals: (JSONParser parse: '[1,[2]]').
	self deny: (JSONParser parse: '[1,2]') = (JSONParser parse: '[2,1]').
	self deny: (JSONParser parse: '[1]') = (JSONParser parse: '[1,2]').
	self deny: (JSONParser parse: '[1]') = 1
]

{ #category : 'tests - copying' }
JSONArrayTest >> testACopyIsIndependent [
	| array copy |
	array := JSONParser parse: '[1]'.
	copy := array copy.
	copy add: 2.
	self assert: array size equals: 1.
	self assert: copy size equals: 2
]

{ #category : 'tests - locking' }
JSONArrayTest >> testAnEnumerationMayTouchTheArray [
	"The array's half of JSONObjectTest>>testAnEnumerationMayTouchTheObject:
	 do: works from a snapshot, so a block that reads or grows the array
	 neither deadlocks nor sees what it just added."

	| array seen |
	array := JSONParser parse: '[1,2]'.
	seen := OrderedCollection new.
	array do: [:each |
		seen add: each.
		array add: 99.
		array at: 1].
	self assert: seen asArray equals: #(1 2).
	self assert: array size equals: 4
]

"
The JSON array: values in the order they were written.

Not a subclass of OrderedCollection, and the reasons are the ones that made
JSONObject not a Dictionary.

Everything that goes in goes through asJsonValue, so a nested Dictionary
becomes a JSONObject and a Rectangle is refused where it is added rather
than where the document is written.  Inheriting add: and at:put: from a
collection that takes anything would put a hole in that at the two places
it matters most.

And an OrderedCollection is not safe to use from two processes at once,
which on this system means two cores.  Every message here that touches my
elements takes my Mutex; see JSONObject's class comment for what that
promises, what it does not, and why a JSON document is worth paying for
when doc/CONCURRENCY.md says the base collections are not.  The short
version: two processes adding cannot lose an element, a sequence of my
messages is still not atomic, and no block of yours ever runs inside my
lock.

The cost of not inheriting is the collection protocol, which has to be
written out.  Most of it is one line over an OrderedCollection and the list
is deliberately short: do:, collect:, select:, reject:, detect:ifNone:,
inject:into: and the accessors.  Anything past that is asOrderedCollection
away -- and that answers a plain OrderedCollection, which is not
thread-safe, which is what asking for one means.

collect: answers an ORDERED COLLECTION and not a JSONArray, which is the
one place this class does not behave like the collection it resembles.  The
block may answer anything at all, and a JSONArray may hold only JSON
values; answering a JSONArray would mean `(anArray collect: [:each | each
class])' raising on a perfectly reasonable block.  So the conversion is the
caller's to ask for, with asJsonValue, and the failure lands where the
decision was made.

An index past the end is an error, not nil.  A name that is not in an
object is ordinary -- that is how JSON spells optional -- but an array has
a length and reading past it is a mistake in the reader's arithmetic, which
is exactly the case where answering nil buries the fault one level down.
at:ifAbsent: is there for the reader who really does not know.
"
Class {
	#name : 'JSONArray',
	#superclass : 'Object',
	#instVars : [ 'elements', 'mutex' ],
	#category : 'Json'
}

{ #category : 'instance creation' }
JSONArray class >> fromString: aString [
	"The JSON array that text denotes; anything else in it is an error."

	^JSONParser parseArray: aString
]

{ #category : 'instance creation' }
JSONArray class >> readFrom: aStream [
	^JSONParser parseArray: aStream
]

{ #category : 'instance creation' }
JSONArray class >> with: aValue [
	^self new add: aValue; yourself
]

{ #category : 'instance creation' }
JSONArray class >> withAll: aCollection [
	"An array of what that collection holds, converted.

	 A Dictionary is a Collection too and would arrive here as its values,
	 in hash order, which is never what anybody means.  Dictionary>>
	 asJsonValue answers a JSONObject for that reason, so the only way to
	 reach this with one is to ask for it by name."

	| array |
	array := self new.
	aCollection do: [:each | array add: each].
	^array
]

{ #category : 'initialization' }
JSONArray >> initialize [
	elements := OrderedCollection new.
	mutex := Mutex new
]

{ #category : 'accessing' }
JSONArray >> at: anIndex [
	"The value at that index.

	 The bounds check and the read are one critical section -- which is what
	 at:ifAbsent: already does -- because as two of them the array could
	 shrink in between and turn a checked index into an unchecked one."

	^self at: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing' }
JSONArray >> at: anIndex ifAbsent: aBlock [
	"The value at that index, or the block's answer.

	 My own Mutex stands in for `not there', so that the block runs outside
	 the lock; a Mutex is not a JSON value and so cannot be one of my
	 elements.  See JSONObject>>at:ifAbsent:."

	| found |
	anIndex isInteger ifFalse: [^aBlock value].
	found := mutex critical: [
		(anIndex between: 1 and: elements size)
			ifTrue: [elements at: anIndex]
			ifFalse: [mutex]].
	found == mutex ifTrue: [^aBlock value].
	^found
]

{ #category : 'accessing' }
JSONArray >> at: anIndex put: aValue [
	"Store that value at that index, filling any gap with null.

	 Filling rather than refusing is org.json's behaviour and is kept: an
	 array is often built by index from a source that has holes, and null
	 is the only thing a JSON array can be filled with anyway.  Refusing
	 would leave the caller to write the same loop.

	 A gap of a million from one careless index is a million nulls, which
	 is why the index is checked for being an Integer first -- `at: nil
	 put:' would otherwise be a comparison against nil rather than an
	 error.  The conversion happens before the lock is taken, for the
	 reason JSONObject>>at:put: gives."

	| value |
	anIndex isInteger ifFalse: [^self outOfRange: anIndex].
	anIndex < 1 ifTrue: [^self outOfRange: anIndex].
	value := aValue asJsonValue.
	^mutex critical: [
		[elements size < anIndex] whileTrue: [elements addLast: nil].
		elements at: anIndex put: value]
]

{ #category : 'accessing' }
JSONArray >> first [
	^self at: 1
]

{ #category : 'accessing' }
JSONArray >> last [
	"self at: self size would be two operations with a gap, and an array
	 that shrank in the gap would be read past its end."

	| found |
	found := mutex critical: [
		elements isEmpty ifTrue: [mutex] ifFalse: [elements last]].
	found == mutex ifTrue: [^self errorEmpty].
	^found
]

{ #category : 'accessing' }
JSONArray >> size [
	^mutex critical: [elements size]
]

{ #category : 'adding' }
JSONArray >> add: aValue [
	"Append that value, converted.  Answers the VALUE, as add: does
	 everywhere else in the image -- which is why the instance-creation
	 methods above end in yourself."

	| value |
	value := aValue asJsonValue.
	mutex critical: [elements addLast: value].
	^aValue
]

{ #category : 'adding' }
JSONArray >> addAll: aCollection [
	"Every element, converted, appended.

	 One critical section for the lot, so that a collection added by one
	 process arrives whole rather than interleaved with another's.  The
	 conversion is done first, outside the lock, which is also what makes
	 that possible."

	| values |
	values := OrderedCollection new: (aCollection size max: 4).
	aCollection do: [:each | values addLast: each asJsonValue].
	mutex critical: [values do: [:each | elements addLast: each]].
	^aCollection
]

{ #category : 'removing' }
JSONArray >> removeAt: anIndex [
	"The value that was at that index, which is now shorter by one.

	 The work is OrderedCollection>>removeIndex:, and it is worth saying
	 which one.  1983's took an index into the Array behind the collection,
	 not the index a caller has -- `removeAt: 2' on [1,2,3] answered 2,
	 which is right, and left [2,3] behind, which is not -- and the first
	 version of this method rebuilt the collection element by element to
	 stay away from it.  lib/Collections-Protocol now gives removeIndex:
	 the meaning its name has, and this is a caller of it (doc/JSON.md).

	 The bounds check is here rather than left to the collection so that
	 the error names the JSONArray and the index, under the lock and
	 without an exception crossing it."

	| found |
	anIndex isInteger ifFalse: [^self outOfRange: anIndex].
	found := mutex critical: [
		(anIndex between: 1 and: elements size)
			ifFalse: [mutex]
			ifTrue: [elements removeIndex: anIndex]].
	found == mutex ifTrue: [^self outOfRange: anIndex].
	^found
]

{ #category : 'removing' }
JSONArray >> removeFirst [
	^self removeAt: 1
]

{ #category : 'removing' }
JSONArray >> removeLast [
	"Not `removeAt: self size', which is two operations with a gap: between
	 the size and the removal another process may have removed one, and the
	 index would then name the wrong element rather than fail."

	| found |
	found := mutex critical: [
		elements isEmpty ifTrue: [mutex] ifFalse: [elements removeLast]].
	found == mutex ifTrue: [^self errorEmpty].
	^found
]

{ #category : 'testing' }
JSONArray >> isEmpty [
	^mutex critical: [elements isEmpty]
]

{ #category : 'testing' }
JSONArray >> notEmpty [
	^mutex critical: [elements notEmpty]
]

{ #category : 'testing' }
JSONArray >> includes: aValue [
	^self snapshot includes: aValue
]

{ #category : 'testing' }
JSONArray >> indexOf: aValue [
	^self snapshot indexOf: aValue
]

{ #category : 'enumerating' }
JSONArray >> do: aBlock [
	"Over a snapshot: your block does not run inside my lock.  See
	 JSONObject's class comment for why."

	self snapshot do: aBlock
]

{ #category : 'enumerating' }
JSONArray >> do: aBlock separatedBy: aSeparatorBlock [
	self snapshot do: aBlock separatedBy: aSeparatorBlock
]

{ #category : 'enumerating' }
JSONArray >> reverseDo: aBlock [
	self snapshot reverseDo: aBlock
]

{ #category : 'enumerating' }
JSONArray >> doWithIndex: aBlock [
	self snapshot doWithIndex: aBlock
]

{ #category : 'enumerating' }
JSONArray >> collect: aBlock [
	"An OrderedCollection, not a JSONArray.  The class comment says why."

	^self snapshot collect: aBlock
]

{ #category : 'enumerating' }
JSONArray >> select: aBlock [
	"A JSONArray: every element of the answer was already an element of
	 mine, so no value can arrive that was not a JSON value already.  That
	 is the difference from collect:, and it is the whole difference."

	^self class withAll: (self snapshot select: aBlock)
]

{ #category : 'enumerating' }
JSONArray >> reject: aBlock [
	^self select: [:each | (aBlock value: each) not]
]

{ #category : 'enumerating' }
JSONArray >> detect: aBlock ifNone: aNoneBlock [
	^self snapshot detect: aBlock ifNone: aNoneBlock
]

{ #category : 'enumerating' }
JSONArray >> inject: aValue into: aBlock [
	^self snapshot inject: aValue into: aBlock
]

{ #category : 'accessing values' }
JSONArray >> stringAt: anIndex [
	^self stringAt: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing values' }
JSONArray >> stringAt: anIndex ifAbsent: aBlock [
	| value |
	value := self at: anIndex ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	value isString ifTrue: [^value].
	^self wrongType: 'a string' at: anIndex was: value
]

{ #category : 'accessing values' }
JSONArray >> numberAt: anIndex [
	^self numberAt: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing values' }
JSONArray >> numberAt: anIndex ifAbsent: aBlock [
	| value |
	value := self at: anIndex ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	value isNumber ifTrue: [^value].
	^self wrongType: 'a number' at: anIndex was: value
]

{ #category : 'accessing values' }
JSONArray >> integerAt: anIndex [
	^self integerAt: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing values' }
JSONArray >> integerAt: anIndex ifAbsent: aBlock [
	"Whole numbers written with a point answer as Integers, for the reason
	 given in JSONObject>>integerAt:ifAbsent:."

	| value |
	value := self numberAt: anIndex ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	value isInteger ifTrue: [^value].
	value = value truncated ifTrue: [^value truncated].
	^self wrongType: 'a whole number' at: anIndex was: value
]

{ #category : 'accessing values' }
JSONArray >> floatAt: anIndex [
	^self floatAt: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing values' }
JSONArray >> floatAt: anIndex ifAbsent: aBlock [
	| value |
	value := self numberAt: anIndex ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	^value asFloat
]

{ #category : 'accessing values' }
JSONArray >> booleanAt: anIndex [
	^self booleanAt: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing values' }
JSONArray >> booleanAt: anIndex ifAbsent: aBlock [
	| value |
	value := self at: anIndex ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	(value == true or: [value == false]) ifTrue: [^value].
	^self wrongType: 'a boolean' at: anIndex was: value
]

{ #category : 'accessing values' }
JSONArray >> objectAt: anIndex [
	^self objectAt: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing values' }
JSONArray >> objectAt: anIndex ifAbsent: aBlock [
	| value |
	value := self at: anIndex ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	(value isKindOf: JSONObject) ifTrue: [^value].
	^self wrongType: 'an object' at: anIndex was: value
]

{ #category : 'accessing values' }
JSONArray >> arrayAt: anIndex [
	^self arrayAt: anIndex ifAbsent: [self outOfRange: anIndex]
]

{ #category : 'accessing values' }
JSONArray >> arrayAt: anIndex ifAbsent: aBlock [
	| value |
	value := self at: anIndex ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	(value isKindOf: JSONArray) ifTrue: [^value].
	^self wrongType: 'an array' at: anIndex was: value
]

{ #category : 'converting' }
JSONArray >> asJsonValue [
	^self
]

{ #category : 'converting' }
JSONArray >> asArray [
	^self snapshot asArray
]

{ #category : 'converting' }
JSONArray >> asOrderedCollection [
	^self snapshot
]

{ #category : 'comparing' }
JSONArray >> = anObject [
	"Both sides snapshotted first, and neither lock held while the other is
	 taken.  See JSONObject>>= for why that ordering is the whole point."

	| mine theirs |
	self == anObject ifTrue: [^true].
	(anObject isKindOf: JSONArray) ifFalse: [^false].
	mine := self snapshot.
	theirs := anObject snapshot.
	mine size = theirs size ifFalse: [^false].
	1 to: mine size do: [:i |
		(mine at: i) = (theirs at: i) ifFalse: [^false]].
	^true
]

{ #category : 'comparing' }
JSONArray >> hash [
	"Size and first element only, for the reason given in JSONObject>>hash:
	 the contents are mutable and hashing all of them walks the tree."

	| first |
	first := self at: 1 ifAbsent: [nil].
	^self size hash bitXor: first hash
]

{ #category : 'copying' }
JSONArray >> postCopy [
	"A copy shares neither my elements nor my lock; see
	 JSONObject>>postCopy, which explains the order these two lines are in
	 and why it matters."

	mutex critical: [elements := elements copy].
	mutex := Mutex new
]

{ #category : 'printing' }
JSONArray >> printOn: aStream [
	"As JSON; see JSONObject>>printOn:."

	JSONWriter write: self on: aStream
]

{ #category : 'private' }
JSONArray >> snapshot [
	"My elements as they are at one instant, in an OrderedCollection of my
	 own.  Everything that enumerates, compares or converts is built on
	 this, so that no block of the caller's runs while my lock is held."

	^mutex critical: [elements copy]
]

{ #category : 'private' }
JSONArray >> errorEmpty [
	"For last and removeLast, which have no index to name."

	^JSONError signal: 'the array is empty'
]

{ #category : 'private' }
JSONArray >> outOfRange: anIndex [
	^JSONError signal: 'index ', anIndex printString,
		' is outside an array of ', self size printString, ' elements'
]

{ #category : 'private' }
JSONArray >> wrongType: aDescription at: anIndex was: aValue [
	^JSONError signal: 'element ', anIndex printString, ' is ',
		aValue class name article, ' ', aValue class name, ' and not ',
		aDescription
]

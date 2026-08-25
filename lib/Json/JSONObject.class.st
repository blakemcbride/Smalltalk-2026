"
The JSON object: names, and the values they stand for.

WHY NOT JUST A DICTIONARY.  Four reasons, and the last two are the ones that
made the class rather than a set of Dictionary extensions.

First, a JSON object may hold only JSON values, and a Dictionary will hold
anything.  Here every value goes in through asJsonValue, so a Dictionary
becomes a JSONObject, an Array becomes a JSONArray, and a Rectangle is
refused at the moment it is put rather than at the moment somebody tries to
write the document -- which in a web application is a request later, in
another process, with none of the code that was wrong on the stack.

Second, the keys keep the order they were first put in.  1983's Dictionary
iterates in hash order, so a document read and written back would come out
with its names shuffled, and two runs of the same program would produce two
different files.  That makes every writer test depend on the hash function
and every diff of two generated documents useless.  JSON says the order is
insignificant; people and version control say otherwise.

Third, at: answers nil for a name that is not there, where Dictionary>>at:
raises.  This is Kiss's choice and it is deliberate: in JSON an absent
field is ordinary -- it is how `optional' is spelled -- and a reader that
raises for one turns every read into at:ifAbsent:.  The cost is that a
missing name and a name whose value is null both answer nil.  When the
difference matters, and sometimes it does, includesKey: and isNullAt: tell
them apart, and nothing else can.

FOURTH, AND THE REASON THIS CLASS IS THE SHAPE IT IS: it is safe to use
from more than one process at once, and a Dictionary is not.

doc/CONCURRENCY.md is explicit that the base collections are unsynchronized
on purpose -- paying for a lock on every access to serve the rare shared
case is the wrong default, which is Java's post-Vector lesson.  A JSON
document is not the rare shared case.  It is the object a request arrives
as and is then handed to whatever answers the request, and on this system
that is genuinely another core rather than another slot in a run queue.
So this class pays.

WHAT THE LOCK PROMISES.  Every message that touches my names or values
takes my Mutex, reads or writes, and gives it back.  Two processes putting
two names cannot lose one of them; a process reading while another writes
sees the value before or the value after and never a Dictionary caught
half-way through growing, which is a real failure and answers nil for a
name that is present.

WHAT IT DOES NOT PROMISE, and this is the part worth reading: a SEQUENCE of
messages is not atomic.  `object at: 'n' put: (object at: 'n') + 1' from
two processes loses an increment, exactly as it would on any other object,
because the read and the write are two operations with a gap.  A caller
that needs several operations to happen together holds its own lock around
them; at:ifAbsentPut: is here because that particular sequence is common
enough to be worth doing properly.

NO BLOCK OF YOURS RUNS INSIDE MY LOCK.  do:, keysDo: and keysAndValuesDo:
take a snapshot of the names under the lock and then enumerate outside it,
so the block sees the members as they were when it started.  Two reasons,
and either alone would be enough.  Mutex is not re-entrant and says so by
raising, so `json do: [:each | json at: ...]' would deadlock in every other
design and would have been a trap laid for the most natural code somebody
could write.  And a lock held across a block that is not mine is a lock
held for as long as somebody else's code takes.

KEYS ARE STRINGS.  A Symbol is accepted and stored as its String, so that
`at: #name' and `at: ''name''' are the same name and keys answers something
that can be written out.  Anything else is refused: JSON has no way to
write a name that is not text, so a number used as a key is a bug that
would otherwise be found by the writer.
"
Class {
	#name : 'JSONObject',
	#superclass : 'Object',
	#instVars : [ 'names', 'map', 'mutex' ],
	#category : 'Json'
}

{ #category : 'instance creation' }
JSONObject class >> fromString: aString [
	"The JSON object that text denotes.  Anything else in it -- an array, a
	 number, a bare string -- is an error, because a caller that asked for
	 an object and got a number has no use for it."

	^JSONParser parseObject: aString
]

{ #category : 'instance creation' }
JSONObject class >> readFrom: aStream [
	^JSONParser parseObject: aStream
]

{ #category : 'instance creation' }
JSONObject class >> withAll: aDictionary [
	"An object holding what that Dictionary holds, converted.

	 keysAndValuesDo: rather than a copy of the Dictionary, because the
	 values have to go through at:put: to be converted and checked."

	| object |
	object := self new.
	aDictionary keysAndValuesDo: [:key :value | object at: key put: value].
	^object
]

{ #category : 'initialization' }
JSONObject >> initialize [
	names := OrderedCollection new.
	map := Dictionary new.
	mutex := Mutex new
]

{ #category : 'accessing' }
JSONObject >> at: aName [
	"The value of that name, or nil when there is none.

	 nil for absent is the whole reason this is not a Dictionary; see the
	 class comment.  A name whose value is JSON null answers nil too, and
	 isNullAt: is what tells the two apart."

	| key |
	key := self keyFor: aName.
	^mutex critical: [map at: key ifAbsent: [nil]]
]

{ #category : 'accessing' }
JSONObject >> at: aName ifAbsent: aBlock [
	"The value of that name, or the block's answer.

	 The block is evaluated OUTSIDE the lock, which is what the sentinel is
	 for.  My own Mutex stands in for `not there': a Mutex is not a JSON
	 value -- asJsonValue refuses one -- so it cannot be a value stored
	 here, and cannot be confused with one.  nil could not do the job,
	 because nil IS a JSON value and is exactly what a null member holds."

	| key found |
	key := self keyFor: aName.
	found := mutex critical: [map at: key ifAbsent: [mutex]].
	found == mutex ifTrue: [^aBlock value].
	^found
]

{ #category : 'accessing' }
JSONObject >> at: aName put: aValue [
	"Store that value under that name, converting it to a JSON value first.

	 The conversion happens OUTSIDE the lock, and has to: asJsonValue is a
	 message, a class of the caller's may implement it, and running a
	 stranger's code inside my lock is how a library ends up holding a lock
	 across a database call.

	 Answers the VALUE, not the receiver.  org.json answers the object so
	 that puts can be chained, which is how Java writes a cascade; Smalltalk
	 has cascades, and at:put: answering anything but its argument breaks
	 the one rule every collection in the image follows."

	| key value |
	key := self keyFor: aName.
	value := aValue asJsonValue.
	^mutex critical: [
		(map includesKey: key) ifFalse: [names addLast: key].
		map at: key put: value]
]

{ #category : 'accessing' }
JSONObject >> at: aName ifAbsentPut: aBlock [
	"The value of that name, storing the block's answer first if there is
	 none.

	 The block and the conversion run outside the lock, so the name is
	 looked up TWICE -- once to find it missing, and once under the lock
	 that stores it, in case another process put it there in between.  The
	 second look is what makes this worth having: two processes reaching an
	 empty name together both evaluate the block, and both get the same
	 answer back, which is the promise a caller of this message is relying
	 on.  Whichever value loses is discarded, so the block must be one that
	 can be run twice -- which is true of the `ifAbsentPut: [JSONArray new]'
	 this exists for and is worth knowing before it is not."

	| key found value |
	key := self keyFor: aName.
	found := mutex critical: [map at: key ifAbsent: [mutex]].
	found == mutex ifFalse: [^found].
	value := aBlock value asJsonValue.
	^mutex critical: [
		(map includesKey: key)
			ifTrue: [map at: key]
			ifFalse: [
				names addLast: key.
				map at: key put: value]]
]

{ #category : 'accessing' }
JSONObject >> keys [
	"My names, in the order they were first put.  A copy, and not only
	 because the order is mine to keep: it is also the snapshot every
	 enumeration here is built on."

	^mutex critical: [names copy]
]

{ #category : 'accessing' }
JSONObject >> values [
	"My values, in the order their names were first put.  A snapshot, like
	 everything else that answers more than one thing at a time."

	^self valuesSnapshot
]

{ #category : 'accessing' }
JSONObject >> size [
	^mutex critical: [names size]
]

{ #category : 'removing' }
JSONObject >> removeKey: aName [
	^self removeKey: aName ifAbsent: [nil]
]

{ #category : 'removing' }
JSONObject >> removeKey: aName ifAbsent: aBlock [
	"The removed value, or the block's answer.  Both halves have to go: a
	 name left in the order list after its value is gone is a name every
	 enumeration would trip over."

	| key found |
	key := self keyFor: aName.
	found := mutex critical: [
		(map includesKey: key)
			ifFalse: [mutex]
			ifTrue: [ | value |
				value := map at: key.
				map removeKey: key.
				names remove: key ifAbsent: [nil].
				value]].
	found == mutex ifTrue: [^aBlock value].
	^found
]

{ #category : 'testing' }
JSONObject >> includesKey: aName [
	| key |
	key := self keyFor: aName.
	^mutex critical: [map includesKey: key]
]

{ #category : 'testing' }
JSONObject >> isNullAt: aName [
	"Whether that name is there AND its value is JSON null.

	 The distinction at: cannot make.  `{""middleName"": null}' says the
	 sender knows about middle names and this person has none; a document
	 without the name says the sender never considered the question.  Some
	 protocols mean different things by those and there is no way to tell
	 from nil alone.

	 One critical section and not two, because `includesKey: and: [at:
	 isNil]' written as two messages could see the name removed in between
	 and answer for a document that never existed."

	| key |
	key := self keyFor: aName.
	^mutex critical: [
		(map includesKey: key) and: [(map at: key) isNil]]
]

{ #category : 'testing' }
JSONObject >> isEmpty [
	^mutex critical: [names isEmpty]
]

{ #category : 'testing' }
JSONObject >> notEmpty [
	^mutex critical: [names notEmpty]
]

{ #category : 'enumerating' }
JSONObject >> do: aBlock [
	"Over the values, which is what do: means for a Dictionary.

	 Over a SNAPSHOT of them, for the reason in the class comment: your
	 block does not run inside my lock."

	self valuesSnapshot do: aBlock
]

{ #category : 'enumerating' }
JSONObject >> keysDo: aBlock [
	self keys do: aBlock
]

{ #category : 'enumerating' }
JSONObject >> keysAndValuesDo: aBlock [
	"The names and values as they were together, which is why this takes
	 one snapshot of both rather than a snapshot of the names and then a
	 lookup each: the lookups would be against whatever the object had
	 become by then, and a name removed in between would answer nil as
	 though its value were null."

	| snapshot |
	snapshot := self associationsSnapshot.
	snapshot do: [:each | aBlock value: each key value: each value]
]

{ #category : 'accessing values' }
JSONObject >> stringAt: aName [
	^self stringAt: aName ifAbsent: [nil]
]

{ #category : 'accessing values' }
JSONObject >> stringAt: aName ifAbsent: aBlock [
	"The String under that name.

	 A value of the wrong type raises rather than converting.  org.json and
	 Kiss both convert -- getString of the number 42 answers '42', getLong
	 of the string '42' answers 42 -- and it is the kind of helpfulness that
	 hides the sender's bug in the reader's code.  A document that says 42
	 and one that says ""42"" are different documents, and the day one turns
	 into the other is the day to be told."

	| value |
	value := self at: aName ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	value isString ifTrue: [^value].
	^self wrongType: 'a string' at: aName was: value
]

{ #category : 'accessing values' }
JSONObject >> numberAt: aName [
	^self numberAt: aName ifAbsent: [nil]
]

{ #category : 'accessing values' }
JSONObject >> numberAt: aName ifAbsent: aBlock [
	"The number under that name, exactly as it was read: an Integer, or a
	 Fraction for anything written with a point or an exponent.  See
	 JSONParser for why it is not a Float."

	| value |
	value := self at: aName ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	value isNumber ifTrue: [^value].
	^self wrongType: 'a number' at: aName was: value
]

{ #category : 'accessing values' }
JSONObject >> integerAt: aName [
	^self integerAt: aName ifAbsent: [nil]
]

{ #category : 'accessing values' }
JSONObject >> integerAt: aName ifAbsent: aBlock [
	"The Integer under that name.

	 A number that is whole answers as an Integer even when it was written
	 with a point -- 3.0 is three, and a sender that writes a count that way
	 is not wrong, only wasteful.  A number that is not whole raises, because
	 truncating 3.7 to 3 in silence is how a quantity becomes wrong by one."

	| value |
	value := self numberAt: aName ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	value isInteger ifTrue: [^value].
	value = value truncated ifTrue: [^value truncated].
	^self wrongType: 'a whole number' at: aName was: value
]

{ #category : 'accessing values' }
JSONObject >> floatAt: aName [
	^self floatAt: aName ifAbsent: [nil]
]

{ #category : 'accessing values' }
JSONObject >> floatAt: aName ifAbsent: aBlock [
	"The number under that name, as a Float.

	 This is the lossy one and it is spelled out at every call site, which
	 is the point: 0.1 is exact until somebody asks for it as a Float, and
	 asking is a decision rather than an accident."

	| value |
	value := self numberAt: aName ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	^value asFloat
]

{ #category : 'accessing values' }
JSONObject >> booleanAt: aName [
	^self booleanAt: aName ifAbsent: [nil]
]

{ #category : 'accessing values' }
JSONObject >> booleanAt: aName ifAbsent: aBlock [
	"true or false, and nothing else.

	 Not `anything but nil is true', which is what a language without a
	 JSON boolean has to do and what makes the string ""false"" mean true."

	| value |
	value := self at: aName ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	(value == true or: [value == false]) ifTrue: [^value].
	^self wrongType: 'a boolean' at: aName was: value
]

{ #category : 'accessing values' }
JSONObject >> objectAt: aName [
	^self objectAt: aName ifAbsent: [nil]
]

{ #category : 'accessing values' }
JSONObject >> objectAt: aName ifAbsent: aBlock [
	"The nested JSONObject under that name.

	 `objectAt: ''address'' ifAbsent: [JSONObject new]' is the idiom for
	 reading a document that may or may not carry a section, and it is why
	 Kiss's getJSONObject(key, true) has no counterpart here -- a block says
	 what to do about absence better than a boolean does."

	| value |
	value := self at: aName ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	(value isKindOf: JSONObject) ifTrue: [^value].
	^self wrongType: 'an object' at: aName was: value
]

{ #category : 'accessing values' }
JSONObject >> arrayAt: aName [
	^self arrayAt: aName ifAbsent: [nil]
]

{ #category : 'accessing values' }
JSONObject >> arrayAt: aName ifAbsent: aBlock [
	"The nested JSONArray under that name."

	| value |
	value := self at: aName ifAbsent: [^aBlock value].
	value isNil ifTrue: [^nil].
	(value isKindOf: JSONArray) ifTrue: [^value].
	^self wrongType: 'an array' at: aName was: value
]

{ #category : 'converting' }
JSONObject >> asJsonValue [
	^self
]

{ #category : 'converting' }
JSONObject >> asDictionary [
	"A plain Dictionary of my names and values, one level deep.

	 One level, deliberately: a nested JSONObject stays a JSONObject, so
	 the answer is still something the writer can write.  A caller that
	 wants the whole tree turned into Dictionaries is asking for a
	 conversion that cannot be undone -- the order is lost -- and should
	 say so a level at a time.

	 The answer is a plain Dictionary and therefore NOT thread-safe.  That
	 is what asking for one means."

	| dictionary |
	dictionary := Dictionary new.
	self keysAndValuesDo: [:key :value | dictionary at: key put: value].
	^dictionary
]

{ #category : 'comparing' }
JSONObject >> = anObject [
	"Same names and same values, whatever order they were put in.

	 Order is kept for writing and ignored for comparing, which is the only
	 combination that makes sense: JSON says two objects with the same
	 members are the same object, and a test that had to predict the order
	 of a document it did not write would be a test of the parser's
	 bookkeeping rather than of the document.

	 Both sides are snapshotted before anything is compared, and neither
	 lock is held while the other is taken.  Two processes asking `a = b'
	 and `b = a' at the same moment is the oldest deadlock there is, and
	 the way not to have it is to hold no lock while acquiring another."

	| mine theirsByName |
	self == anObject ifTrue: [^true].
	(anObject isKindOf: JSONObject) ifFalse: [^false].
	mine := self associationsSnapshot.
	theirsByName := Dictionary new.
	anObject associationsSnapshot do: [:each |
		theirsByName at: each key put: each value].
	mine size = theirsByName size ifFalse: [^false].
	mine do: [:each |
		| found |
		found := theirsByName at: each key ifAbsent: [^false].
		found = each value ifFalse: [^false]].
	^true
]

{ #category : 'comparing' }
JSONObject >> hash [
	"The names alone.

	 A hash over the values would have to walk the whole tree, and the tree
	 is mutable -- an object used as a key and then added to would be lost
	 in its own Set.  The names are enough to spread objects out and are
	 cheap, and = does the rest."

	| hash |
	hash := 0.
	self keys do: [:key | hash := hash bitXor: key hash].
	^hash
]

{ #category : 'copying' }
JSONObject >> postCopy [
	"A copy shares nothing with me: not the collections, and not the lock.

	 Without the collections, `copy' answers an object holding MY
	 OrderedCollection and MY Dictionary, so putting a name in the copy puts
	 it in the original.  Object>>copy sends postCopy here -- it did not
	 always, and every Pharo collection copied in this image shared its
	 storage until it did.

	 Without the LOCK, the copy and the original are two objects guarded by
	 one Mutex.  That is not a correctness bug -- one lock for two objects
	 is over-strict, never under -- but it means a process working on the
	 copy blocks a process working on the original, forever, for no reason
	 either of them can see.

	 The order matters and is the whole trick here: at this moment `mutex'
	 is still the ORIGINAL's, because shallowCopy copied the reference.
	 Taking it is what makes the two collection copies a consistent
	 snapshot of the original rather than two reads with a gap.  Only then
	 is it replaced."

	mutex critical: [
		names := names copy.
		map := map copy].
	mutex := Mutex new
]

{ #category : 'printing' }
JSONObject >> printOn: aStream [
	"As JSON, not as `a JSONObject'.

	 printString is what the Inspector, the Transcript and every debugger
	 line shows, and for this class the JSON text IS the useful description.
	 It is not re-readable Smalltalk, which is what printOn: is nominally
	 for; storeOn: was never re-readable for this class either, and a name
	 for the class in front of the text tells a reader nothing they cannot
	 see from the braces."

	JSONWriter write: self on: aStream
]

{ #category : 'private' }
JSONObject >> associationsSnapshot [
	"My names and values, paired, as they are at one instant.

	 Every enumeration and both comparisons are built on this.  It is
	 private because an Association is not a JSON value and handing them out
	 would invite somebody to put one back."

	^mutex critical: [
		names collect: [:key | key -> (map at: key)]]
]

{ #category : 'private' }
JSONObject >> valuesSnapshot [
	^mutex critical: [names collect: [:key | map at: key]]
]

{ #category : 'private' }
JSONObject >> keyFor: aName [
	"That name as the String this object files it under.

	 A Symbol is converted rather than accepted as itself.  In this image
	 Symbol has a value-based #= and hash, so a Symbol and a String would
	 find each other in the Dictionary anyway -- but keys would then answer
	 a mixture, and the writer would be handed Symbols to write.  Converting
	 once, here, is what keeps every other method from having to care.

	 Deliberately touches no instance variable, so that it can be sent
	 before the lock is taken -- which is where every caller sends it."

	aName isSymbol ifTrue: [^aName asString].
	aName isString ifTrue: [^aName].
	^JSONError signal: 'a JSON name must be a String, not ',
		aName class name article, ' ', aName class name
]

{ #category : 'private' }
JSONObject >> wrongType: aDescription at: aName was: aValue [
	^JSONError signal: 'the value of ', aName printString, ' is ',
		aValue class name article, ' ', aValue class name, ' and not ',
		aDescription
]

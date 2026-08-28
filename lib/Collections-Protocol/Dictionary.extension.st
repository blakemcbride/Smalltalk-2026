Extension { #name : 'Dictionary' }

{ #category : '*Collections-Protocol' }
Dictionary >> at: key ifAbsentPut: aBlock [
	"Answer the value, adding the block's answer first if there is none.

	 Under threads this is check-then-act and two workers can each build
	 one; see doc/ParallelAudit.md when that day comes.  Single-threaded
	 it is the idiom every cache is written with."
	^self at: key ifAbsent: [self at: key put: aBlock value]
]

{ #category : '*Collections-Protocol' }
Dictionary >> keysAndValuesDo: aBlock [
	self associationsDo: [:each | aBlock value: each key value: each value]
]

{ #category : '*Collections-Protocol' }
Dictionary >> keysSortedSafely [
	^self keys asSortedCollection asArray
]

{ #category : '*Collections-Protocol' }
Dictionary >> associationsSortedByKey [
	^(self associations asSortedCollection: [:a :b | a key <= b key]) asArray
]

{ #category : '*Collections-Protocol' }
Dictionary >> , aDictionary [
	"Both dictionaries merged, the argument's bindings winning.

	 Overridden from Collection>>, because that one appends what do: yields,
	 and a Dictionary's do: yields VALUES -- so the generic version added
	 values where associations belong and the result then sent #key to a
	 SmallInteger.  Concatenating two dictionaries means merging their
	 KEYS, and the tests say the later one wins: `dictionary1 , dictionary2'
	 has dictionary2's size when the keys coincide."
	| merged |
	merged := self copy.
	aDictionary associationsDo: [:each |
		merged at: each key put: each value].
	^merged
]

{ #category : '*Collections-Protocol' }
Dictionary >> inspect [
	"Open an inspector on me, or say what I am when there is nowhere to
	 open one.

	 Object>>inspect and Object>>basicInspect are guarded in
	 lib/Kernel-Exceptions for the reason given there -- an InspectorView
	 opened with no screen parks the process on the input semaphore for
	 ever -- and this is the second door into the same room: 1983's
	 Dictionary>>inspect opens a DictionaryInspector directly rather than
	 going through either of them."
	self canOpenUserInterface ifFalse: [^super inspect].
	InspectorView open: (DictionaryInspector inspect: self)
]

{ #category : '*Collections-Protocol' }
Dictionary >> findKeyOrNil: key [
	"The slot key's association is in, or the empty slot it would go in.

	 1983's loop with the hash mixed before it picks a bucket, exactly as
	 Set>>findElementOrNil: does and for the reason given there (Bugs3
	 B33): the table's capacity is a power of two and `key hash \\ length'
	 puts every round number in one slot.  Ten thousand Float keys took
	 fifty-five seconds to put in a Dictionary.  The arithmetic is copied
	 rather than shared because this is the hottest loop in the image and
	 a send in it is measurable; the two copies are asserted equal by the
	 check in tests/unit/test_image.c that fills a Set and a Dictionary
	 with the same round numbers.

	 A receiver with no indexed part is Pharo's Dictionary, under
	 profiles/pharo-collections, which probes with scanFor: and never
	 sends this; it is handed back to its own class rather than divided
	 by zero, as Set>>findElementOrNil: explains."
	| location length probe pass hash |
	length := self basicSize.
	length = 0 ifTrue: [^super findKeyOrNil: key].
	pass := 1.
	hash := key hash.
	location := (((hash + (hash bitShift: -30) bitAnd: 16r3FFFFFFF) * 16r9E3779B1)
				bitShift: -16) \\ length + 1.
	[(probe := self basicAt: location) == nil or: [probe key = key]]
		whileFalse: [
			(location := location + 1) > length
				ifTrue: [
					location := 1.
					pass := pass + 1.
					pass > 2 ifTrue: [^self grow findKeyOrNil: key]]].
	^location
]

{ #category : '*Collections-Protocol' }
Dictionary >> = aDictionary [
	"Same class, same keys, and the same value under each key.

	 Set>>= in Collection.extension.st gives a Set value equality and says
	 in its comment that it is defined on Set and not higher up because
	 `equality for a Dictionary has to consider keys as well as values'.
	 A Dictionary is a Set here, though, and INHERITED that =, which walks
	 do: -- the values -- and asks includes: -- also the values -- so two
	 Dictionaries with the same values under different keys were equal:
	 `(Dictionary new at: 1 put: 2; yourself) = (Dictionary new at: 3
	 put: 2; yourself)' answered true.  Bugs3 B34 needed a Dictionary hash
	 built from the keys alone, and a hash from the keys is only lawful
	 when = looks at the keys, so this is the = that hash goes with.

	 at:ifAbsent: rather than includesKey: and at:, so that a key held with
	 a nil value is told apart from a key not held, and one probe per key
	 rather than two.  A Dictionary is equal to itself without a walk."
	self == aDictionary ifTrue: [^true].
	self class == aDictionary class ifFalse: [^false].
	self size = aDictionary size ifFalse: [^false].
	self associationsDo: [:each |
		(aDictionary at: each key ifAbsent: [^false]) = each value
			ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Dictionary >> hash [
	"From my size and my KEYS, never my values.

	 Set>>hash xors the hash of every element, and a Dictionary's elements
	 -- what do: yields -- are its values.  TextConstants holds itself
	 under the key #TextConstants and Smalltalk holds Smalltalk, so hashing
	 either recursed until RecursionDepthExceeded, and so did anything that
	 hashed them: `Set new add: TextConstants' failed, and with it 1983's
	 Class>>sharing:, which adds each pool to a Set -- so no class could
	 name TextConstants as a pool at run time, and a Tonel class with
	 `#pools : [ 'TextConstants' ]' could not be loaded (Bugs3 B34).

	 Keys are what a Dictionary is looked up by, and a key that holds the
	 Dictionary it is a key of is not something anyone has done.  Values
	 are where a self-reference lives, and leaving them out costs nothing
	 a hash needs: two Dictionaries that differ only in a value collide,
	 which a hash may do.  xor because a Dictionary has no order, and the
	 size folded in so that key sets that cancel are still told apart --
	 the same two rules as Set>>hash.  keysDo: rather than the slots,
	 because under profiles/pharo-collections the receiver is Pharo's
	 Dictionary and keeps its associations somewhere else."
	| h |
	h := self size hash.
	self keysDo: [:key | h := h bitXor: key hash].
	^h
]

{ #category : '*Collections-Protocol' }
Dictionary class >> newFrom: aCollection [
	"One of me holding what that collection holds: another Dictionary's
	 bindings, or a collection of Associations.

	 Collection class>>newFrom: says in its own comment that Dictionary
	 overrides it, and Dictionary did not: `Dictionary newFrom:
	 aDictionary' went to withAll:, whose do: walks a Dictionary's VALUES,
	 and the first value was sent #key (Bugs3 B51).  A Dictionary is
	 copied binding by binding into fresh Associations -- not by adding
	 its own, which are the objects at:put: writes through, so sharing
	 them would make the copy write the original."
	| dictionary |
	dictionary := self new: aCollection size.
	(aCollection isKindOf: Dictionary)
		ifTrue: [aCollection keysAndValuesDo: [:key :value | dictionary at: key put: value]]
		ifFalse: [aCollection do: [:each | dictionary at: each key put: each value]].
	^dictionary
]

{ #category : '*Collections-Protocol' }
Dictionary >> postCopy [
	"Give the copy Associations of its own.

	 Object>>copy is shallowCopy followed by postCopy, and the shallow copy
	 of a 1983 Dictionary is a new slot Array holding the SAME Association
	 objects: `d2 := d copy. d2 at: #k put: 2' wrote 2 into the Association
	 both dictionaries held, and `d at: #k' answered 2 (Bugs3 B42).  The
	 library's own Dictionary>>, below begins with `self copy' and so
	 modified its receiver.  Pharo's Dictionary has exactly this method,
	 which is why profiles/pharo-collections was never bitten: there its
	 own postCopy is installed after this one and wins.

	 By the slots and not by public protocol, because there is no public
	 way to replace an Association in place, and rebuilding through at:put:
	 would rehash a dictionary whose layout is already right.  `copy' on
	 each Association rather than `key -> value' so that a subclass of
	 Association -- a LookupKey with more in it -- stays what it was."
	super postCopy.
	1 to: self basicSize do: [:index | | each |
		each := self basicAt: index.
		each == nil ifFalse: [self basicAt: index put: each copy]]
]

{ #category : '*Collections-Protocol' }
Dictionary >> select: aBlock [
	"The associations whose VALUE the block accepts, in a new dictionary
	 of my kind.

	 1983's built the new dictionary with `newCollection add: each' -- the
	 receiver's own Association -- so the result shared every Association
	 it kept with the receiver, and a store into the selection was a store
	 into the original (Bugs3 B42).  at:put: makes a fresh Association in
	 the new dictionary, which is what a selection has to be.  reject: is
	 1983's `self select: [... == false]' and is fixed by this too."
	| newCollection |
	newCollection := self species new.
	self associationsDo: [:each |
		(aBlock value: each value)
			ifTrue: [newCollection at: each key put: each value]].
	^newCollection
]

{ #category : '*Collections-Protocol' }
Dictionary >> valuesDo: aBlock [
	"Evaluate aBlock with each of my values.

	 1983's do: already iterates the values and keysDo: the keys, so the
	 ANSI and Pharo name for the first was the one the manual's selector
	 table listed and nothing answered (Bugs3 B64).  Pharo's own Dictionary
	 in profiles/pharo-collections supersedes this class whole, methods of
	 this file included, and defines the selector itself."
	self do: aBlock
]

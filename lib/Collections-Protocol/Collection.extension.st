"
Forty years of collection protocol that 1983 does not have.

One decision runs through all of it and is worth stating once rather than
apologising for repeatedly: where Pharo answers `self species` -- so that
collecting over an OrderedCollection gives you an OrderedCollection -- the
methods here that CHANGE the shape of the result answer an **Array**.

flatCollect: is the clearest case.  A sorted Set is not a Set,
and a flattened Bag is not a Bag, so `species` is already the wrong answer
for two of the three most interesting receivers; Pharo works around that
with a family of `as:` variants.  An Array is what every one of them can
answer honestly and it is predictable without knowing the receiver.

sorted and sorted: are the exception, and Pharo's own doctest is what
pointed it out: a SEQUENCEABLE collection has an obvious species to answer,
because order is what it is for, so SequenceableCollection overrides both
and 'hello' sorted is 'ehllo'.  The Array answer stands for everything
else, where there is nothing better.  The methods that PRESERVE shape
-- select:, reject:, collect:, copyWithout: -- go on using species, because
there the 1983 answer was already right.
"
Extension { #name : 'Collection' }

{ #category : '*Collections-Protocol' }
Collection >> do: elementBlock separatedBy: separatorBlock [
	"Run the separator between elements, never before the first and never
	 after the last -- which is the whole reason this is not a do: with an
	 nextPutAll: at the end."
	| first |
	first := true.
	self do: [:each |
		first
			ifTrue: [first := false]
			ifFalse: [separatorBlock value].
		elementBlock value: each]
]

{ #category : '*Collections-Protocol' }
Collection >> asArray [
	| result index |
	result := Array new: self size.
	index := 0.
	self do: [:each | result at: (index := index + 1) put: each].
	^result
]

{ #category : '*Collections-Protocol' }
Collection >> sorted [
	"An Array, sorted.  See the class comment on why not the species."
	^self asSortedCollection asArray
]

{ #category : '*Collections-Protocol' }
Collection >> sorted: aBlock [
	^(self asSortedCollection: aBlock) asArray
]

{ #category : '*Collections-Protocol' }
Collection >> flatCollect: aBlock [
	"Collect, then concatenate what each answer contains, one level deep."
	| out |
	out := OrderedCollection new.
	self do: [:each | out addAll: (aBlock value: each)].
	^out asArray
]

{ #category : '*Collections-Protocol' }
Collection >> count: aBlock [
	| n |
	n := 0.
	self do: [:each | (aBlock value: each) ifTrue: [n := n + 1]].
	^n
]

{ #category : '*Collections-Protocol' }
Collection >> anySatisfy: aBlock [
	"Stops at the first one that does, which is why it is a non-local
	 return out of the block rather than a flag."
	self do: [:each | (aBlock value: each) ifTrue: [^true]].
	^false
]

{ #category : '*Collections-Protocol' }
Collection >> allSatisfy: aBlock [
	self do: [:each | (aBlock value: each) ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Collection >> noneSatisfy: aBlock [
	self do: [:each | (aBlock value: each) ifTrue: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Collection >> notEmpty [
	^self isEmpty not
]

{ #category : '*Collections-Protocol' }
Collection >> ifEmpty: aBlock [
	^self isEmpty
		ifTrue: [aBlock cull: self]
		ifFalse: [self]
]

{ #category : '*Collections-Protocol' }
Collection >> ifNotEmpty: aBlock [
	^self isEmpty
		ifTrue: [nil]
		ifFalse: [aBlock cull: self]
]

{ #category : '*Collections-Protocol' }
Collection >> ifEmpty: emptyBlock ifNotEmpty: notEmptyBlock [
	^self isEmpty
		ifTrue: [emptyBlock cull: self]
		ifFalse: [notEmptyBlock cull: self]
]

{ #category : '*Collections-Protocol' }
Collection >> ifNotEmpty: notEmptyBlock ifEmpty: emptyBlock [
	^self isEmpty
		ifTrue: [emptyBlock cull: self]
		ifFalse: [notEmptyBlock cull: self]
]

{ #category : '*Collections-Protocol' }
Collection >> anyOne [
	"Some element, unspecified which.  Meaningful for a Set, where there
	 is no first."
	self do: [:each | ^each].
	^self emptyCheck
]

{ #category : '*Collections-Protocol' }
Collection >> sum [
	"Signals on an empty collection rather than answering zero: the sum of
	 nothing depends on what was going to be added, and this collection
	 cannot say."
	| total first |
	total := nil.
	first := true.
	self do: [:each |
		first
			ifTrue: [total := each. first := false]
			ifFalse: [total := total + each]].
	first ifTrue: [^self emptyCheck].
	^total
]

{ #category : '*Collections-Protocol' }
Collection >> sum: aBlock [
	^(self collect: aBlock) sum
]

{ #category : '*Collections-Protocol' }
Collection >> average [
	^self sum / self size
]

{ #category : '*Collections-Protocol' }
Collection >> max [
	| best first |
	best := nil.
	first := true.
	self do: [:each |
		(first or: [each > best]) ifTrue: [best := each. first := false]].
	first ifTrue: [^self emptyCheck].
	^best
]

{ #category : '*Collections-Protocol' }
Collection >> min [
	| best first |
	best := nil.
	first := true.
	self do: [:each |
		(first or: [each < best]) ifTrue: [best := each. first := false]].
	first ifTrue: [^self emptyCheck].
	^best
]

{ #category : '*Collections-Protocol' }
Collection >> copyWithout: anObject [
	^self reject: [:each | each = anObject]
]

{ #category : '*Collections-Protocol' }
Collection >> isCollection [
	^true
]

{ #category : '*Collections-Protocol' }
Collection >> contents [
	"My elements, as a collection of my own kind.

	 A copy rather than the receiver: contents is what a caller asks for
	 when it wants the values without holding on to the thing that owns
	 them, and answering self makes that caller's later mutation somebody
	 else's surprise.  Streams answer contents too, meaning the same thing
	 -- what has been accumulated -- which is why the name is worth having
	 on both."
	^self copy
]

{ #category : '*Collections-Protocol' }
Set >> = aCollection [
	"Same class, same size, same elements.

	 1983 leaves Set with Object's identity =, so two Sets built the same
	 way are never equal and `assert: x equals: y' on them can only fail --
	 which it did, reporting `expected Set (1 7 ) but got Set (1 7 )', a
	 message that reads like a bug in the printer.

	 Defined on Set and not on HashedCollection, which is where Pharo puts
	 it, because Dictionary is a HashedCollection here too and equality for
	 a Dictionary has to consider keys as well as values.  Getting that
	 wrong quietly is worse than not answering it."
	self == aCollection ifTrue: [^true].
	self class == aCollection class ifFalse: [^false].
	self size = aCollection size ifFalse: [^false].
	self do: [:each | (aCollection includes: each) ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Set >> hash [
	"Derived from my elements, so that it agrees with the = above.

	 Added in the same breath as Set>>= and for the same reason that
	 Symbol>>hash had to follow Symbol>>=: giving a class value equality and
	 leaving it Object's identity hash produces objects that are equal and
	 hash differently, which is precisely the pair a hashed collection
	 cannot hold.  Defining = without hash does not leave things as they
	 were -- it breaks them in a new place, one indirection further away.

	 Combined with bitXor:, which is commutative, because a Set has no
	 order and two Sets that are equal must hash the same however they were
	 built up.  The size is folded in so that sets whose elements happen to
	 cancel are still told apart."
	| h |
	h := self size hash.
	self do: [:each | h := h bitXor: each hash].
	^h
]

{ #category : '*Collections-Protocol' }
Collection class >> with: a with: b with: c with: d with: e [
	"1983 stops at four.  Pharo's collection tests build five-element
	 literals constantly -- it is the smallest size at which an ordering
	 bug shows -- so the fifth is worth having."
	^(self with: a with: b with: c with: d) add: e; yourself
]

{ #category : '*Collections-Protocol' }
Collection class >> with: a with: b with: c with: d with: e with: f [
	^(self with: a with: b with: c with: d with: e) add: f; yourself
]

{ #category : '*Collections-Protocol' }
Collection >> copyEmpty [
	"An empty collection of my kind.  Pharo asks this of a receiver rather
	 than of its class, so that a collection carrying extra state -- a
	 sort block, an equality block -- can hand that state on."
	^self species new
]

{ #category : '*Collections-Protocol' }
Collection >> as: aClass [
	"Me, converted to that class."
	^aClass withAll: self
]

{ #category : '*Collections-Protocol' }
Collection class >> withAll: aCollection [
	"A new collection of my kind holding all of those elements."
	^self new addAll: aCollection; yourself
]

{ #category : '*Collections-Protocol' }
Collection >> identityIncludes: anObject [
	"Whether anObject is one of my elements, by identity rather than by =.
	 Two equal Strings are one element to includes: and two to this."
	self do: [:each | anObject == each ifTrue: [^true]].
	^false
]

{ #category : '*Collections-Protocol' }
Collection >> flatCollect: aBlock as: aClass [
	"Collect, then flatten one level, into a collection of that class."
	| all |
	all := OrderedCollection new.
	self do: [:each | all addAll: (aBlock value: each)].
	^aClass withAll: all
]

{ #category : '*Collections-Protocol' }
Collection >> flatCollect: aBlock [
	^self flatCollect: aBlock as: self species
]

{ #category : '*Collections-Protocol' }
Collection >> select: selectBlock thenCollect: collectBlock [
	"Filter then map, in one pass.

	 Not the same as `(self select: a) collect: b' for every receiver: a
	 Dictionary's select: answers a Dictionary and its collect: a Bag, so
	 chaining them changes the species twice.  Doing both in one pass keeps
	 the answer the species collect: would give."
	| result |
	result := OrderedCollection new.
	self do: [:each |
		(selectBlock value: each)
			ifTrue: [result add: (collectBlock value: each)]].
	^self species withAll: result
]

{ #category : '*Collections-Protocol' }
Collection >> collect: collectBlock thenSelect: selectBlock [
	| result |
	result := OrderedCollection new.
	self do: [:each |
		| mapped |
		mapped := collectBlock value: each.
		(selectBlock value: mapped) ifTrue: [result add: mapped]].
	^self species withAll: result
]

{ #category : '*Collections-Protocol' }
Collection >> reduce: aBlock [
	"Fold with no initial value, the first element being the seed.

	 Collection, not SequenceableCollection: a Set has no order, and reduce:
	 is defined for it anyway because the block is meant to be associative.
	 Answering the elements in whatever order do: gives them is the honest
	 reading of that."
	| taken first |
	self isEmpty ifTrue: [^self errorEmptyCollection].
	first := true.
	taken := nil.
	self do: [:each |
		first
			ifTrue: [taken := each. first := false]
			ifFalse: [taken := aBlock value: taken value: each]].
	^taken
]

{ #category : '*Collections-Protocol' }
Collection >> includesAny: aCollection [
	"True if any element of that collection is one of mine."
	aCollection do: [:each | (self includes: each) ifTrue: [^true]].
	^false
]

{ #category : '*Collections-Protocol' }
Collection >> includesAll: aCollection [
	aCollection do: [:each | (self includes: each) ifFalse: [^false]].
	^true
]

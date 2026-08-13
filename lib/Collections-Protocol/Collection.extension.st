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

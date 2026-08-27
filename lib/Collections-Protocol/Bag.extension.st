"
The third member of the family, given the comparison the other two have.

Set>>= and Set>>hash were added to lib/Collections-Protocol/Collection's
extension for one reason: 1983 leaves a Set with Object's identity =, so two
Sets built the same way are never equal.  Dictionary compares structurally
too.  Bag -- the third unordered collection, holding the same elements at
the same multiplicities -- still answered Object>>=:

	(Bag withAll: #(1 1 2)) = (Bag withAll: #(1 1 2))       false

so no Bag could be a Dictionary key or a Set element, and `assert: x equals:
y' on two Bags could only fail.

add:withOccurrences: is here for a different reason.  It took any Integer,
including a negative one, and stored it -- so `(Bag new add: 3
withOccurrences: -2; yourself) size' answered -2, a collection with fewer
than no elements in it.  Every count in a Bag is a count of times something
was added; there is no adding a thing minus twice.
"
Extension { #name : 'Bag' }

{ #category : '*Collections-Protocol' }
Bag >> = aCollection [
	"Same class, same elements, same number of each.

	 Through occurrencesOf: rather than by comparing the two dictionaries,
	 because a Bag's dictionary is private and two Bags that agree about
	 every element and count are equal however each got there."
	self == aCollection ifTrue: [^true].
	self class == aCollection class ifFalse: [^false].
	self size = aCollection size ifFalse: [^false].
	contents keysAndValuesDo: [:element :count |
		(aCollection occurrencesOf: element) = count ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Bag >> hash [
	"Derived from my elements and their counts, so that it agrees with =.

	 Combined with bitXor:, which is commutative, because a Bag has no
	 order; the count is mixed into each element's hash rather than
	 xor-ed separately, so that two elements swapping counts is a
	 different Bag with a different hash."
	| h |
	h := self size hash.
	contents keysAndValuesDo: [:element :count |
		h := h bitXor: ((element hash bitAnd: 16rFFFFFFFF) + (count * 31))].
	^h bitAnd: 16rFFFFFFF
]

{ #category : '*Collections-Protocol' }
Bag >> add: newObject withOccurrences: anInteger [
	"1983's, refusing a count below zero.

	 Zero is allowed and does nothing, which is what `add: x withOccurrences:
	 n' means when n is the size of something empty."
	anInteger < 0 ifTrue: [
		^self error: 'a Bag cannot hold an element ', anInteger printString,
			' times'].
	anInteger = 0 ifTrue: [^newObject].
	(self includes: newObject)
		ifTrue: [contents at: newObject put: anInteger + (contents at: newObject)]
		ifFalse: [contents at: newObject put: anInteger].
	^newObject
]

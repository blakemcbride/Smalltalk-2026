"
Two sorted collections holding the same elements are equal; a nil sortBlock
is the default one; reSort keeps equal elements in their order; a sorted
collection refuses to be told where an element goes; and every copy of one
keeps its sort block.

The last two are one fault seen from two sides (Bugs3 B45).  A
SortedCollection is an OrderedCollection whose order is decided by its
sortBlock, and 1983 stops only at:put: -- addFirst:, addLast:, addAllFirst:,
addAllLast:, add:before: and add:after: are inherited unchanged, so `addFirst:
9' on 1 2 3 answered 9 1 2 3 and the next add: did a binary search over a
sequence that was no longer sorted.  The 1983 image itself refuses those
adders with shouldNotImplement; only this file's sources lost it.

The copies are the other side.  copyFrom:to: (which is also shallowCopy,
first:, last:, allButFirst and allButLast), reverse, copyWithout: and
SequenceableCollection's `,' all build their answer with `self species new:'
and fill it with add: -- and `SortedCollection new:' has the DEFAULT block.
So `copyFrom: 1 to: 2' of a collection sorted descending answered one sorted
ascending, printed in an order its own block contradicts, and the next add:
put the element where the ascending block said.  copy, select:, reject: and
copyWith: were right all along, because they go through copyEmpty.
"
Extension { #name : 'SortedCollection' }

{ #category : '*Collections-Protocol' }
SortedCollection >> = aCollection [
	"Same class, same elements, same order.

	 1983 also requires `sortBlock = aSortedCollection sortBlock', and a
	 block's = is identity -- so two collections built separately, sorted
	 identically, holding identical elements, are never equal, because the
	 two [:a :b | a <= b] blocks are two objects.  That is a distinction
	 without a difference as far as the CONTENTS go, and it is not what any
	 later dialect means by =.

	 The sort block still matters for what a collection DOES; it just does
	 not make two collections with the same elements in the same order
	 unequal."
	self class == aCollection class ifFalse: [^false].
	self size = aCollection size ifFalse: [^false].
	1 to: self size do: [:i |
		(self at: i) = (aCollection at: i) ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
SortedCollection >> sortBlock: aBlock [
	"Make aBlock my ordering, or the default `<=' when it is nil.

	 `SortedCollection sortBlock: nil' is how Pharo asks for the default
	 order, and it is what ported code writes; 1983 sent fixTemps to the
	 nil and the collection died in a doesNotUnderstand at its first add:
	 (Bugs3 B51).  fixTemps is kept because a BlockContext -- the bluebook
	 dialect's block -- still has to copy its home."
	sortBlock := (aBlock ifNil: [[:x :y | x <= y]]) fixTemps.
	self size > 0 ifTrue: [self reSort]
]

{ #category : '*Collections-Protocol' }
SortedCollection >> reSort [
	"Put my elements back in order, by a sort that leaves elements the
	 block calls equal in the order they were in.

	 1983's reSort is a quicksort over the underlying Array, and addAll:
	 uses it whenever the addition is large -- which asSortedCollection:
	 always is.  So `sort:' and `sorted:' in this library, both of which
	 came through here, and asSortedCollection: itself, all shuffled
	 elements the block ranked equal: pairs sorted by their first element
	 came back with their second elements in a different order every time
	 the size changed (Bugs3 B51).  A merge sort keeps them as they were,
	 provided the block answers true for equal elements, as `<=' does; a
	 strict `<' says equal elements may go either way, and gets that.

	 The elements are sorted in an Array and put back between firstIndex
	 and lastIndex, which costs one Array and is simpler than merging in
	 place in a collection whose storage starts wherever firstIndex says."
	| ordered |
	self size <= 1 ifTrue: [^self].
	ordered := self asArray sort: sortBlock.
	1 to: ordered size do: [:i |
		self basicAt: firstIndex + i - 1 put: (ordered at: i)]
]

{ #category : '*Collections-Protocol' }
SortedCollection >> addFirst: newObject [
	"Where an element goes is the sortBlock's decision, not the caller's.
	 The class's own add: and addAll: reach OrderedCollection's addLast:
	 with a super send, which this does not intercept."
	self shouldNotImplement
]

{ #category : '*Collections-Protocol' }
SortedCollection >> addLast: newObject [
	"See addFirst:."
	self shouldNotImplement
]

{ #category : '*Collections-Protocol' }
SortedCollection >> addAllFirst: aCollection [
	"See addFirst:."
	self shouldNotImplement
]

{ #category : '*Collections-Protocol' }
SortedCollection >> addAllLast: aCollection [
	"See addFirst:."
	self shouldNotImplement
]

{ #category : '*Collections-Protocol' }
SortedCollection >> add: newObject before: oldObject [
	"See addFirst:."
	self shouldNotImplement
]

{ #category : '*Collections-Protocol' }
SortedCollection >> add: newObject after: oldObject [
	"See addFirst:."
	self shouldNotImplement
]

{ #category : '*Collections-Protocol' }
SortedCollection >> addAll: aCollection [
	"1983's, guarded against being given myself.

	 1983 takes the fast path for a large argument -- addLast: each element
	 and re-sort -- and enumerating myself while appending to myself does
	 not stop where the elements stopped: 1 2 3 answered 1 1 1 2 2 3 3
	 (Bugs3 B49).  A copy is what the caller meant."
	aCollection == self ifTrue: [self addAll: self copy. ^aCollection].
	aCollection size > (self size // 3)
		ifTrue:
			["Faster to add the new elements and resort"
			aCollection do: [:each | super addLast: each].
			self reSort]
		ifFalse: ["Faster to add the elements individually in their proper places"
			aCollection do: [:each | self add: each]].
	^aCollection
]

{ #category : '*Collections-Protocol' }
SortedCollection >> copyFrom: startIndex to: endIndex [
	"Those of my elements, in my order, in a collection with my block.

	 Through copyEmpty, which is how a copy keeps its sortBlock; 1983's
	 `self species new:' has the default one.  This is also shallowCopy,
	 first:, last:, allButFirst and allButLast, all of which are written on
	 copyFrom:to:.  The elements are handed to addAll: as one Array, which
	 takes them by the append-and-resort path: appended in the right order
	 already, so the sort has nothing to move, where add: one at a time
	 would shift the whole collection down for each element."
	| copy |
	copy := self copyEmpty.
	endIndex < startIndex ifTrue: [^copy].
	copy addAll: ((startIndex to: endIndex) collect: [:index | self at: index]).
	^copy
]

{ #category : '*Collections-Protocol' }
SortedCollection >> reverse [
	"My elements in the opposite order, in a collection that will KEEP
	 them that way: its block is mine negated, so the next add: lands where
	 a reversed sequence says it should.

	 1983's answer was `self species new' filled with add:, which sorts the
	 elements ascending again -- the reverse of a descending collection was
	 itself, and the reverse of an ascending one was ascending too.  This
	 is Squeak's answer to the same question, and it is the only one under
	 which `reverse reverse' is the collection you started with."
	| reversed |
	reversed := self species sortBlock: [:a :b | (sortBlock value: a value: b) not].
	reversed addAll: self asArray reverse.
	^reversed
]

{ #category : '*Collections-Protocol' }
SortedCollection >> , aCollection [
	"A copy of me with that collection's elements added -- in MY order,
	 which is the only order I have.

	 SequenceableCollection's `,' is copyReplaceFrom:to:with:, which for an
	 OrderedCollection builds `self species new:' and add:s into it -- the
	 default block again.  The library's Collection>>, does the right thing
	 (copy, then add each) and is exactly what this says; it is here
	 because the 1983 SequenceableCollection method sits between the two
	 in the hierarchy and wins."
	| joined |
	joined := self copy.
	joined addAll: aCollection.
	^joined
]

{ #category : '*Collections-Protocol' }
SortedCollection >> copyWithout: oldElement [
	"A copy of me without every element equal to that one, keeping my
	 block.  1983's OrderedCollection version goes through `self species
	 new:'; reject: goes through copyEmpty, which is the whole difference."
	^self reject: [:each | each = oldElement]
]

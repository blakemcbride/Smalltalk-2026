"
The 1983 collection hierarchy, asserted from what a collection is supposed
to do rather than from what these ones happen to do.
"
Class {
	#name : 'St80CollectionTest',
	#superclass : 'TestCase',
	#category : 'Library-Tests'
}

{ #category : 'array' }
St80CollectionTest >> testArray [
	| a |
	a := Array new: 3.
	self assert: a size equals: 3.
	self assert: (a at: 1) equals: nil.
	a at: 1 put: 'x'.
	self assert: (a at: 1) equals: 'x'.
	self assert: (Array with: 1 with: 2) equals: #(1 2).
	self should: [a at: 0] raise: Error.
	self should: [a at: 4] raise: Error
]

{ #category : 'ordered' }
St80CollectionTest >> testOrderedCollectionGrowsBothEnds [
	| c |
	c := OrderedCollection new.
	c add: 2; add: 3; addFirst: 1.
	self assert: c asArray equals: #(1 2 3).
	self assert: c removeFirst equals: 1.
	self assert: c removeLast equals: 3.
	self assert: c size equals: 1.
	c addAll: #(9 8 7).
	self assert: c asArray equals: #(2 9 8 7).
	self assert: (c indexOf: 8) equals: 3
]

{ #category : 'ordered' }
St80CollectionTest >> testOrderedCollectionRemove [
	| c |
	c := OrderedCollection withAll: #(1 2 3 2).
	c remove: 2.
	self assert: c asArray equals: #(1 3 2).
	self assert: (c remove: 99 ifAbsent: ['gone']) equals: 'gone'.
	self should: [c remove: 99] raise: Error
]

{ #category : 'ordered' }
St80CollectionTest >> testOrderedCollectionRemoveIndex [
	"removeIndex: takes the index at: takes, and answers what it removed.
	 1983's took an index into the Array behind the collection and answered
	 the collection; `removeIndex: 2' on 1, 2, 3 left 2 and 3."
	| c |
	c := OrderedCollection withAll: #(1 2 3).
	self assert: (c removeIndex: 2) equals: 2.
	self assert: c asArray equals: #(1 3).
	self should: [c removeIndex: 3] raise: Error.
	self should: [c removeIndex: 0] raise: Error.
	self assert: c asArray equals: #(1 3).
	self assert: (c removeAt: 1) equals: 1.
	self assert: c asArray equals: #(3)
]

{ #category : 'ordered' }
St80CollectionTest >> testOrderedCollectionRemoveIndexAfterTheFrontMoved [
	"Once removeFirst and addFirst: have moved firstIndex, the index a
	 caller sees and the index into the Array differ by more than one --
	 which is exactly when a removeIndex: that took the Array's index went
	 wrong quietly.  And the two 1983 removers that always used the Array's
	 index still work, through removeBasicIndex:."
	| c |
	c := OrderedCollection new.
	1 to: 10 do: [:i | c add: i].
	c removeFirst; removeFirst; addFirst: 0.
	self assert: c asArray equals: #(0 3 4 5 6 7 8 9 10).
	self assert: (c removeIndex: 2) equals: 3.
	self assert: c asArray equals: #(0 4 5 6 7 8 9 10).
	self assert: (c removeAllSuchThat: [:e | e even]) asArray
		equals: #(0 4 6 8 10).
	self assert: c asArray equals: #(5 7 9).
	self assert: (c remove: 7) equals: 7.
	self assert: c asArray equals: #(5 9)
]

{ #category : 'ordered' }
St80CollectionTest >> testOrderedCollectionGrowsPastItsStart [
	"addFirst: many times has to grow the front, not just the back."
	| c |
	c := OrderedCollection new.
	20 to: 1 by: -1 do: [:i | c addFirst: i].
	self assert: c size equals: 20.
	self assert: c first equals: 1.
	self assert: c last equals: 20.
	self assert: (c at: 10) equals: 10
]

{ #category : 'set' }
St80CollectionTest >> testSet [
	| s |
	s := Set new.
	s add: 1; add: 2; add: 1.
	self assert: s size equals: 2.
	self assert: (s includes: 1).
	self deny: (s includes: 3).
	s remove: 1.
	self assert: s size equals: 1.
	self assert: (Set withAll: #(1 1 2 2 3)) size equals: 3
]

{ #category : 'set' }
St80CollectionTest >> testSetUsesEqualityNotIdentity [
	| s |
	s := Set new.
	s add: 'abc' copy.
	self assert: (s includes: 'abc' copy)
]

{ #category : 'set' }
St80CollectionTest >> testSetGrowsPastItsInitialCapacity [
	| s |
	s := Set new.
	1 to: 200 do: [:i | s add: i].
	self assert: s size equals: 200.
	1 to: 200 do: [:i | self assert: (s includes: i)].
	self deny: (s includes: 201)
]

{ #category : 'bag' }
St80CollectionTest >> testBagCounts [
	| b |
	b := Bag new.
	b add: 1; add: 1; add: 2.
	self assert: b size equals: 3.
	self assert: (b occurrencesOf: 1) equals: 2.
	b remove: 1.
	self assert: (b occurrencesOf: 1) equals: 1
]

{ #category : 'dictionary' }
St80CollectionTest >> testDictionary [
	| d |
	d := Dictionary new.
	d at: #a put: 1.
	d at: #b put: 2.
	self assert: (d at: #a) equals: 1.
	self assert: d size equals: 2.
	self assert: (d at: #z ifAbsent: [99]) equals: 99.
	self assert: (d includesKey: #a).
	self deny: (d includesKey: #z).
	self assert: (d keyAtValue: 2) equals: #b.
	d removeKey: #a.
	self assert: d size equals: 1.
	self should: [d at: #a] raise: Error
]

{ #category : 'dictionary' }
St80CollectionTest >> testDictionaryReplacesRatherThanDuplicates [
	| d |
	d := Dictionary new.
	d at: #a put: 1.
	d at: #a put: 2.
	self assert: d size equals: 1.
	self assert: (d at: #a) equals: 2
]

{ #category : 'dictionary' }
St80CollectionTest >> testDictionaryGrows [
	| d |
	d := Dictionary new.
	1 to: 200 do: [:i | d at: i put: i * i].
	self assert: d size equals: 200.
	1 to: 200 do: [:i | self assert: (d at: i) equals: i * i]
]

{ #category : 'dictionary' }
St80CollectionTest >> testDictionaryIterationSeesValues [
	| d sum |
	d := Dictionary new.
	d at: #a put: 1; at: #b put: 2.
	sum := 0.
	d do: [:v | sum := sum + v].
	self assert: sum equals: 3.
	sum := 0.
	d keysAndValuesDo: [:k :v | sum := sum + v].
	self assert: sum equals: 3.
	self assert: d keys size equals: 2.
	self assert: d values size equals: 2
]

{ #category : 'sorted' }
St80CollectionTest >> testSortedCollection [
	| s |
	s := SortedCollection sortBlock: [:a :b | a <= b].
	s addAll: #(3 1 2).
	self assert: s asArray equals: #(1 2 3).
	s add: 0.
	self assert: s first equals: 0.
	self assert: #(3 1 2) asSortedCollection asArray equals: #(1 2 3).
	self assert: ((SortedCollection sortBlock: [:a :b | a > b])
		addAll: #(1 3 2); yourself) asArray equals: #(3 2 1)
]

{ #category : 'enumerating' }
St80CollectionTest >> testEnumeration [
	self assert: (#(1 2 3) collect: [:x | x * 10]) equals: #(10 20 30).
	self assert: (#(1 2 3 4) select: [:x | x even]) equals: #(2 4).
	self assert: (#(1 2 3 4) reject: [:x | x even]) equals: #(1 3).
	self assert: (#(1 2 3) detect: [:x | x > 1]) equals: 2.
	self assert: (#(1 2 3) detect: [:x | x > 9] ifNone: [0]) equals: 0.
	self assert: (#(1 2 3) inject: 0 into: [:a :b | a + b]) equals: 6.
	self should: [#(1 2 3) detect: [:x | x > 9]] raise: Error
]

{ #category : 'enumerating' }
St80CollectionTest >> testEnumerationPreservesKind [
	self assert: (#(1 2 3) collect: [:x | x]) class equals: Array.
	self assert: ((OrderedCollection withAll: #(1 2)) collect: [:x | x]) class
		equals: OrderedCollection.
	self assert: ((Set withAll: #(1 2)) collect: [:x | x]) class equals: Set
]

{ #category : 'copying' }
St80CollectionTest >> testCopyingIsShallowButSeparate [
	| a b |
	a := OrderedCollection withAll: #(1 2 3).
	b := a copy.
	b add: 4.
	self assert: a size equals: 3.
	self assert: b size equals: 4
]

{ #category : 'converting' }
St80CollectionTest >> testConversions [
	self assert: #(1 2 3) asOrderedCollection asArray equals: #(1 2 3).
	self assert: (Set withAll: #(1)) asArray equals: #(1).
	self assert: #(3 1 2) asSortedCollection asArray equals: #(1 2 3).
	self assert: #(1 1 2) asBag size equals: 3.
	self assert: #(1 1 2) asSet size equals: 2
]

{ #category : 'sequenceable' }
St80CollectionTest >> testSequenceableProtocol [
	self assert: (#(1 2 3) copyFrom: 2 to: 3) equals: #(2 3).
	self assert: (#(1 2) , #(3)) equals: #(1 2 3).
	self assert: #(1 2 3) reverse equals: #(3 2 1).
	self assert: #(1 2 3) first equals: 1.
	self assert: #(1 2 3) last equals: 3.
	self assert: (#(1 2 3) indexOf: 2) equals: 2.
	self assert: (#(1 2 3) indexOf: 9) equals: 0.
	self assert: (#(1 2 3) includes: 2).
	self assert: #() isEmpty.
	self deny: #(1) isEmpty
]

{ #category : 'sequenceable' }
St80CollectionTest >> testWithCollect [
	self assert: (#(1 2) with: #(10 20) collect: [:a :b | a + b])
		equals: #(11 22)
]

{ #category : 'sorting' }
St80CollectionTest >> testSortIsStable [
	"Elements the block ranks equal keep the order they were in, through
	 sort:, sorted: and asSortedCollection: alike -- all three sorted
	 through 1983's quicksort and shuffled them (Bugs3 B51)."
	| pairs inOrder |
	pairs := (1 to: 300) collect: [:i | i \\ 7 -> i].
	inOrder := [:a :b | a key < b key or: [a key = b key and: [a value < b value]]].
	self assert: ((pairs copy sort: [:a :b | a key <= b key]) isSortedBy: inOrder).
	self assert: ((pairs sorted: [:a :b | a key <= b key]) isSortedBy: inOrder).
	self assert: ((pairs asSortedCollection: [:a :b | a key <= b key]) asArray
		isSortedBy: inOrder).
	self assert: #(3 1 2) copy sort equals: #(1 2 3).
	self assert: 'hello' copy sort equals: 'ehllo'.
	self assert: ((SortedCollection sortBlock: nil) add: 3; add: 1; yourself) asArray
		equals: #(1 3)
]

{ #category : 'associations' }
St80CollectionTest >> testAssociationsCompareBothHalves [
	"1983's LookupKey>>= compared keys alone, so a Set of Associations lost
	 every pair whose key it had seen (Bugs3 B51).  The Dictionary at the
	 end is the reason the hash stays the key's: removeKey: re-probes by
	 the Association's hash and looks up by the key's."
	| d |
	self deny: (1->2) = (1->3).
	self assert: (1->2) = (1->2).
	self assert: (1->2) hash equals: (1->2) hash.
	self deny: ((Set with: 1->2) includes: 1->3).
	self assert: (Set with: 1->2 with: 1->3) size equals: 2.
	d := Dictionary new.
	1 to: 50 do: [:i | d at: i put: i * i].
	1 to: 50 by: 2 do: [:i | d removeKey: i].
	self assert: ((2 to: 50 by: 2) inject: 0 into: [:sum :i | sum + (d at: i)])
		equals: 22100.
	self assert: (d includesAssociation: 2->4).
	self deny: (d includesAssociation: 2->5)
]

{ #category : 'linked lists' }
St80CollectionTest >> testLinkedListRefusesWhatBreaksIt [
	"A Link added twice pointed at itself and size never returned; a
	 non-Link went into the chain; at: indexed the list's own slots; copy
	 shared one chain between two lists (Bugs3 B51)."
	| l a b c |
	l := LinkedList new.
	a := Link new.
	b := Link new.
	l add: a.
	self should: [l add: a] raise: Error.
	self should: [l add: 3] raise: Error.
	self assert: l size equals: 1.
	l add: b.
	self assert: (l at: 1) == a.
	self assert: (l at: 2) == b.
	self should: [l at: 3] raise: Error.
	c := l copy.
	c add: Link new.
	self assert: l size equals: 2.
	self assert: c size equals: 3.
	self deny: c first == l first.
	l remove: a.
	l add: a.
	self assert: l size equals: 2
]

{ #category : 'copying' }
St80CollectionTest >> testCopyingPastTheEnds [
	"An inverted range copies nothing, as OrderedCollection's always did; a
	 negative count is refused in words; copyFrom: alone is the tail, not
	 Object>>copyFrom: (Bugs3 B51)."
	self assert: #() allButFirst equals: #().
	self assert: (#(1 2 3) allButFirst: 5) equals: #().
	self assert: ('abc' allButLast: 7) equals: ''.
	self should: [#(1 2 3) first: -1] raise: Error.
	self should: [#(1 2 3) last: -1] raise: Error.
	self assert: (#(1 2 3) copyFrom: 2) equals: #(2 3).
	self assert: ('hello' copyFrom: 3) equals: 'llo'
]

{ #category : 'concatenation' }
St80CollectionTest >> testConcatenationRefusesNonCollections [
	"Object>>size is 0 for a SmallInteger and for nil, so `'abc' , 3' was
	 'abc' (Bugs3 B51)."
	self should: ['abc' , 3] raise: Error.
	self should: ['abc' , nil] raise: Error.
	self assert: 'abc' , 'def' equals: 'abcdef'.
	self assert: #(1 2) , (OrderedCollection with: 3) equals: #(1 2 3)
]

{ #category : 'dictionaries' }
St80CollectionTest >> testDictionaryNewFromDictionary [
	"newFrom: takes a Dictionary as well as a collection of Associations
	 (Bugs3 B51).  That the 1983 one copies the bindings rather than
	 sharing them is asserted in tests/unit/test_image.c and not here: the
	 pharo-collections and pharo-weak profiles run Pharo's Dictionary,
	 whose own newFrom: is loaded over this library's and shares them."
	| a b |
	a := Dictionary new at: 1 put: 2; at: 3 put: 4; yourself.
	b := Dictionary newFrom: a.
	self assert: b size equals: 2.
	self assert: (b at: 1) equals: 2.
	self assert: (b at: 3) equals: 4.
	self assert: (Dictionary newFrom: {1->2. 3->4}) size equals: 2.
	self assert: ((Dictionary newFrom: {1->2. 3->4}) at: 3) equals: 4
]

{ #category : 'copying' }
St80CollectionTest >> testDictionaryCopyHasItsOwnAssociations [
	"A shallow copy of a Dictionary copied the slot Array and shared the
	 Associations in it, so a store into the copy was a store into the
	 original; select:, reject: and the library's own `,' shared them the
	 same way (Bugs3 B42)."
	| d d2 |
	d := Dictionary new.
	d at: #k put: 1.
	d2 := d copy.
	d2 at: #k put: 2.
	self assert: (d at: #k) equals: 1.
	self assert: (d2 at: #k) equals: 2.
	d2 := d select: [:each | true].
	d2 at: #k put: 3.
	self assert: (d at: #k) equals: 1.
	d2 := d reject: [:each | false].
	d2 at: #k put: 4.
	self assert: (d at: #k) equals: 1.
	d2 := d , (Dictionary new at: #k put: 5; yourself).
	self assert: (d at: #k) equals: 1.
	self assert: (d2 at: #k) equals: 5.
	d := IdentityDictionary new.
	d at: #k put: 1.
	d2 := d copy.
	d2 at: #k put: 2.
	self assert: (d at: #k) equals: 1
]

{ #category : 'copying' }
St80CollectionTest >> testBagCopyHasItsOwnCounts [
	"A Bag is one Dictionary, and a shallow copy shared it: adding to the
	 copy counted in the original, and removing from the copy removed from
	 both (Bugs3 B43)."
	| b b2 |
	b := Bag with: 1.
	b2 := b copy.
	b2 add: 2.
	self assert: b size equals: 1.
	self assert: b2 size equals: 2.
	b2 remove: 1.
	self assert: b size equals: 1.
	self assert: (b occurrencesOf: 1) equals: 1.
	self assert: (b2 occurrencesOf: 1) equals: 0
]

{ #category : 'dictionaries' }
St80CollectionTest >> testIdentityDictionaryHasNoGhostForNilKey [
	"nil in a 1983 IdentityDictionary's key slots is what an empty slot
	 looks like: `at: nil put:' counted an entry nothing could find, and a
	 second store made the count 0 (Bugs3 B44).  1983's refuses nil now.
	 Pharo's IdentityDictionary, which profiles/pharo-collections and
	 pharo-weak run this test against, keeps Associations and holds nil
	 as a key perfectly well.  What must never happen in either is the
	 ghost: a store that is counted and cannot be found.  A Dictionary
	 holds nil as a key in both."
	| d stored |
	d := IdentityDictionary new.
	stored := [d at: nil put: 1. true] on: Error do: [:e | false].
	stored
		ifTrue: [
			self assert: d size equals: 1.
			self assert: (d at: nil) equals: 1.
			self assert: (d includesKey: nil).
			self assert: d keys size equals: 1.
			d at: nil put: 2.
			self assert: d size equals: 1.
			self assert: (d at: nil) equals: 2]
		ifFalse: [
			self assert: d size equals: 0.
			self deny: (d includesKey: nil).
			self assert: (d at: nil ifAbsent: ['absent']) equals: 'absent'].
	d at: #a put: 1.
	self assert: (d at: #a) equals: 1.
	d := Dictionary new.
	d at: nil put: 7.
	self assert: (d at: nil) equals: 7.
	self assert: d keys size equals: 1
]

{ #category : 'sorted' }
St80CollectionTest >> testSortedCollectionRefusesPositionalAdds [
	"Where an element goes is the sortBlock's decision.  1983's image
	 refuses addFirst: and the rest with shouldNotImplement; the sources
	 here had lost that, and `addFirst: 9' on 1 2 3 answered 9 1 2 3
	 (Bugs3 B45).  The collection is untouched by a refused add."
	| s |
	s := SortedCollection withAll: #(3 1 2).
	self should: [s addFirst: 9] raise: Error.
	self should: [s addLast: 0] raise: Error.
	self should: [s addAllFirst: #(9 8)] raise: Error.
	self should: [s addAllLast: #(0)] raise: Error.
	self should: [s add: 9 before: 1] raise: Error.
	self should: [s add: 9 after: 1] raise: Error.
	self should: [s add: 9 beforeIndex: 1] raise: Error.
	self should: [s at: 1 put: 9] raise: Error.
	self assert: s asArray equals: #(1 2 3).
	s add: 0.
	self assert: s asArray equals: #(0 1 2 3)
]

{ #category : 'sorted' }
St80CollectionTest >> testSortedCollectionCopiesKeepTheirBlock [
	"copyFrom:to: -- and with it shallowCopy, first:, last:, allButFirst
	 and allButLast -- reverse, copyWithout: and `,' all built their answer
	 with `species new:', which has the DEFAULT block, so a copy of a
	 descending collection was ascending and its next add: went where the
	 wrong block said (Bugs3 B45).  Each answer here is checked by what
	 an add: does to it afterwards, which is the only test that matters."
	| s |
	s := SortedCollection sortBlock: [:a :b | a > b].
	s addAll: #(3 1 2).
	self assert: s asArray equals: #(3 2 1).
	self assert: ((s copyFrom: 1 to: 2) add: 5; yourself) asArray equals: #(5 3 2).
	self assert: (s shallowCopy add: 0; yourself) asArray equals: #(3 2 1 0).
	self assert: (s copy add: 5; yourself) asArray equals: #(5 3 2 1).
	self assert: ((s first: 2) add: 5; yourself) asArray equals: #(5 3 2).
	self assert: (s allButFirst add: 5; yourself) asArray equals: #(5 2 1).
	self assert: ((s , #(7)) add: 5; yourself) asArray equals: #(7 5 3 2 1).
	self assert: ((s copyWithout: 2) add: 5; yourself) asArray equals: #(5 3 1).
	self assert: (s reverse add: 5; yourself) asArray equals: #(1 2 3 5).
	self assert: s reverse reverse asArray equals: s asArray.
	self assert: (s copyFrom: 3 to: 2) isEmpty
]

{ #category : 'adding' }
St80CollectionTest >> testAddAllAndRemoveAllOfSelf [
	"Enumerating a collection while adding to it or removing from it walks
	 over its own changes: `removeAll: self' on 1 2 3 4 left 2 4, and
	 `addAll: self' on 1 2 3 added four copies of three (Bugs3 B49).  The
	 argument is copied first when it is the receiver."
	| oc s b |
	oc := OrderedCollection withAll: #(1 2 3 4).
	oc removeAll: oc.
	self assert: oc isEmpty.
	oc := OrderedCollection withAll: #(1 2 3).
	oc addAll: oc.
	self assert: oc asArray equals: #(1 2 3 1 2 3).
	oc addAllFirst: oc.
	self assert: oc size equals: 12.
	s := SortedCollection withAll: #(1 2 3).
	s addAll: s.
	self assert: s asArray equals: #(1 1 2 2 3 3).
	s := Set withAll: #(1 2 3).
	s removeAll: s.
	self assert: s isEmpty.
	b := Bag withAll: #(1 2 3).
	b addAll: b.
	self assert: b size equals: 6.
	b removeAll: b.
	self assert: b isEmpty
]

{ #category : 'intervals' }
St80CollectionTest >> testIntervalRefusesZeroStep [
	"`(1 to: 10 by: 0) do:' never passed 10 and never stopped (Bugs3
	 B14); an Interval with no step is refused where it is made."
	self should: [1 to: 10 by: 0] raise: Error.
	self should: [Interval from: 1 to: 10 by: 0] raise: Error.
	self assert: (1 to: 10 by: 3) asArray equals: #(1 4 7 10).
	self assert: (10 to: 1 by: -3) asArray equals: #(10 7 4 1)
]

{ #category : 'copying' }
St80CollectionTest >> testDeepCopyStopsAtCode [
	"deepCopy of anything holding a block followed the block's context
	 to its sender and from there through the whole image, for 47 seconds
	 and 788 MB, and then failed (Bugs3 B47).  Blocks, contexts, classes,
	 methods, processes and semaphores are shared by a deep copy; a
	 nested collection is still copied all the way down."
	| s a b |
	s := SortedCollection sortBlock: [:x :y | x > y].
	s addAll: #(3 1 2).
	self assert: s deepCopy asArray equals: #(3 2 1).
	self assert: (s deepCopy add: 5; yourself) asArray equals: #(5 3 2 1).
	self assert: [:x | x] deepCopy class equals: BlockClosure.
	self assert: Object deepCopy == Object.
	self assert: Object class deepCopy == Object class.
	self assert: (Object >> #copy) deepCopy == (Object >> #copy).
	self assert: Processor activeProcess deepCopy == Processor activeProcess.
	self assert: thisContext deepCopy == thisContext.
	a := Array with: (OrderedCollection with: 'x').
	b := a deepCopy.
	self deny: b first == a first.
	self deny: b first first == a first first.
	self assert: b first first equals: 'x'
]

{ #category : 'bugs4' }
St80CollectionTest >> testSortedAnswersTheSameKindOfCollection [
	"`sorted' and `sorted:' built the answer with `self species new: n' and
	 filled it with at:put:, which only works for a kind whose `new: n' is
	 already n elements long.  `(OrderedCollection withAll: #(3 1 2))
	 sorted' answered `attempt to index non-existent element', a
	 SortedCollection answered `you must use add:' and a LinkedList failed
	 its primitive -- while the manual promises the same kind of
	 collection (Bugs4 DOCS-1)."
	| oc sc |
	oc := OrderedCollection withAll: #(3 1 2).
	self assert: oc sorted asArray equals: #(1 2 3).
	self assert: oc sorted class equals: OrderedCollection.
	self assert: (oc sorted: [:a :b | a > b]) asArray equals: #(3 2 1).
	self assert: (oc sorted: [:a :b | a > b]) class equals: OrderedCollection.
	self assert: oc asArray equals: #(3 1 2).
	sc := SortedCollection withAll: #(3 1 2).
	self assert: sc sorted asArray equals: #(1 2 3).
	self assert: sc sorted class equals: SortedCollection.
	self assert: (sc sorted: [:a :b | a > b]) asArray equals: #(3 2 1).
	self assert: LinkedList new sorted class equals: LinkedList.
	self assert: LinkedList new sorted isEmpty.
	"The arrayed kinds answered before and must go on answering."
	self assert: #(3 1 2) sorted equals: #(1 2 3).
	self assert: 'cba' sorted equals: 'abc'.
	self assert: (Interval from: 1 to: 5) sorted equals: #(1 2 3 4 5).
	"with:collect: had the same shape and the same three failures."
	self assert: (oc with: #(1 2 3) collect: [:a :b | a + b]) asArray equals: #(4 3 5).
	self assert: (oc with: #(1 2 3) collect: [:a :b | a + b]) class equals: OrderedCollection.
	self assert: (#(3 1 2) with: #(1 2 3) collect: [:a :b | a + b]) equals: #(4 3 5).
	self assert: ('abc' with: 'abc' collect: [:a :b | a]) equals: 'abc'
]

{ #category : 'bugs4' }
St80CollectionTest >> testOrderedCollectionInsertsAtEitherEnd [
	"insert:before: always made room at the FRONT and shifted every
	 element up to the slot, so an insert at the end copied the whole
	 collection: ten thousand ascending adds to a SortedCollection took
	 26.7 seconds where ten thousand descending took 0.18 (Bugs4 COLL-2).
	 It appends, prepends, or shifts the nearer side now.  What is
	 asserted here is that it still puts things where it always put them --
	 the timings are in the audit; correctness is here."
	| oc sc |
	oc := OrderedCollection new.
	#(1 2 3) do: [:e | oc addLast: e].
	oc add: 99 beforeIndex: 1.
	self assert: oc asArray equals: #(99 1 2 3).
	oc := OrderedCollection withAll: #(1 2 3).
	oc add: 99 beforeIndex: 2.
	self assert: oc asArray equals: #(1 99 2 3).
	oc := OrderedCollection withAll: #(1 2 3).
	oc add: 99 beforeIndex: 4.
	self assert: oc asArray equals: #(1 2 3 99).
	oc := OrderedCollection withAll: #(1 2 3 4 5 6 7).
	oc add: 99 before: 5.
	self assert: oc asArray equals: #(1 2 3 4 99 5 6 7).
	oc := OrderedCollection withAll: #(1 2 3 4 5 6 7).
	oc add: 99 after: 5.
	self assert: oc asArray equals: #(1 2 3 4 5 99 6 7).
	"Insert in the middle, then work both ends: an insertion that left
	 firstIndex or lastIndex wrong would show up here and nowhere else."
	oc := OrderedCollection withAll: #(1 2 3 4 5 6 7).
	oc add: 99 beforeIndex: 4.
	oc removeFirst.
	oc removeLast.
	oc addFirst: 0.
	oc addLast: 8.
	self assert: oc asArray equals: #(0 2 3 99 4 5 6 8).
	"Every insertion point of every size up to a few growths."
	1 to: 10 do: [:n |
		1 to: n + 1 do: [:at |
			oc := OrderedCollection new.
			1 to: n do: [:i | oc addLast: i].
			oc add: 0 beforeIndex: at.
			self assert: oc size equals: n + 1.
			self assert: (oc at: at) equals: 0.
			self assert: (oc copyWithout: 0) asArray equals: (1 to: n) asArray]].
	"Ascending and descending both come out sorted, at a size that used
	 to be quadratic."
	sc := SortedCollection new.
	1 to: 60 do: [:i | sc add: i].
	self assert: sc asArray equals: (1 to: 60) asArray.
	sc := SortedCollection new.
	60 to: 1 by: -1 do: [:i | sc add: i].
	self assert: sc asArray equals: (1 to: 60) asArray.
	sc := SortedCollection sortBlock: [:a :b | a > b].
	1 to: 60 do: [:i | sc add: i].
	self assert: sc asArray equals: (1 to: 60) asArray reverse.
	"The sizes at which the old code took half a minute are in
	 tests/unit/test_image.c's test_bugs4_collections; a suite that runs
	 under one bytecode budget for all of it is no place for them."
]

{ #category : 'bugs4' }
St80CollectionTest >> testSymbolTableGrows [
	"USTable had 512 buckets, fixed in C, and Symbol class>>intern: copies
	 the whole bucket for each new symbol -- so interning slowed down as
	 the image filled: 10,000 new symbols took 0.85 seconds and the next
	 40,000 took 10.3 (Bugs4 COLL-1).  It grows and rehashes now.  What
	 matters is that every symbol stays FINDABLE and IDENTICAL across the
	 rehash, since a Symbol that is not identical to itself cannot be sent
	 or looked up."
	| before after names |
	before := (Symbol classPool at: #USTable) size.
	names := (1 to: 120) collect: [:i | 'bugs4sym' , i printString].
	names do: [:each | each asSymbol].
	after := (Symbol classPool at: #USTable) size.
	self assert: after >= before.
	names do: [:each |
		self assert: (Symbol lookup: each) == each asSymbol].
	self assert: (Symbol lookup: 'bugs4sym-not-there') isNil.
	"The symbols the image was built with are still there, and so is the
	 identity the compiler depends on.  Interning at the size that showed
	 the quadratic is in tests/unit/test_image.c."
	self assert: #printOn: == 'printOn:' asSymbol.
	self assert: (Symbol lookup: 'printOn:') == #printOn:.
	self assert: #+ == '+' asSymbol
]

{ #category : 'bugs4' }
St80CollectionTest >> testEmptySymbolHasNoArguments [
	"Symbol>>numArgs read `self at: 1' without looking, so `#'''' numArgs'
	 raised SubscriptOutOfBounds where every dialect answers 0 (Bugs4
	 COLL-3)."
	self assert: '' asSymbol numArgs equals: 0.
	self assert: #foo numArgs equals: 0.
	self assert: #foo: numArgs equals: 1.
	self assert: #foo:bar: numArgs equals: 2.
	self assert: #+ numArgs equals: 1
]

{ #category : 'bugs4' }
St80CollectionTest >> testCopyReplaceAllNeedsCollections [
	"The replacement is measured with `size', and `Character>>size' is
	 zero, so `'abc' copyReplaceAll: 'b' with: $z' found the b, put
	 nothing in its place and answered 'ac' -- a String of the right kind
	 with the wrong contents, and nothing said (Bugs4 COLL-4).  Refused
	 on both sides rather than coerced on one: coercing the replacement
	 alone would leave `copyReplaceAll: $b with: 'z'' silently answering
	 the receiver unchanged."
	self assert: ('abc' copyReplaceAll: 'b' with: 'zz') equals: 'azzc'.
	self assert: ('abcabc' copyReplaceAll: 'b' with: 'X') equals: 'aXcaXc'.
	self assert: ('abc' copyReplaceAll: 'b' with: '') equals: 'ac'.
	self should: ['abc' copyReplaceAll: 'b' with: $z] raise: Error.
	self should: ['abc' copyReplaceAll: $b with: 'z'] raise: Error.
	self should: ['abc' copyReplaceAll: 'b' with: 3] raise: Error.
	self assert: (#(1 2 3) copyReplaceAll: #(2) with: #(9 9)) equals: #(1 9 9 3)
]

{ #category : 'bugs4' }
St80CollectionTest >> testFindStringClampsItsStart [
	"A start PAST the end answered 0 cleanly, because the loop's upper
	 bound was already below its lower one; a start of zero made the first
	 thing the loop did `self at: 0' and raised (Bugs4 COLL-5).  One end
	 of the string answered and the other raised."
	self assert: ('abc' findString: 'b' startingAt: 0) equals: 2.
	self assert: ('abc' findString: 'b' startingAt: -5) equals: 2.
	self assert: ('abc' findString: 'b' startingAt: 1) equals: 2.
	self assert: ('abc' findString: 'b' startingAt: 2) equals: 2.
	self assert: ('abc' findString: 'b' startingAt: 3) equals: 0.
	self assert: ('abc' findString: 'b' startingAt: 9) equals: 0.
	self assert: ('abc' findString: '' startingAt: 0) equals: 0.
	self assert: (#(1 2 3) indexOfSubCollection: #(2) startingAt: 0) equals: 2.
	self assert: (#(1 2 3) indexOfSubCollection: #(2) startingAt: 9) equals: 0
]

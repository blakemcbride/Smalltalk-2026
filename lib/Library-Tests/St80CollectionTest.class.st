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

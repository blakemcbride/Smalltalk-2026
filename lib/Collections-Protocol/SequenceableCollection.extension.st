"
Order-dependent protocol.  reverse already exists here and answers a copy;
reversed is Pharo's name for the same thing, and both are common enough in
ported source that having only one of them is a papercut.
"
Extension { #name : 'SequenceableCollection' }

{ #category : '*Collections-Protocol' }
SequenceableCollection >> reversed [
	^self reverse
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButFirst [
	^self copyFrom: 2 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButLast [
	^self copyFrom: 1 to: self size - 1
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButFirst: n [
	^self copyFrom: n + 1 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButLast: n [
	^self copyFrom: 1 to: self size - n
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> first: n [
	^self copyFrom: 1 to: n
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> last: n [
	^self copyFrom: self size - n + 1 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> with: aCollection collect: aBlock [
	"Element by element, in step.  Signals if the two are different
	 lengths, because quietly stopping at the shorter one is how a
	 mismatch turns into a wrong answer instead of an error."
	| result |
	aCollection size = self size
		ifFalse: [^self error: 'collections are not the same size'].
	result := self species new: self size.
	1 to: self size do: [:i |
		result at: i put: (aBlock value: (self at: i)
		                          value: (aCollection at: i))].
	^result
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> doWithIndex: aBlock [
	1 to: self size do: [:i | aBlock value: (self at: i) value: i]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> withIndexDo: aBlock [
	1 to: self size do: [:i | aBlock value: (self at: i) value: i]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> keysAndValuesDo: aBlock [
	"The index first, the element second -- the opposite order from
	 withIndexDo:, which is Pharo's convention and not a slip."
	1 to: self size do: [:i | aBlock value: i value: (self at: i)]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> sorted: aBlock [
	"Sorted, and the SAME KIND of collection -- 'hello' sorted is 'ehllo'
	 and not an Array of five Characters.

	 Collection>>sorted answers an Array because a sorted Set is not a
	 Set and there is nothing better it could answer.  Here there is:
	 order is what a sequenceable collection is for, so sorting one has
	 an obvious answer and the species is it.  Pharo's own doctest for
	 this method is what said so."
	| ordered result |
	ordered := (self asSortedCollection: aBlock) asArray.
	result := self species new: ordered size.
	1 to: ordered size do: [:i | result at: i put: (ordered at: i)].
	^result
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> sorted [
	^self sorted: [:a :b | a <= b]
]

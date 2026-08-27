"
removeIndex: takes the index you can see, and answers what it removed.

1983's did neither.  An OrderedCollection keeps its elements in the middle
of a larger Array, between firstIndex and lastIndex, and 1983's removeIndex:
took an index into THAT Array -- it was private, and both of its callers,
remove:ifAbsent: and removeAllSuchThat:, walked from firstIndex and handed
it what they found.  It answered the collection.  Sent from outside with
the index a client has, it removed an element from somewhere else entirely
and said nothing: `removeIndex: 2' on an OrderedCollection holding 1, 2, 3
left 2 and 3 behind, and which element it took depended on how the
collection had grown.  lib/Json's JSONArray>>removeAt: met it first, and
its first version rebuilt the whole collection to avoid the message
(doc/JSON.md).

A selector that reads as public and is private is a trap for every reader
after the first, so the name now means what it says.  The 1983 body lives
on as removeBasicIndex:, named for basicAt: and basicSize, which are the
other messages that speak in that Array's indices; the two 1983 callers are
re-pointed at it, unchanged otherwise.  removeAt: is the same operation
under the name Pharo gives it, so that ported code -- removeDuplicates in
pharo/Collections-Unordered sends it -- finds it.
"
Extension { #name : 'OrderedCollection' }

{ #category : '*Collections-Protocol' }
OrderedCollection >> removeIndex: anInteger [
	"Remove my element at anInteger -- the index at: uses -- and answer it.
	 An index I have no element at is an error, the same one at: raises."
	| removed |
	removed := self at: anInteger.
	self removeBasicIndex: anInteger + firstIndex - 1.
	^removed
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> removeAt: anInteger [
	"removeIndex: under the name Pharo uses for it."
	^self removeIndex: anInteger
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> removeBasicIndex: removedIndex [
	"Close up the gap at removedIndex, an index into my underlying Array.
	 This is 1983's removeIndex:, renamed for what its argument is."
	| index |
	index := removedIndex.
	[index < lastIndex]
		whileTrue:
			[self basicAt: index put: (self basicAt: index + 1).
			index := index + 1].
	self basicAt: lastIndex put: nil.
	lastIndex := lastIndex - 1
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> remove: oldObject ifAbsent: absentBlock [
	"1983's, sending removeBasicIndex: where it sent removeIndex:."
	| index |
	index := firstIndex.
	[index <= lastIndex]
		whileTrue:
			[oldObject = (self basicAt: index)
				ifTrue:
					[self removeBasicIndex: index.
					^oldObject]
				ifFalse: [index := index + 1]].
	^absentBlock value
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> removeAllSuchThat: aBlock [
	"Evaluate aBlock for each element of the receiver.  Remove each element
	 for which aBlock evaluates to true, and answer them.  1983's, sending
	 removeBasicIndex: where it sent removeIndex:."
	| index element newCollection |
	newCollection := self species new.
	index := firstIndex.
	[index <= lastIndex]
		whileTrue:
			[element := self basicAt: index.
			(aBlock value: element)
				ifTrue:
					[newCollection add: element.
					self removeBasicIndex: index]
				ifFalse: [index := index + 1]].
	^newCollection
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> add: newObject beforeIndex: anIndex [
	"Put newObject where anIndex is now, moving that element and everything
	 after it up one.  Answers newObject.

	 The index is MINE, one-based over my elements, not an index into the
	 Array behind me -- the same distinction removeIndex: above had to make,
	 and the same one that made 1983's version of that method remove the
	 wrong element.  An index of size + 1 appends, which is what makes
	 `add: x beforeIndex: coll size + 1' work without a special case at the
	 caller."
	(anIndex between: 1 and: self size + 1)
		ifFalse: [^self errorSubscriptBounds: anIndex].
	anIndex = (self size + 1)
		ifTrue: [^self addLast: newObject].
	self add: newObject beforeIndexBasic: anIndex.
	^newObject
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> add: newObject beforeIndexBasic: anIndex [
	"The insertion itself: grow by one at the end, then shift the tail up.

	 Written on addLast: and at:put: rather than on firstIndex and lastIndex
	 so that it stays correct however the collection grows -- addLast: is
	 the one method that knows what to do when there is no room left."
	| index |
	self addLast: self last.
	index := self size - 1.
	[index > anIndex] whileTrue: [
		self at: index put: (self at: index - 1).
		index := index - 1].
	self at: anIndex put: newObject
]

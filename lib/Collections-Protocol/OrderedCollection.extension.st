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

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
	"The insertion itself, at a one-based index over my ELEMENTS despite
	 what the selector says.

	 This grew the collection by one at the end and shifted the tail up,
	 which is right for an insert near the end and O(my size) for one near
	 the front: `add: x beforeIndex: 1' in a loop was quadratic while
	 addFirst: in the same loop is linear (Bugs4 COLL-2 names the same
	 asymmetry, in the other direction, in insert:before:).  insert:
	 before: now shifts whichever end is nearer, so both ends of this
	 method are fast and there is one piece of shifting code rather than
	 two."
	^self insert: newObject before: firstIndex + anIndex - 1
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> insert: anObject before: spot [
	"Put anObject where the BASIC index spot is now, moving what is there
	 and everything on one side of it out of the way.  Answers anObject.

	 1983 always moves the FRONT: it makes room at the low end -- sliding
	 the whole collection to the top of its Array when there is none --
	 and then shifts every element from my first up to the slot down by
	 one.  For an insert at the very front that is two writes; for an
	 insert at the END it is a copy of the entire collection, every time.

	 SortedCollection>>add: is what makes that matter.  It binary-searches
	 for the slot in O(log n) and then comes here, so feeding a
	 SortedCollection data that is already ascending -- the most natural
	 thing anybody does to one -- put every element at the end and copied
	 the collection to get it there: ten thousand ascending adds took 26.7
	 seconds where the same ten thousand descending took 0.18 (Bugs4
	 COLL-2).  The best case was the worst case.  Delay>>wait inserts into
	 SuspendedDelays this way, and delays mostly arrive in order, so a
	 server with thousands of timed waits paid it too (CHRON-5).

	 Three cases now.  A slot past my last element is an append; a slot at
	 or before my first goes on the front.  Anything in between shifts
	 whichever side is nearer, so no insertion ever moves more than half
	 the collection.  The two shifting arms are 1983's own front-shift and
	 its mirror; each takes the distance to the slot from firstIndex
	 BEFORE making room, because makeRoomAtFirst and makeRoomAtLast both
	 slide the elements inside the Array and grow may replace the object
	 entirely with become:.

	 The two ends are 1983's addFirst: and addLast: written out rather than
	 sent, because SortedCollection is an OrderedCollection and refuses
	 both by name -- where an element goes is its sortBlock's decision --
	 and it is SortedCollection>>add: that arrives here most.  Its own add:
	 reaches OrderedCollection's addLast: with a super send for the same
	 reason."
	| delta spotIndex index |
	spot > lastIndex ifTrue: [
		lastIndex = self basicSize ifTrue: [self makeRoomAtLast].
		lastIndex := lastIndex + 1.
		self basicAt: lastIndex put: anObject.
		^anObject].
	spot <= firstIndex ifTrue: [
		firstIndex = 1 ifTrue: [self makeRoomAtFirst].
		firstIndex := firstIndex - 1.
		self basicAt: firstIndex put: anObject.
		^anObject].
	delta := spot - firstIndex.
	delta <= (lastIndex - spot)
		ifTrue: [
			firstIndex = 1 ifTrue: [self makeRoomAtFirst].
			spotIndex := firstIndex + delta.
			index := firstIndex := firstIndex - 1.
			[index < (spotIndex - 1)] whileTrue: [
				self basicAt: index put: (self basicAt: index + 1).
				index := index + 1].
			self basicAt: index put: anObject]
		ifFalse: [
			lastIndex = self basicSize ifTrue: [self makeRoomAtLast].
			spotIndex := firstIndex + delta.
			index := lastIndex := lastIndex + 1.
			[index > spotIndex] whileTrue: [
				self basicAt: index put: (self basicAt: index - 1).
				index := index - 1].
			self basicAt: spotIndex put: anObject].
	^anObject
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> add: newObject afterIndex: anIndex [
	"Put newObject just after the element at anIndex, and answer it.

	 The Blue Book names add:after: and add:before:, which find the place
	 by SEARCHING for an element; both fail when the collection holds
	 duplicates or when the caller is walking by index in the first place.
	 Pharo adds these two by index; add:beforeIndex: is already here and
	 this is its other half, with an index of 0 meaning `at the front'."
	(anIndex between: 0 and: self size)
		ifFalse: [^self errorSubscriptBounds: anIndex].
	^self add: newObject beforeIndex: anIndex + 1
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> removeFirst: count [
	"Remove my first count elements and answer them, in order.

	 Answers what was removed rather than the receiver, which is what makes
	 it useful as the front half of a queue: `batch := queue removeFirst:
	 10'.  Removing more than there are is an error rather than a short
	 answer -- a caller that asked for ten and silently got three has a
	 wrong answer, not a small one."
	| removed |
	(count between: 0 and: self size)
		ifFalse: [^self error: 'cannot remove ', count printString,
			' of ', self size printString, ' elements'].
	removed := OrderedCollection new: count.
	count timesRepeat: [removed addLast: self removeFirst].
	^removed
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> removeLast: count [
	"Remove my last count elements and answer them, IN MY ORDER.

	 In my order, not in the order they came off: `#(1 2 3 4) asOrderedCollection
	 removeLast: 2' answers (3 4), which is what the elements looked like
	 in me.  Pharo answers the same way, and answering (4 3) would make
	 `coll removeLast: n' and `coll last: n' disagree about a sequence
	 neither of them reordered."
	| removed |
	(count between: 0 and: self size)
		ifFalse: [^self error: 'cannot remove ', count printString,
			' of ', self size printString, ' elements'].
	removed := OrderedCollection new: count.
	count timesRepeat: [removed addFirst: self removeLast].
	^removed
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> addAllLast: aCollection [
	"1983's, guarded against being given myself.

	 This is where OrderedCollection>>addAll: arrives, and `aCollection do:
	 [:each | self addLast: each]' with the receiver as the argument walks
	 an index towards a lastIndex that every addLast: moves further away:
	 1 2 3 became 1 2 3 1 2 3 1, and where it stopped depended on when the
	 collection happened to grow (Bugs3 B49).  Collection>>addAll: carries
	 the same guard for the collections that do not override it."
	aCollection == self ifTrue: [self addAllLast: self copy. ^aCollection].
	aCollection do: [:each | self addLast: each].
	^aCollection
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> addAllFirst: aCollection [
	"1983's, guarded against being given myself, for the reason given at
	 addAllLast: read from the other end: reverseDo: walks an index down
	 towards a firstIndex that every addFirst: moves."
	aCollection == self ifTrue: [self addAllFirst: self copy. ^aCollection].
	aCollection reverseDo: [:each | self addFirst: each].
	^aCollection
]

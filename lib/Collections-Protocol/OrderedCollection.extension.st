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
	 This is 1983's removeIndex:, renamed for what its argument is.

	 1983 closed it by moving every element after the gap down one, a
	 basicAt:put: at a time: removing from the middle of ten thousand
	 elements was five thousand stores in the interpreter, and five
	 thousand such removals took eight seconds (Bugs07 KERNA-2).  The elements now move
	 in one primitive, and from whichever side of the gap is shorter, as
	 insert:before: has always chosen its side -- so taking out an element
	 near the front costs what removeFirst does.  A tie moves the tail, as
	 1983 did.

	 Which side moved is not something a caller can see through my own
	 indices, which are counted from firstIndex; removeAllSuchThat: walks
	 the underlying Array and so had to stop assuming it was the tail."
	(removedIndex - firstIndex) < (lastIndex - removedIndex)
		ifTrue: [
			self basicReplaceFrom: firstIndex + 1 to: removedIndex with: self startingAt: firstIndex.
			self basicAt: firstIndex put: nil.
			firstIndex := firstIndex + 1]
		ifFalse: [
			self basicReplaceFrom: removedIndex to: lastIndex - 1 with: self startingAt: removedIndex + 1.
			self basicAt: lastIndex put: nil.
			lastIndex := lastIndex - 1]
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
	 removeBasicIndex: where it sent removeIndex:.

	 1983 stayed where it was after a removal, because its removeIndex:
	 always moved the tail down into the gap and the next element to look
	 at arrived there.  removeBasicIndex: moves whichever side is shorter
	 now (Bugs07 KERNA-2), so this counts the elements still to be looked
	 at -- they are the last ones, whichever side moved -- and finds the
	 next from lastIndex."
	| index element newCollection after |
	newCollection := self species new.
	index := firstIndex.
	[index <= lastIndex]
		whileTrue:
			[element := self basicAt: index.
			(aBlock value: element)
				ifTrue:
					[newCollection add: element.
					after := lastIndex - index.
					self removeBasicIndex: index.
					index := lastIndex - after + 1]
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
	 BEFORE making room, because making room slides the elements inside
	 the Array and grow may replace the object entirely with become:.

	 The two ends are 1983's addFirst: and addLast: written out rather than
	 sent, because SortedCollection is an OrderedCollection and refuses
	 both by name -- where an element goes is its sortBlock's decision --
	 and it is SortedCollection>>add: that arrives here most.  Its own add:
	 reaches OrderedCollection's addLast: with a super send for the same
	 reason.

	 That made the two ends cheap and left the middle as it was, which is
	 where a SortedCollection given data in no particular order -- what one
	 is FOR -- puts nearly everything.  The shifting arms moved a quarter of
	 the collection on average, a basicAt:put: at a time in the
	 interpreter, and ten thousand random adds took 18 seconds where
	 asSortedCollection of the same numbers took 0.2 (Bugs07 KERNA-2).  The
	 move is one primitive now, the C loop behind replaceFrom:to:with:
	 startingAt:.

	 And a middle insertion that finds no room on its side no longer sends
	 makeRoomAtFirst or makeRoomAtLast.  Those slide the whole collection
	 to the far end of the Array, which suits addFirst: and addLast: --
	 they keep coming back to the same end -- and is the worst thing to do
	 here: the next insertion on the other side finds no room there and
	 slides it all back, and on random data about every other add moved
	 the entire collection that way.  makeRoomAtBothEnds leaves half the
	 free slots on each side instead."
	| delta spotIndex |
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
			firstIndex = 1 ifTrue: [self makeRoomAtBothEnds].
			spotIndex := firstIndex + delta.
			self basicReplaceFrom: firstIndex - 1 to: spotIndex - 2 with: self startingAt: firstIndex.
			firstIndex := firstIndex - 1.
			self basicAt: spotIndex - 1 put: anObject]
		ifFalse: [
			lastIndex = self basicSize ifTrue: [self makeRoomAtBothEnds].
			spotIndex := firstIndex + delta.
			self basicReplaceFrom: spotIndex + 1 to: lastIndex + 1 with: self startingAt: spotIndex.
			lastIndex := lastIndex + 1.
			self basicAt: spotIndex put: anObject].
	^anObject
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> makeRoomAtFirst [
	"Free a slot before my first element: slide every element to the top
	 of my Array, so that all the free slots are at the front, growing
	 first if there are none.

	 1983's, with its rule unchanged -- where the elements end up is what
	 makes a run of addFirst: cheap, and is the same as it always was --
	 and the slide done by moveBasicFrom:to:by: in one primitive rather
	 than two basicAt:put:s an element (Bugs07 KERNA-2)."
	| delta |
	delta := self basicSize - self size.
	delta = 0 ifTrue: [
		self grow.
		delta := self basicSize - self size].
	lastIndex = self basicSize ifTrue: [^self].
	self moveBasicFrom: firstIndex to: lastIndex by: delta + 1 - firstIndex.
	firstIndex := delta + 1.
	lastIndex := self basicSize
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> makeRoomAtLast [
	"Free a slot after my last element: slide every element to the bottom
	 of my Array, growing first if there is no free slot at all.  1983's
	 rule and moveBasicFrom:to:by:'s move; see makeRoomAtFirst."
	| newLast |
	newLast := self size.
	self basicSize - self size = 0 ifTrue: [self grow].
	firstIndex = 1 ifTrue: [^self].
	self moveBasicFrom: firstIndex to: lastIndex by: 1 - firstIndex.
	firstIndex := 1.
	lastIndex := newLast
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> makeRoomAtBothEnds [
	"Leave free slots before my first element AND after my last: half of
	 what is free on each side, growing first if that would leave either
	 side none.

	 For an insertion into my middle, which may shift either side and
	 cannot know which the next one will (Bugs07 KERNA-2; see insert:
	 before:).  Each call moves my elements once and buys at least half
	 my free slots on both sides, so a run of insertions that alternates
	 sides at random pays for it at most once per half of the free space,
	 where sliding everything to one end paid for it on every change of
	 side."
	| size newFirst |
	self basicSize - self size < 2 ifTrue: [self grow].
	size := self size.
	newFirst := (self basicSize - size) // 2 + 1.
	self moveBasicFrom: firstIndex to: lastIndex by: newFirst - firstIndex.
	firstIndex := newFirst.
	lastIndex := newFirst + size - 1
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> moveBasicFrom: first to: last by: distance [
	"Move my slots first to last -- indices into my underlying Array, as
	 basicAt: takes them -- distance places, towards the end when it is
	 positive, and put nil into the slots the move leaves behind.

	 A slot outside firstIndex..lastIndex holds nil, which is what keeps
	 an element I no longer hold from being kept alive by me; 1983's
	 makeRoomAtFirst and makeRoomAtLast nilled each source slot as they
	 went, and this does it after the move for the ones nothing was moved
	 into."
	| vacatedFirst vacatedLast |
	(last < first or: [distance = 0]) ifTrue: [^self].
	self basicReplaceFrom: first + distance to: last + distance with: self startingAt: first.
	distance > 0
		ifTrue: [
			vacatedFirst := first.
			vacatedLast := first + distance - 1 min: last]
		ifFalse: [
			vacatedFirst := last + distance + 1 max: first.
			vacatedLast := last].
	vacatedFirst = vacatedLast
		ifTrue: [self basicAt: vacatedFirst put: nil]
		ifFalse: [
			self
				basicReplaceFrom: vacatedFirst
				to: vacatedLast
				with: (Array new: vacatedLast - vacatedFirst + 1)
				startingAt: 1]
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> basicReplaceFrom: start to: stop with: replacement startingAt: repStart [
	"Put replacement's slots repStart onwards into my slots start to stop,
	 and answer me.  Every index here is a BASIC one -- into my underlying
	 Array, as basicAt: takes it -- and into replacement's the same way;
	 replacement may be me, and an overlapping move comes out as if the
	 slots had been copied somewhere else first.

	 This is how my elements move in C rather than a basicAt:put: at a
	 time (Bugs07 KERNA-2).  Primitive 105 is String's and ByteArray's
	 replaceFrom:to:with:startingAt:, and it counts a pointer object's
	 indices from the first field after its named instance variables, as
	 at: does, so my firstIndex and lastIndex -- and a SortedCollection's
	 sortBlock -- are not among the slots it can reach.  Pharo moves an
	 OrderedCollection's elements with the same primitive, on the Array
	 it keeps them in.

	 What follows runs only when the primitive refuses: an index outside
	 either object, or a replacement that holds bytes rather than
	 pointers.  It does the same thing a slot at a time, so a bad index is
	 refused by basicAt:, in basicAt:'s words; copying downwards when the
	 replacement is me and the slots move up is the primitive's rule too."
	<primitive: 105>
	(replacement == self and: [repStart < start])
		ifTrue: [
			stop to: start by: -1 do: [:index |
				self basicAt: index put: (replacement basicAt: index - start + repStart)]]
		ifFalse: [
			start to: stop do: [:index |
				self basicAt: index put: (replacement basicAt: index - start + repStart)]]
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

{ #category : '*Collections-Protocol' }
OrderedCollection >> at: anInteger [
	"Answer with my element at index anInteger.  at: is used by a
	 knowledgeable client to access an existing element.

	 1983's, asking first that the index be an Integer: `at: 'x'' died in
	 the arithmetic with `+ sent to a String' (Bugs6 KERNA-14), and a
	 Float reached the primitive's own refusal.  Both are refused here, in
	 the primitive's words."
	anInteger isInteger ifFalse: [^self errorNonIntegerIndex].
	(anInteger < 1 or: [anInteger + firstIndex - 1 > lastIndex])
		ifTrue: [^self errorNoSuchElement].
	^super at: anInteger + firstIndex - 1
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> at: anInteger put: anObject [
	"Put anObject at element index anInteger.  at:put: can not be used to
	 append, front or back, to an ordered collection; it is used by a
	 knowledgeable client to replace an element.

	 1983 truncated the index itself before the primitive could refuse
	 it, so `at: 1.5 put: 9' stored where `at: 1.5' then refused to read
	 -- the half of Bugs1 B17 the method's own arithmetic had undone
	 (Bugs6 KERNA-14)."
	anInteger isInteger ifFalse: [^self errorNonIntegerIndex].
	(anInteger < 1 or: [anInteger + firstIndex - 1 > lastIndex])
		ifTrue: [^self errorNoSuchElement].
	^super at: anInteger + firstIndex - 1 put: anObject
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> do: aBlock [
	"Over my elements, first to last, with the last fixed when the walk
	 begins.  1983 compared its index against lastIndex at every step, so
	 a block that added to the receiver moved the goal one further each
	 time and the walk never ended: `s do: [:x | s add: x + 10]' on a
	 SortedCollection of three did not finish (Bugs6 KERNA-20).  Pharo's
	 walk is bounded like this; what a block that changes the collection
	 it is walking sees is unspecified either way, and the manual says so."
	firstIndex to: lastIndex do: [:index | aBlock value: (self basicAt: index)]
]

{ #category : '*Collections-Protocol' }
OrderedCollection >> reverseDo: aBlock [
	"Over my elements, last to first, the bounds fixed as do: fixes them."
	lastIndex to: firstIndex by: -1 do: [:index | aBlock value: (self basicAt: index)]
]

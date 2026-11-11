Extension { #name : 'Interval' }

{ #category : '*Collections-Protocol' }
Interval >> = anObject [
	"Whether anObject is a collection with my elements, in my order.

	 1983 guards with `self species == anInterval species' and then sends
	 the argument first, increment and size.  Interval>>species is ARRAY, so
	 an Array passes the guard and is then asked for its increment:
	 `(1 to: 3) = #(1 2 3)' raised MessageNotUnderstood.  Equality must
	 never raise -- it is what Set, Dictionary and every includes: is built
	 on, and a comparison that can raise is a comparison nobody can use
	 without a handler.

	 It was also not symmetric, and still would not be under the obvious
	 repair.  `#(1 2 3) = (1 to: 3)' is TRUE, because
	 SequenceableCollection>>= compares species and elements and an
	 Interval's species is Array.  Answering false here for an Array would
	 have replaced an error with a disagreement: true one way, false the
	 other.  So a non-Interval falls through to the inherited element-wise
	 comparison, which is the one that was already deciding the other
	 direction, and the two now agree.

	 The Interval case stays as its own arm because it is O(1): two
	 Intervals are equal when their first, increment and size are, without
	 looking at an element."
	(anObject isKindOf: Interval) ifFalse: [^super = anObject].
	^start = anObject first
		and: [step = anObject increment and: [self size = anObject size]]
]

{ #category : '*Collections-Protocol' }
Interval >> hash [
	"Array's hash, exactly, because an Array can be equal to me.

	 1983 hashes an Interval from its start, stop and size, which is a
	 perfectly good hash of an Interval and the wrong one here: `#(1 2 3) =
	 (1 to: 3)' is true, so the two have to hash the same or a Set holding
	 one cannot find the other.  They did not -- 4 against 15 -- and
	 `(Set with: #(1 2 3)) includes: (1 to: 3)' answered false for a
	 collection it was equal to.

	 It used to say so by copying Array's body, first and last, which was
	 the right reasoning about a function that read two elements out of
	 however many there were.  Now Array>>hash is
	 SequenceableCollection>>hash and so is this: the two agree because
	 they are the same method, which is a stronger guarantee than agreeing
	 because somebody kept two copies in step.

	 That method walked every element when it was first shared, so
	 `(1 to: 1000000) hash' walked a million of them and a Set of twenty
	 thousand Intervals did not finish (Bugs3 B33).  It now hashes every
	 element up to thirty-two and a sixteen-element sample beyond that,
	 which for an Interval is constant in its length; the trade is argued
	 there, where the function is.

	 The arm below is that same sample computed without sending at:.
	 Interval>>at: checks its bounds and computes the size for every
	 element, which made the sixteen samples cost four times what an
	 Array's cost and put a Set of twenty thousand Intervals over a
	 second; here each element is `start + (step * index)' and its hash
	 is itself, which is only true when start, stop and step are all
	 SmallIntegers -- then every element lies between start and stop and
	 is one too, and SmallInteger>>hash is the number.  Any other
	 Interval, or one short enough to be walked whole, takes the shared
	 body.  The positions and the arithmetic are SequenceableCollection>>
	 hash's exactly, and tests/unit/test_image.c holds the two together."
	| h size last |
	size := self size.
	(size <= 32
	 or: [(start isMemberOf: SmallInteger) not
	 or: [(stop isMemberOf: SmallInteger) not
	 or: [(step isMemberOf: SmallInteger) not]]])
		ifTrue: [^super hash].
	h := ((2166136261 bitXor: (size bitAnd: 16rFFFFFFFF)) * 16777619) bitAnd: 16rFFFFFFFF.
	last := size - 1.
	0 to: 15 do: [:k |
		h := ((h bitXor: ((start + (step * (k * last // 15))) bitAnd: 16rFFFFFFFF))
				* 16777619) bitAnd: 16rFFFFFFFF].
	^(h bitXor: (h bitShift: -16)) bitAnd: 16rFFFFFFF
]

{ #category : '*Collections-Protocol' }
Interval class >> from: startInteger to: stopInteger by: stepInteger [
	"1983's, refusing a step of zero.

	 An Interval walks from start towards stop by adding step until it has
	 passed stop, and a step of zero never passes anything: `(1 to: 10 by:
	 0) do:' ran for ever, in the image and not in C, so it could not even
	 be stopped from outside (Bugs3 B14).  `size' on the same Interval
	 divided by the step and raised ZeroDivide, which was the right thing
	 to say, said one message too late.  Every way of making an Interval
	 -- to:, to:by:, Number>>to:by: -- comes through here, so this is the
	 one place the question needs asking."
	stepInteger = 0
		ifTrue: [^self error: 'an Interval cannot have a step of zero'].
	^self new
		setFrom: startInteger
		to: stopInteger
		by: stepInteger
]

{ #category : '*Collections-Protocol' }
Interval >> do: aBlock [
	"Evaluate aBlock with each of my elements, first to last.

	 Each element is computed from its index -- `start + (step * (i - 1))',
	 which is what at: answers -- rather than by adding step to a running
	 value as 1983 does.  The two are the same for Integers and not for
	 Floats: repeated addition rounds at every step and the division in
	 size rounds once, so `(0 to: 0.6 by: 0.1)' had a size of 6 and a last
	 of 0.5 while do: delivered seven elements ending in 0.6, because
	 0.5 + 0.1 is 0.6 exactly and 0.6 / 0.1 is 5.999... (Bugs6 KERNA-2).
	 inject:into:, asOrderedCollection and everything else built on do:
	 disagreed with size, at: and asArray about how many elements I had,
	 and collect:, which allocates size slots and filled them from the
	 walk, raised SubscriptOutOfBounds on an ordinary Interval.  Now every
	 element comes from the one expression, so every message agrees;
	 reverseDo: and collect: below are the same arithmetic the other way
	 round and into an Array."
	1 to: self size do: [:i | aBlock value: start + (step * (i - 1))]
]

{ #category : '*Collections-Protocol' }
Interval >> reverseDo: aBlock [
	"Evaluate aBlock with each of my elements, last to first: do:'s
	 arithmetic, counted down.  See do:."
	self size to: 1 by: -1 do: [:i | aBlock value: start + (step * (i - 1))]
]

{ #category : '*Collections-Protocol' }
Interval >> collect: aBlock [
	"An Array of aBlock's answers for my elements, in my order: do:'s
	 arithmetic, so the Array is exactly size long.  See do:."
	| size result |
	size := self size.
	result := self species new: size.
	1 to: size do: [:i | result at: i put: (aBlock value: start + (step * (i - 1)))].
	^result
]

{ #category : '*Collections-Protocol' }
Interval >> last [
	"My last element: the one at: size answers, and the last do: delivers.

	 1983 computes it as `stop - (stop - start \\ step)', which is my last
	 element in exact arithmetic and a different floating-point expression
	 from do:'s `start + (step * (i - 1))' -- two subtractions and a Float
	 \\, itself `a - (a // b * b)' -- so for a Float or mixed Interval it
	 rounded elsewhere: `(0.1 to: -8 by: -10) last' was 0.09999999999999964
	 where the one element is 0.1, and the Interval did not include its own
	 last, nor find it with indexOf: (Bugs07 KERNA-4).  do:, reverseDo:,
	 collect: and at: already compute element i one way since Bugs6
	 KERNA-2; last is element size, the same way.

	 Empty, there is no element to answer, and 1983's expression is kept:
	 it is where the walk would have stopped, which is what Pharo answers
	 too."
	| size |
	size := self size.
	size = 0 ifTrue: [^stop - (stop - start \\ step)].
	^start + (step * (size - 1))
]

{ #category : '*Collections-Protocol' }
Interval >> includes: anObject [
	"Whether one of my elements is equal to anObject: indexOf:ifAbsent:,
	 which finds it by arithmetic rather than by looking at every element.
	 See there."
	^(self indexOf: anObject ifAbsent: [0]) > 0
]

{ #category : '*Collections-Protocol' }
Interval >> indexOf: anElement ifAbsent: exceptionBlock [
	"The index of my first element equal to anElement, or exceptionBlock's
	 value when none is -- the answer the inherited walk gives, found
	 without the walk.

	 Collection>>includes: and SequenceableCollection>>indexOf:ifAbsent:
	 compare anElement with every element in turn, an interpreted at: and
	 = each, so `(1 to: 1000000) includes: 0' took half a second, a Float
	 argument against ten million Integers a minute and a half, and a range
	 guard written the natural way, `(1 to: limit) includes: n', never
	 answered for an n outside `(1 to: SmallInteger maxVal)' (Bugs07
	 KERNA-6).  An Interval knows where its elements are: the one nearest
	 anElement is at `(anElement - start) / step', rounded, plus one.

	 Element i is `start + (step * (i - 1))', as at: and do: compute it,
	 and the answer is confirmed by computing it, so it is the walk's
	 exactly, Float steps included.  The elements are monotonic in i --
	 each is two rounded operations of a quantity that is, and rounding
	 keeps order -- so the elements equal to anElement, if any, are one
	 run of indices next to the estimate.  Exact arithmetic puts the run
	 at the estimate itself; Float rounding can put it a step or two away,
	 or make it longer than one element when the step is small beside the
	 elements' magnitude (`2.0 raisedTo: 53' plus a quarter at a time).
	 So the search starts at the estimate and walks towards anElement only
	 until it finds it or passes it, and a match is followed back to the
	 first of its run, which is the one the walk would have met first.  For
	 any Interval worth an estimate that is one or two comparisons.

	 Every element is a Number, and no Number equals anything else (a Point
	 included), so anything that is not a Number is absent without
	 arithmetic it might not answer: `(1 to: 3) includes: nil' is false,
	 as it was.  isKindOf: rather than isNumber, because DbColumnInfo
	 answers isNumber about the column it describes.  An infinite or NaN
	 estimate means anElement is one of those, which no element of an
	 Interval that has a size can be."
	| size k e before |
	(anElement isKindOf: Number) ifFalse: [^exceptionBlock value].
	size := self size.
	size = 0 ifTrue: [^exceptionBlock value].
	k := anElement - start / step.
	(k isFloat and: [k isFinite not]) ifTrue: [^exceptionBlock value].
	k := (k rounded + 1 max: 1) min: size.
	e := start + (step * (k - 1)).
	e = anElement ifTrue: [
		[k > 1 and: [start + (step * (k - 2)) = anElement]]
			whileTrue: [k := k - 1].
		^k].
	"Whether an element comes before anElement in my order."
	before := step > 0
		ifTrue: [[:x | x < anElement]]
		ifFalse: [[:x | x > anElement]].
	(before value: e)
		ifTrue: [
			[k < size] whileTrue: [
				k := k + 1.
				e := start + (step * (k - 1)).
				e = anElement ifTrue: [^k].
				(before value: e) ifFalse: [^exceptionBlock value]]]
		ifFalse: [
			[k > 1] whileTrue: [
				k := k - 1.
				e := start + (step * (k - 1)).
				e = anElement ifTrue: [
					[k > 1 and: [start + (step * (k - 2)) = anElement]]
						whileTrue: [k := k - 1].
					^k].
				(before value: e) ifTrue: [^exceptionBlock value]]].
	^exceptionBlock value
]

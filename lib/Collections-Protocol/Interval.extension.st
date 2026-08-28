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

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

	 Same formula as Array>>hash, first and last, so the equality above and
	 this agree by construction rather than by coincidence."
	self size = 0 ifTrue: [^17171].
	^self first hash + self last hash
]

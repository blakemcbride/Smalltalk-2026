"
RunArray class>>new:, which every other ArrayedCollection has.

A RunArray stores two ordinary Arrays -- run lengths and the value each run
repeats -- so it has named instance variables and no indexed ones, and
`Behavior>>new:' hands an unindexable class straight to a primitive that
fails.  `RunArray new: 0' therefore answered `Error: a primitive has
failed' (Bugs4, section 11), which says nothing about what went wrong and
is the same message a caller gets for a dozen unrelated faults.  It is
reachable from ordinary collection code: `species new: size' is how
ArrayedCollection builds its answers.

The class already knows how to make one of a given size -- `new: size
withAll: value' is 1983's, and it is one run of that length -- so `new:' is
that with nil, which is exactly what `Array new: 5' means by five slots.
`new: 0' answers an empty RunArray, the same object `RunArray new' answers.

Filling with nil rather than raising is the honest reading of the request,
but it does not make a RunArray writable: the class has no `at:put:', in
1983 or here, and the sender that wanted to fill one in gets told so at the
store instead of at the allocation.  That is a better place to be told,
because the message names the operation the class actually lacks.
"
Extension { #name : 'RunArray' }

{ #category : '*Collections-Protocol' }
RunArray class >> new: size [
	"Answer a RunArray of size nils -- one run, exactly as `new: size
	 withAll: nil'.  See the comment above."

	^self new: size withAll: nil
]

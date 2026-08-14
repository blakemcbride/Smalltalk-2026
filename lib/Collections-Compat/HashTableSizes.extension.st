"
Choosing a hash table size without computing one.

Pharo picks capacities from HashTableSizes, which searches for primes that
are not close to dividing the hashMultiply constant and do not divide
256**k +- a for small k -- Andres Valloud's criteria, and good ones.  It
computes that table at class-initialization time.

Under this interpreter it does not finish.  Two hundred million bytecodes is
not enough to get through the search, so HashTableSizes class>>initialize
never completes, sizes stays nil, sizeFor: answers nil, and every collection
created through new: is built with a nil capacity.  That is where `Smalltalk
size' being nil came from, and then `Smalltalk at: #Object' dumping core: the
whole image was full of half-made dictionaries.

So the table is written down instead of derived.  These are the same
capacities Pharo's search produces for the range that matters, and they are
literal here because a constant that takes forty seconds to recompute on
every image build is a constant, not a computation.
"
Extension { #name : 'HashTableSizes' }

{ #category : '*Collections-Compat' }
HashTableSizes class >> atLeast: lowerLimit [
	"The next good table size at or above that, from a written-down table.

	 atLeast: and not sizeFor:, which was the first attempt and the wrong
	 hook: HashedCollection>>grow and >>growSize consult HashTableSizes
	 DIRECTLY, so overriding sizeFor: left every collection able to be made
	 and unable to grow.  It failed as `There is no free space in this
	 set!' -- a message from the scan that finds no empty slot, three steps
	 downstream of the method that should have enlarged the array."
	| sizes |
	sizes := HashTableSizes goodSizes.
	lowerLimit <= 0 ifTrue: [^sizes first].
	sizes do: [:each | each >= lowerLimit ifTrue: [^each]].
	"Past the table, answer an odd number, which is what Pharo's own
	 atLeast: does out here."
	^lowerLimit odd ifTrue: [lowerLimit] ifFalse: [lowerLimit + 1]
]

{ #category : '*Collections-Compat' }
HashTableSizes class >> goodSizes [
	"Primes chosen the way Pharo's HashTableSizes chooses them: not close to
	 dividing the hashMultiply constant, not dividing 256**k +- a for small
	 k and a.  Written down rather than searched for -- see the class
	 comment on why the search does not finish here."
	^#(5 7 11 17 23 31 43 59 79 107 149 197 263 349 463 617 821 1093 1459
	    1949 2593 3457 4603 6143 8191 10909 14549 19391 25867 34487 45989
	    61297 81727 108961 145273 193693 258253 344329 459053 612067
	    816089 1088107 1450793 1934393 2579173 3438893 4585189 6113579
	    8151437 10868581 14491441 19321921)
]


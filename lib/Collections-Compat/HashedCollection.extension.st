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
Extension { #name : 'HashedCollection' }

{ #category : '*Collections-Compat' }
HashedCollection class >> sizeFor: nElements [
	"A capacity comfortably larger than nElements, from a fixed table.

	 The +1*4//3 is Pharo's own slop factor -- a hash table wants to stay
	 below about three quarters full -- and is kept so that a collection
	 grows at the same point it would there."
	| wanted |
	wanted := nElements + 1 * 4 // 3.
	HashedCollection goodSizes do: [:each |
		each >= wanted ifTrue: [^each]].
	"Past the end of the table, keep doubling.  Distribution matters less
	 out here than not failing does."
	^HashedCollection goodSizes last * 2
]

{ #category : '*Collections-Compat' }
HashedCollection class >> goodSizes [
	"Primes chosen the way Pharo's HashTableSizes chooses them."
	^#(5 7 11 17 23 31 43 59 79 107 149 197 263 349 463 617 821 1093 1459
	    1949 2593 3457 4603 6143 8191 10909 14549 19391 25867 34487 45989
	    61297 81727 108961 145273 193693 258253 344329 459053 612067
	    816089 1088107 1450793 1934393 2579173 3438893 4585189 6113579
	    8151437 10868581 14491441 19321921)
]

"
Array>>hash, over every element.

1983's reads two of them:

	hash
		self size = 0 ifTrue: [^17171].
		^(self at: 1) hash + (self at: self size) hash

so everything between the ends is invisible.  #(1 2 3), #(1 99 3) and
#(1 -5 3) all hash to 4, and a hundred arrays of the shape (0, i, 0) share
one hash value between them.  A Dictionary keyed by Arrays is then a linear
scan and filling one is quadratic: 1,000 keys took 1.6 seconds, 2,000 took
7.9 and 4,000 took 26.0, against 1,000 String keys at 20 milliseconds.  A
row from a database, a coordinate, a key path, a JSON array -- anything of
the shape (constant, varying, constant) -- collapsed into one bucket.

This is the identical fault commit 45f95b0 fixed for String -- `two hundred
names hashed to eleven values' -- never applied to the class next door.  The
body is now inherited from SequenceableCollection, which is where it belongs:
Array is not special, it was merely the one sequenceable class that had a
hash at all.

Interval>>hash follows the same body, and must: `#(1 2 3) = (1 to: 3)' is
true, so the two have to agree.  1983's Interval>>hash deliberately copied
1983's Array>>hash for that reason, which was correct reasoning about the
wrong function, and the two are still one function now.

That one function walks every element up to thirty-two of them and samples
sixteen beyond that (Bugs3 B33), so that an Interval of a million elements
-- which is equal to the Array of them and has to hash as one -- costs the
same as an Array of sixteen.  SequenceableCollection>>hash says why that is
the right trade and what it gives up.
"
Extension { #name : 'Array' }

{ #category : '*Collections-Protocol' }
Array >> hash [
	"SequenceableCollection's, over every element.  See the comment above."
	^super hash
]

{ #category : '*Collections-Protocol' }
Array >> hashMappedBy: map [
	"What my hash would be if oops changed according to map: unchanged.

	 1983's answers `(self first hashMappedBy: map) + (self last hashMappedBy:
	 map)', because its hash was built from two elements whose hashes could
	 be identity hashes.  Mine is built from my elements' hashes and is not
	 the receiver's oop, so a remap cannot move it -- which is exactly what
	 String>>hashMappedBy: says about String>>hash, in the same words."
	^self hash
]

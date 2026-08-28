"
A Set that can hold nil.

1983's Set>>add: opens with

	newObject == nil ifTrue: [^newObject]

because a nil slot is how the table says `empty', and it ANSWERS its
argument -- so a caller checking the return value of `aSet add: nil' sees a
successful add and gets a Set that grew by nothing.  It is data loss with a
success code:

	| s | s := Set new. s add: nil. s size            0
	(Array with: 1 with: nil with: 2) asSet size      2, want 3

and it reached Dictionary, which stores a nil key perfectly well and then
could not enumerate it, because `keys' builds a Set.

lib/Collections-Protocol/CollectionElement was already in the tree as the
answer to exactly this, with Object>>asCollectionElement and
Object>>enclosedElement as its other half in lib/Kernel-Protocol, and the
image's own Set used none of it.  Its class comment said so: `a system with
the wrapper and no unwrapping stores elements it can never read back.'

Four methods wrap on the way in and unwrap on the way out.  The PROBE loop
-- findElementOrNil:, which is the hottest thing in a hashed collection --
was deliberately untouched by this: CollectionElement answers = and hash
for what it encloses, so a wrapper standing in a slot compares equal to the
object it stands for and lands in the bucket that object hashes to.
Nothing outside this file's four methods ever sees a wrapper.  The probe
loop is overridden below all the same, for a different fault -- Bugs3 B33,
round numbers in a power-of-two table -- and still knows nothing about
wrappers.

Dictionary and IdentitySet are Set subclasses here, so both inherit these.
Dictionary is unaffected -- its slots hold Associations, which are their own
asCollectionElement, and it overrides do:, collect:, grow and rehash with
versions that speak in associations.  IdentitySet needs one method of its
own; see IdentitySet.extension.st.
"
Extension { #name : 'Set' }

{ #category : '*Collections-Protocol' }
Set >> add: newObject [
	"Add newObject, INCLUDING nil, and answer it."
	| index |
	index := self findElementOrNil: newObject.
	(self basicAt: index) == nil
		ifTrue: [self atNewIndex: index put: newObject asCollectionElement].
	^newObject
]

{ #category : '*Collections-Protocol' }
Set >> do: aBlock [
	"1983's, unwrapping each slot on the way out."
	tally = 0 ifTrue: [^self].
	1 to: self basicSize do: [:index | | element |
		element := self basicAt: index.
		element == nil ifFalse: [aBlock value: element enclosedElement]]
]

{ #category : '*Collections-Protocol' }
Set >> collect: aBlock [
	"1983's, unwrapping each slot before the block sees it.  It reads the
	 slots directly rather than going through do:, which is why overriding
	 do: alone is not enough."
	| newSet size index element |
	tally = 0 ifTrue: [^Set new: 2].
	newSet := Set new: (size := self basicSize).
	index := 0.
	[(index := index + 1) <= size] whileTrue: [
		(element := self basicAt: index) == nil
			ifFalse: [newSet add: (aBlock value: element enclosedElement)]].
	^newSet
]

{ #category : '*Collections-Protocol' }
Set >> noCheckAdd: anObject [
	"Store without probing for a duplicate -- what grow and rehash use.

	 They take their elements from do:, which has just unwrapped them, so
	 this has to wrap again or a nil element would vanish on the first
	 growth of the Set that holds it."
	self basicAt: (self findElementOrNil: anObject)
		put: anObject asCollectionElement.
	tally := tally + 1
]

{ #category : '*Collections-Protocol' }
Set >> findElementOrNil: anObject [
	"The slot anObject is in, or the empty slot it would go in.

	 1983's loop with one change: the hash is MIXED before it picks a
	 bucket.  A Set starts at two slots and doubles, so its capacity is
	 always a power of two, and 1983 takes `anObject hash \\ length + 1'
	 with SmallInteger>>hash being the number itself -- so every element
	 that is a multiple of 2^k lands in one slot until the table is bigger
	 than 2^k.  Twenty thousand multiples of 65536 took forty-seven seconds
	 to put in a Set, and twenty thousand consecutive integers took a tenth
	 of a second (Bugs3 B33).  Round numbers are the common case, not the
	 exception: sizes in bytes, addresses, timestamps rounded to a second,
	 flags, anything shifted into a field.  Pharo avoids it by choosing
	 prime capacities, which is why the Pharo Set that
	 profiles/pharo-collections loads does not need this.

	 The mixing is Fibonacci hashing.  The hash's high bits are folded
	 into its low thirty -- a SmallInteger may be sixty-two bits wide and
	 `i bitShift: 40' has its information nowhere else -- the thirty bits
	 are multiplied by 2^32 / phi, whose product depends on every bit of
	 the input, and the low sixteen bits of the product are discarded, so
	 that a run of zeros at the bottom of the hash cannot come through as
	 a run of zeros in the bucket.  Everything is SmallInteger arithmetic:
	 thirty bits times thirty-two stays below 2^62, and each step is a
	 special-selector bytecode with a SmallInteger fast path, so the whole
	 of it costs less than the one `probe = anObject' send it precedes.
	 This is the hottest loop in the image, which is why the mixing is
	 written out here rather than sent as a method of its own.

	 Dictionary>>findKeyOrNil: and IdentitySet>>findElementOrNil: do the
	 same arithmetic, and must: a collection is only consistent with
	 itself if every probe into it chooses buckets the same way.
	 IdentityDictionary probes by `asOop', as the interpreter's own method
	 lookup does in C, and is left exactly as it was.

	 A Set with no indexed part is not one of 1983's: profiles/
	 pharo-collections supersedes Set with Pharo's, which keeps its
	 elements in an `array' instance variable and inherits a
	 findElementOrNil: of its own from HashedCollection.  This extension
	 still lands on that class, so a receiver with a basicSize of zero is
	 handed back to it rather than dividing by that zero -- the same rule
	 IdentitySet>>findElementOrNil: states, for the same reason."
	| index length probe pass hash |
	length := self basicSize.
	length = 0 ifTrue: [^super findElementOrNil: anObject].
	pass := 1.
	hash := anObject hash.
	index := (((hash + (hash bitShift: -30) bitAnd: 16r3FFFFFFF) * 16r9E3779B1)
				bitShift: -16) \\ length + 1.
	[(probe := self basicAt: index) == nil or: [probe = anObject]]
		whileFalse: [
			(index := index + 1) > length
				ifTrue: [
					index := 1.
					pass := pass + 1.
					pass > 2 ifTrue: [^self grow findElementOrNil: anObject]]].
	^index
]

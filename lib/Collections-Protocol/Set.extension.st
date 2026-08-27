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
is deliberately untouched: CollectionElement answers = and hash for what it
encloses, so a wrapper standing in a slot compares equal to the object it
stands for and lands in the bucket that object hashes to.  Nothing outside
this file's four methods ever sees a wrapper.

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

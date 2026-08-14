"
An OrderedCollection that holds its elements weakly: what only it refers to is
collected, and its slot becomes nil rather than being removed.  So my size is
not the number of objects I hold, and anything walking me has to expect nils.

This replaces Pharo's class of the same name, and the reason is a difference
in the class it is built on rather than a difference of opinion.

Pharo's WeakOrderedCollection is one method -- `arrayType ^WeakArray' -- and
that is the whole of it, because Pharo's OrderedCollection keeps its elements
in a separate storage array and allocates that array through
`self class arrayType new:'.  Say WeakArray there and the collection is weak.

This system's OrderedCollection is the 1983 one, which has no storage array at
all: it is a `SequenceableCollection variableSubclass:' with firstIndex and
lastIndex, and the elements live in its OWN indexed part.  There is nothing to
give a type to.  What has to be weak is the collection itself, which is what
`#type : 'weak'' says here -- the two named instance variables stay strong,
because a weak class's NAMED fields are not weak, and only the indexed part is.

That mapping is exact rather than approximate: growing goes through
`self species new:', so every array this collection ever allocates for itself
is another weak one, and the copy that grow performs preserves the nils.

The alternative was to import Pharo's OrderedCollection, which is a Tier 2
substitution of the class the whole 1983 library and the C bootstrap stand on.
That is a ratchet turn of its own and not a way to fix a weak collection.
"
Class {
	#name : 'WeakOrderedCollection',
	#superclass : 'OrderedCollection',
	#type : 'weak',
	#category : 'Collections-Weak-Compat'
}

{ #category : 'private' }
WeakOrderedCollection class >> arrayType [
	"Answered for the sake of anything ported that asks.  It is not what
	 makes this collection weak here -- the collection itself is weak, and
	 there is no separate storage array to give a type to -- but code
	 written against Pharo's version may ask, and answering WeakArray is
	 the truthful answer to `what do your elements live in': they live in
	 me, and I am weak."
	^WeakArray
]

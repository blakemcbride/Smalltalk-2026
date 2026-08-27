"
A stand-in for an element a hashed collection cannot store as itself.

Pharo's Set keeps its elements in an Array and reads an empty slot as nil, so
nil cannot be an element: storing it would make the slot vanish and the
element with it.  Every element therefore goes in as ``asCollectionElement''
and comes back out as ``enclosedElement'', and for every object but one those
are both the object itself.  nil is the one, and it goes in wrapped in one of
these.

The wrapper is invisible to hashing and to =, because it ANSWERS the enclosed
element's = and hash: a wrapper standing in a slot compares equal to the
object it stands for and lands in the bucket that object hashes to.  That is
what lets Set wrap on the way in and unwrap on the way out without touching
its probe loop, which is the hottest code in a hashed collection.  It is not
a general box; it is the answer to `what goes in the slot where nil would
mean empty'.

Object>>asCollectionElement and Object>>enclosedElement are the other half and
live in lib/Kernel-Protocol.  The three are one mechanism: a system with the
wrapper and no unwrapping stores elements it can never read back.
"
Class {
	#name : 'CollectionElement',
	#superclass : 'Object',
	#instVars : [ 'enclosedElement' ],
	#category : 'Collections-Protocol'
}

{ #category : 'instance creation' }
CollectionElement class >> with: anObject [
	^self new setEnclosedElement: anObject
]

{ #category : 'private' }
CollectionElement >> setEnclosedElement: anObject [
	enclosedElement := anObject
]

{ #category : 'accessing' }
CollectionElement >> enclosedElement [
	"What I stand in for.  The one message a collection asks me."
	^enclosedElement
]

{ #category : 'printing' }
CollectionElement >> printOn: aStream [
	aStream nextPutAll: 'a CollectionElement('.
	enclosedElement printOn: aStream.
	aStream nextPut: $)
]

{ #category : 'comparing' }
CollectionElement >> = anObject [
	"Whether anObject is -- or stands in for -- what I stand in for.

	 Asked as `probe = sought' inside Set>>findElementOrNil:, where the
	 probe is whatever is in the slot and the sought object is what a
	 caller passed to add:, includes: or remove:.  Both sides go through
	 enclosedElement, so all four combinations agree: a wrapper matches the
	 nil a caller asked for, and it matches ITSELF, which is what
	 fixCollisionsFrom: needs when it re-probes an element it is moving."
	^enclosedElement = anObject enclosedElement
]

{ #category : 'comparing' }
CollectionElement >> hash [
	"What I stand in for hashes to, so that I land in its bucket.

	 Set>>add: hashes the raw object to find a slot and then stores me in
	 it; a later `includes:' hashes the raw object again.  If I hashed as
	 myself the two would look in different places, and the element would
	 be in the Set and unfindable."
	^enclosedElement hash
]

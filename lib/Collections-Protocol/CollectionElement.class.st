"
A stand-in for an element a hashed collection cannot store as itself.

Pharo's Set keeps its elements in an Array and reads an empty slot as nil, so
nil cannot be an element: storing it would make the slot vanish and the
element with it.  Every element therefore goes in as ``asCollectionElement''
and comes back out as ``enclosedElement'', and for every object but one those
are both the object itself.  nil is the one, and it goes in wrapped in one of
these.

The wrapper is invisible to hashing and to =, because Set unwraps at every
site that compares -- scanFor:, includes:, rehash, grow -- so this class has
exactly one message to answer.  That is why it can be this small: it is not a
general box, it is the answer to `what goes in the slot where nil would mean
empty'.

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

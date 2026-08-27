"
A queue is itself and nothing else: LinkedList compares and hashes by identity.

LinkedList is a SequenceableCollection, so it inherited
SequenceableCollection>>= -- same species, same size, element by element --
and that is the wrong contract for what a LinkedList IS here.  Semaphore is
one, and a Semaphore's `elements' are the processes waiting on it at this
instant: they arrive and leave from other threads, and two Semaphores that
nobody is waiting on are both empty.  `Semaphore new = Semaphore new'
answered TRUE, which has been so since 1983 and did no harm only because
LinkedList had Object's identity hash, so a Set could still hold both.

Giving SequenceableCollection a structural hash -- which is what a
sequenceable collection with a structural = has to have -- would have made
the two agree in the wrong direction: two fresh Semaphores would have become
one element of a Set, and a Semaphore's hash would have changed every time a
process waited on it or was woken.  A hashed collection cannot hold a key
whose hash moves under it.

So the pair is fixed here rather than half-fixed there.  A LinkedList is
equal to itself alone, and hashes as itself, which is what every user of one
in this system already assumed.
"
Extension { #name : 'LinkedList' }

{ #category : '*Collections-Protocol' }
LinkedList >> = anObject [
	"Identity.  See the comment above: my contents are a queue, not a value."
	^self == anObject
]

{ #category : '*Collections-Protocol' }
LinkedList >> hash [
	"My identity hash, which is what Object would have answered.

	 Object>>hash's body, written out, because SequenceableCollection>>hash
	 now stands between us and there is no way to reach past it by name."
	<primitive: 75>
	^self identityHash
]

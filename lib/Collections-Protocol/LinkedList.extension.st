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

The adders, at:, and copy are here too, for a different reason: 1983's
LinkedList trusted whatever it was handed.  A Link added twice pointed at
itself and size never returned; a non-Link went into the chain and the next
walk was a doesNotUnderstand; at: indexed the list object's own slots; and
copy shared one chain between two lists (Bugs3 B51).  checkLink: says what
is refused and why the last link of another list cannot be.
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

{ #category : '*Collections-Protocol' }
LinkedList >> addFirst: aLink [
	"Add aLink at my front, if it is a Link that is not already in a
	 list.  See checkLink: for what happened when it was not."
	self checkLink: aLink.
	self isEmpty ifTrue: [lastLink := aLink].
	aLink nextLink: firstLink.
	firstLink := aLink.
	^aLink
]

{ #category : '*Collections-Protocol' }
LinkedList >> addLast: aLink [
	"Add aLink at my end, if it is a Link that is not already in a list.
	 See checkLink: for what happened when it was not."
	self checkLink: aLink.
	self isEmpty
		ifTrue: [firstLink := aLink]
		ifFalse: [lastLink nextLink: aLink].
	lastLink := aLink.
	^aLink
]

{ #category : '*Collections-Protocol' }
LinkedList >> checkLink: aLink [
	"Refuse what 1983 accepted and was broken by.

	 A LinkedList is a chain of Links, each pointing at the next, and it
	 has no other record of what it holds -- size walks the chain.  So
	 adding something that is not a Link put an object into the chain
	 that could not answer nextLink, and the next size or do: was a
	 doesNotUnderstand; and adding a Link that was ALREADY the last one
	 pointed it at itself, after which size never returned at all (Bugs3
	 B51).  A Link is in a list when its nextLink is set, or when it is
	 my last -- the last link of some OTHER list has nextLink nil and
	 cannot be told from a free one, which is what a singly linked list
	 costs; a Link that remove: has taken out has nextLink nil again and
	 may come back."
	(aLink isKindOf: Link)
		ifFalse: [^self error: 'a LinkedList holds Links, not a ' , aLink class name].
	(aLink nextLink notNil or: [aLink == lastLink])
		ifTrue: [^self error: 'that Link is already in a list']
]

{ #category : '*Collections-Protocol' }
LinkedList >> at: index [
	"The index-th Link, walking the chain, since there is no other way to
	 the middle of one.  1983 left this to Object>>at:, which indexes the
	 receiver's own slots, of which a LinkedList has none past its two
	 named ones -- so at: 1 was a SubscriptOutOfBounds (Bugs3 B51)."
	| link |
	index < 1 ifTrue: [^self errorSubscriptBounds: index].
	link := firstLink.
	index - 1 timesRepeat: [link isNil ifTrue: [^self errorSubscriptBounds: index]. link := link nextLink].
	link isNil ifTrue: [^self errorSubscriptBounds: index].
	^link
]

{ #category : '*Collections-Protocol' }
LinkedList >> copy [
	"A new list of copies of my Links.

	 The Links have to be copied, not shared: a Link's nextLink is its
	 place in ONE chain, so a shallow copy of the list -- which is what
	 Object>>copy gave -- had two lists over one chain, and adding to
	 either altered both.  Each copy has its nextLink cleared before it
	 is added, which is what addLast: now requires of a Link."
	| copy |
	copy := self species new.
	self do: [:each | copy addLast: (each shallowCopy nextLink: nil; yourself)].
	^copy
]

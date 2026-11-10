"
A weak-keyed dictionary that the Finalizer never edits under its readers.

Pharo's WeakKeyDictionary is a Dictionary of WeakKeyAssociations, and each
association, when its key dies, asks the dictionary to remove it -- from
the finalization process, which runs on whatever worker is free at the
moment the collector queues it.  So the dictionary has a second writer its
owner cannot see, and two things follow from that.

Its writers have to be serialised against that one.  The owner inside
at:put:, growing the table, and the Finalizer removing an entry from it
lost each other's updates, and a dead key stayed in the array, held
strongly, for ever (Bugs6 OM-6).  Every method here that writes the table
holds LibraryLocks class>>holdingMourning:, and so does the Finalizer's
removal.  The caller's blocks run outside it.  The lock is one for every
weak dictionary in the image, so nothing is done under it that takes
longer than one walk of one table: a mourn marks its entry, and the
entries one drain of the Finalizer marked are taken out together when it
ends, by one rebuild of the table (removeMourned:, Bugs07 FIXES-2).

And its readers have to be safe from it, which no lock the owner could
take would make them (Bugs07 FIXES-4).  Pharo removes an entry by nilling
its slot and then moving the later entries of the cluster back one swap:
at a time, in the live table; an owner probing for a key further down
that cluster -- a key held strongly the whole time -- reached the freed
slot before the entry had moved into it, stopped at the nil and answered
absent, one read in five while a neighbour was mourned on another worker.
So the Finalizer does not edit the table anyone may be reading.  It
builds a new one without the dead entries and puts it in place with one
store: a reader sees the table from before the removal or the table from
after it, never one in between.  And the readers read the table ONCE.  Pharo's
at:ifAbsent: probes `array' in scanFor: and then indexes `array' again
with the answer, and a table put in place between the two would be
indexed with a slot found in the other -- an absent answer, or another
key's value.  Every reader below takes the table into a temporary and
probes and indexes that one; valuesDo: and the select:/reject: shortcuts
walk the table they took, where Pharo's re-read it at every index.  What
the owner itself writes it writes in place, as any Dictionary does: its
own reads come after its own writes, and a dictionary it shares with its
other processes is its to lock (doc/CONCURRENCY.md).

WeakIdentityKeyDictionary is a sibling of this class, not a subclass, and
has the same methods with identity in place of equality; see there.
"
Extension { #name : 'WeakKeyDictionary' }

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> at: key put: anObject [
	"Set the value at key to be anObject.  If key is not found, create a
	new entry for key and set is value to anObject. Answer anObject."

	| index assoc |
	LibraryLocks holdingMourning: [
		index := self findElementOrNil: key.
		assoc := array at: index.
		assoc isNil
			ifTrue: [self atNewIndex: index put: ((WeakKeyAssociation key: key value: anObject) container: self)]
			ifFalse: [
				"An entry mourned in the drain that is running stays in the
				 table until the drain ends, and is dropped then with
				 whatever was stored into it; a key put again in the
				 meantime -- an equal one, since the mourned key itself
				 is gone -- gets an entry of its own (Bugs07 FIXES-2)."
				assoc container == self
					ifTrue: [assoc value: anObject]
					ifFalse: [array at: index put: ((WeakKeyAssociation key: key value: anObject) container: self)]]].
	^ anObject
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> at: key update: updateBlock initial: initBlockOrValue [
	"Pharo's writes the table itself, outside the lock the other writers
	 hold, and with a plain Association, which would hold its key
	 strongly in a dictionary that promises not to.  Through at:put:,
	 with the caller's blocks run outside the lock."
	| assoc |
	assoc := self associationOf: key in: array.
	assoc isNil
		ifTrue: [self at: key put: initBlockOrValue value]
		ifFalse: [self at: key put: (updateBlock value: assoc value)]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> removeKey: key ifAbsent: aBlock [
	"Remove key (and its associated value) from the receiver. If key is not in
	the receiver, answer the result of evaluating aBlock. Otherwise, answer
	the value externally named by key.

	 The owner's removal, in place and under the lock; aBlock outside it.
	 The gap is closed by vacate:in:, one look at each entry after it,
	 and not by Pharo's fixCollisionsFrom:, which probes each of them
	 again from its home: a cluster thousands long made one removal cost
	 seconds, with the lock every weak dictionary's writers share held
	 throughout (Bugs07 FIXES-2)."
	| found |
	found := LibraryLocks holdingMourning: [ | index assoc |
		index := self findElementOrNil: key.
		assoc := array at: index.
		assoc isNil ifFalse: [
			self vacate: index in: array.
			tally := tally - 1].
		assoc].
	^found isNil
		ifTrue: [aBlock value]
		ifFalse: [found value]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> removeAll [
	"Under the lock: a table replaced beside the Finalizer's rebuild would
	 be replaced again by the rebuild, with every entry back in it."
	LibraryLocks holdingMourning: [super removeAll]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> rehash [
	"Under the lock, as removeAll."
	LibraryLocks holdingMourning: [super rehash]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> compact [
	"Under the lock, as removeAll."
	LibraryLocks holdingMourning: [super compact]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> removeMourned: anAssociation [
	"Take anAssociation out of me, for the Finalizer, whose key has died
	 -- once the drain that is mourning it has ended.  It is marked, by
	 belonging to no dictionary any more, and I am given to the Finalizer
	 to be sent finishMourning when the drain is over, which takes out of
	 my table every entry so marked at once.

	 Bugs07 FIXES-2.  Taken out one at a time, each removal walked the
	 rest of its cluster -- Pharo's fixCollisionsFrom: probes every entry
	 in it again from its home slot -- while the owner's next write waited
	 on the lock; a collection that had found thirty thousand dead keys
	 in a table near its growth threshold left clusters nine thousand
	 slots long, every removal cost seconds, and the write that would
	 have grown the table and broken them up was the one waiting.  Eight
	 workers stalled for minutes where one finished in five seconds.  Now
	 a mourn is two stores and the table is rebuilt once per drain, so the
	 owner waits at most for one rebuild.  The mark is made under the
	 lock, which is held for that one store: at:put: looks at it and then
	 stores into the association, and a mark between the two would drop
	 the owner's value with the entry."
	LibraryLocks holdingMourning: [anAssociation container: nil].
	Finalizer afterMourning: self
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> finishMourning [
	"Take out of my table every entry mourned since the last time, for
	 the Finalizer at the end of a drain: into a new table of the same
	 size, put in place with one store, so that a reader sees the old
	 table or the new one and never one being changed (Bugs07 FIXES-4).
	 The rest keep their associations, and the table its size, which
	 Pharo's never shrinks either."
	LibraryLocks holdingMourning: [ | old fresh kept |
		old := array.
		fresh := Array new: old size.
		kept := 0.
		old do: [:each |
			(each notNil and: [each container == self]) ifTrue: [
				fresh at: (self emptySlotFor: each key in: fresh) put: each.
				kept := kept + 1]].
		array := fresh.
		tally := kept]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> emptySlotFor: key in: table [
	"The first empty slot of key's probe in table, which has one."
	| size index |
	size := table size.
	index := self homeOf: key in: size.
	[(table at: index) == nil] whileFalse: [index := index \\ size + 1].
	^index
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> homeOf: key in: tableSize [
	"The slot key's probe starts at, in a table of tableSize slots: scanFor:'s."
	^key hash \\ tableSize + 1
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> associationOf: key in: table [
	"key's association in table, or nil.  scanFor:'s probe, over the one
	 table the caller read, answering what it found there rather than
	 where."
	| size start element |
	size := table size.
	start := self homeOf: key in: size.
	start to: size do: [:index |
		(element := table at: index) == nil ifTrue: [^nil].
		element key = key ifTrue: [^element]].
	1 to: start - 1 do: [:index |
		(element := table at: index) == nil ifTrue: [^nil].
		element key = key ifTrue: [^element]].
	^nil
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> vacate: index in: table [
	"Empty slot index of table and close the gap, as Knuth's Algorithm R
	 does: walk on through the cluster from the hole, and move back into
	 the hole each entry whose home slot does not lie cyclically after
	 the hole and at or before where the entry stands -- the hole moving
	 to where that entry was.  Each entry is looked at once, where
	 Pharo's fixCollisionsFrom: probes every one of them again from its
	 home."
	| size hole at element home |
	size := table size.
	hole := index.
	table at: hole put: nil.
	at := hole.
	[at := at \\ size + 1.
	 (element := table at: at) == nil] whileFalse: [
		home := self homeOf: element key in: size.
		(hole < at
			ifTrue: [home <= hole or: [home > at]]
			ifFalse: [home <= hole and: [home > at]])
			ifTrue: [
				table at: hole put: element.
				table at: at put: nil.
				hole := at]]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> at: key ifAbsent: aBlock [
	"Answer the value associated with the key or, if key isn't found,
	answer the result of evaluating aBlock.

	 Probed in the table read once; see the comment at the top."
	| assoc |
	assoc := self associationOf: key in: array.
	^assoc isNil
		ifTrue: [aBlock value]
		ifFalse: [assoc value]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> at: key ifPresent: aBlock [
	"Probed in the table read once; see the comment at the top."
	| assoc |
	assoc := self associationOf: key in: array.
	^assoc isNil
		ifTrue: [nil]
		ifFalse: [aBlock cull: assoc value]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> associationAt: key ifAbsent: aBlock [
	"Probed in the table read once; see the comment at the top."
	| assoc |
	assoc := self associationOf: key in: array.
	^assoc isNil
		ifTrue: [aBlock value]
		ifFalse: [assoc]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> associationAt: key ifPresent: aBlock [
	"Probed in the table read once; see the comment at the top."
	| assoc |
	assoc := self associationOf: key in: array.
	^assoc isNil
		ifTrue: [nil]
		ifFalse: [aBlock cull: assoc]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> includesKey: key [
	"Probed in the table read once; see the comment at the top."
	^(self associationOf: key in: array) ~~ nil
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> valuesDo: aBlock [
	"Over the table read once.  Pharo's read the size of one table and
	 indexed whichever was in place at each step."
	tally = 0 ifTrue: [^self].
	array do: [:each | each == nil ifFalse: [aBlock value: each value]]
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> select: selectBlock thenCollect: collectBlock [
	"Pharo's shortcut, over the table read once, as valuesDo:."
	| newDictionary |
	newDictionary := self copyEmpty.
	self associationsDo: [:assoc |
		(selectBlock value: assoc value) ifTrue: [
			newDictionary at: assoc key put: (collectBlock value: assoc value)]].
	^newDictionary
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> reject: rejectBlock thenCollect: collectBlock [
	"Pharo's shortcut, over the table read once, as valuesDo:."
	| newDictionary |
	newDictionary := self copyEmpty.
	self associationsDo: [:assoc |
		(rejectBlock value: assoc value) ifFalse: [
			newDictionary at: assoc key put: (collectBlock value: assoc value)]].
	^newDictionary
]

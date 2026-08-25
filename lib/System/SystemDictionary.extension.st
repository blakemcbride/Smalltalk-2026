"
Asking for a collection.

There is no garbageCollect anywhere in the 1983 library -- the image simply
cannot ask.  That is livable while nothing depends on when a collection
happens, and not livable once weak references exist: a weak slot is cleared
by a collection, so without a way to request one there is no way to observe
the mechanism working, and therefore no way to test it from inside.
"
Extension { #name : 'SystemDictionary' }

{ #category : '*System-Runtime' }
SystemDictionary >> garbageCollect [
	"Collect now, and answer how many objects were reclaimed."
	<primitive: 250>
	^0
]

{ #category : '*System' }
SystemDictionary >> at: aKey put: anObject [
	"Declare a global, holding the lock every writer to Smalltalk holds.

	 Declaring is find-the-slot-then-fill-it, and two workers declaring at
	 once both found the same empty slot: eight workers declaring 200
	 globals each lost 245 of the 1,600.  The body is lockedAt:put:, which
	 is 1983's here and Pharo's in lib/Collections-Compat, where Dictionary
	 is Pharo's and the 1983 insides mean nothing."
	^LibraryLocks holdingGlobals: [self lockedAt: aKey put: anObject]
]

{ #category : '*System' }
SystemDictionary >> lockedAt: aKey put: anObject [
	"1983's at:put:, to be run with the globals lock held: a name found in
	 Undeclared is adopted rather than duplicated, so a method compiled
	 against the forward reference sees the value."
	| index element |
	index := self findKeyOrNil: aKey.
	element := self basicAt: index.
	element == nil
		ifTrue:
			[self
				valueAtNewKey: aKey
				put: anObject
				atIndex: index
				declareFrom: Undeclared]
		ifFalse:
			[element value: anObject].
	^anObject
]

{ #category : '*System' }
SystemDictionary >> add: anAssociation [
	"Under the same lock as at:put:; the two are the only ways in."
	^LibraryLocks holdingGlobals: [super add: anAssociation]
]

{ #category : '*System' }
SystemDictionary >> removeKey: key ifAbsent: aBlock [
	"Under the same lock as at:put:: removal closes the gap it leaves by
	 moving later entries back, which a concurrent declare would land in."
	^LibraryLocks holdingGlobals: [super removeKey: key ifAbsent: aBlock]
]

{ #category : '*System' }
SystemDictionary >> classNames [
	"Answer a SortedCollection of all class names, cached.

	 1983 read CachedClassNames twice -- once to test it for nil and once
	 to answer it -- and flushClassNameCache, which every class definition
	 sends, could set it to nil between the two.  Read once."
	| names |
	names := CachedClassNames.
	names == nil ifTrue: [^self newClassNames].
	^names
]

{ #category : '*System' }
SystemDictionary >> at: key ifAbsent: aBlock [
	"Under the globals lock, like the writers -- because of how a Set grows.

	 1983's Set>>grow makes a bigger copy and sends become:, so that every
	 reference to the old collection now refers to the new one.  A reader
	 that had already read the old basicSize and computed its probe from
	 it then probes the NEW table with the old arithmetic, and misses.
	 Eight workers reading `Smalltalk at:' in a loop while others declared
	 globals answered nil for a key that was there.  Globals compiled into
	 methods are bindings and never come through here, so the lock costs
	 the tools and the tests, and buys every reader a table that is not
	 being replaced under it."
	^LibraryLocks holdingGlobals: [super at: key ifAbsent: aBlock]
]

{ #category : '*System' }
SystemDictionary >> associationAt: key ifAbsent: aBlock [
	"Under the globals lock; see at:ifAbsent:."
	^LibraryLocks holdingGlobals: [super associationAt: key ifAbsent: aBlock]
]

{ #category : '*System' }
SystemDictionary >> includesKey: key [
	"Under the globals lock; see at:ifAbsent:."
	^LibraryLocks holdingGlobals: [super includesKey: key]
]

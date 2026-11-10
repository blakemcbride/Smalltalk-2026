"
What SystemDictionary has to be told once Dictionary is Pharo's.

SystemDictionary is a 1983 class and frozen, and it is a Dictionary subclass,
so superseding Dictionary re-parents it onto an implementation whose insides
are somewhere else.  One of its methods reaches inside, and this is that
method -- under the name lib/System's locked at:put: calls it by.

It matters more than one method usually would: at:put: is how every global in
the image is declared, so getting it wrong does not fail somewhere later, it
fails at `Smalltalk at: #Object' -- which, before this, dumped core.

The others are Pharo selectors that 1983's Dictionary does not have and that
Pharo's implements by reading or writing its array directly, past the
globals lock lib/System gives every reader and writer of Smalltalk.  Each is
routed through lib/System's locked methods here, with the caller's block
run after the lock is given back (Bugs07 KERNB-2).
"
Extension { #name : 'SystemDictionary' }

{ #category : '*Collections-Compat' }
SystemDictionary >> lockedAt: aKey put: anObject [
	"Declare a global, reusing whatever binding already exists for it.
	 This is the body at:put: runs with LibraryLocks' globals lock held --
	 lib/System's at:put: takes the lock and sends this, which is why it
	 sends super add: rather than add: below, and super associationAt:
	 rather than associationAt:: both take the same lock, and the lock is
	 not re-entrant.

	 The 1983 version finds the slot with findKeyOrNil: and reads it with
	 basicAt:, neither of which means anything against Pharo's Dictionary.
	 What it is DOING, though, has to survive exactly, and it is two things:

	 An Association already in the table is updated in place rather than
	 replaced.  A compiled method holds the Association, not the dictionary
	 -- that is what makes reading a global lock-free, see CONCURRENCY.md --
	 so replacing it would leave every method already compiled pointing at a
	 binding nobody updates again.

	 A binding the compiler left in Undeclared is ADOPTED, not duplicated.
	 Compiling a forward reference puts an Association in Undeclared and
	 hands it to the method; when the global is finally declared, that same
	 Association has to become the real one, or the method that was waiting
	 for it waits for ever."
	| existing pending |
	existing := super associationAt: aKey ifAbsent: [nil].
	existing notNil ifTrue: [
		existing value: anObject.
		^anObject].
	pending := Undeclared isNil
		ifTrue: [nil]
		ifFalse: [Undeclared associationAt: aKey ifAbsent: [nil]].
	pending isNil
		ifTrue: [super at: aKey put: anObject]
		ifFalse: [
			Undeclared removeKey: aKey ifAbsent: [nil].
			pending value: anObject.
			super add: pending].
	^anObject
]

{ #category : '*Collections-Compat' }
SystemDictionary >> at: key ifPresent: aBlock [
	"Pharo's, through the locked look-up of lib/System's at:ifAbsent:.

	 Pharo's Dictionary reads its array here directly, twice, with no
	 lock: once to find the slot and once to read it, so a declaration
	 on another worker that replaced the array between the two read the
	 new array at the old one's index.  lib/System puts every look-up in
	 Smalltalk under the globals lock and runs the caller's block after
	 it (Bugs07 KERNB-2); this is the selector 1983's Dictionary does not
	 have, so it is here and not there.  at:ifPresent:ifAbsent: and
	 at:ifPresent:ifAbsentPut: come through it."
	| value |
	value := self at: key ifAbsent: [^nil].
	^aBlock cull: value
]

{ #category : '*Collections-Compat' }
SystemDictionary >> associationAt: key ifPresent: aBlock [
	"As at:ifPresent:, for the Association."
	| binding |
	binding := self associationAt: key ifAbsent: [^nil].
	^aBlock cull: binding
]

{ #category : '*Collections-Compat' }
SystemDictionary >> at: key update: updateBlock initial: initBlockOrValue [
	"Pharo's, with the write under the globals lock and neither block
	 inside it (Bugs07 KERNB-2).

	 Pharo's finds the slot and stores into it with no lock at all -- a
	 writer to Smalltalk past the lock every other writer holds -- and
	 runs both blocks while it has the slot.  Here the old value is read
	 by the locked look-up, the block runs with nothing held, and the new
	 value goes in by at:put:.  Two processes updating one global at once
	 can each start from the same old value; Smalltalk is not a counter."
	| absent old |
	absent := false.
	old := self at: key ifAbsent: [absent := true. nil].
	self
		at: key
		put: (absent
			ifTrue: [initBlockOrValue value]
			ifFalse: [updateBlock value: old])
]

{ #category : '*Collections-Compat' }
SystemDictionary >> select: selectBlock thenCollect: collectBlock [
	"Pharo's walks its array directly; this walks the copy lib/System's
	 associationsDo: takes under the globals lock (Bugs07 KERNB-2)."
	| newDictionary |
	newDictionary := self copyEmpty.
	self associationsDo: [:assoc |
		(selectBlock value: assoc value)
			ifTrue: [newDictionary at: assoc key put: (collectBlock value: assoc value)]].
	^newDictionary
]

{ #category : '*Collections-Compat' }
SystemDictionary >> reject: rejectBlock thenCollect: collectBlock [
	"As select:thenCollect:."
	| newDictionary |
	newDictionary := self copyEmpty.
	self associationsDo: [:assoc |
		(rejectBlock value: assoc value)
			ifFalse: [newDictionary at: assoc key put: (collectBlock value: assoc value)]].
	^newDictionary
]

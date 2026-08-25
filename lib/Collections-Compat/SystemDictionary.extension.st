"
What SystemDictionary has to be told once Dictionary is Pharo's.

SystemDictionary is a 1983 class and frozen, and it is a Dictionary subclass,
so superseding Dictionary re-parents it onto an implementation whose insides
are somewhere else.  Exactly one of its methods reaches inside, and this is
that method -- under the name lib/System's locked at:put: calls it by.

It matters more than one method usually would: at:put: is how every global in
the image is declared, so getting it wrong does not fail somewhere later, it
fails at `Smalltalk at: #Object' -- which, before this, dumped core.
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

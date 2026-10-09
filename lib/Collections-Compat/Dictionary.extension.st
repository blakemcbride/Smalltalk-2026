Extension { #name : 'Dictionary' }

{ #category : '*Collections-Compat' }
Dictionary >> declare: key from: aDictionary [
	"Add key to me.  If I already have it, do nothing.  If aDictionary has
	 it, MOVE it here -- taking its Association, not a copy of its value --
	 and remove it there.

	 Taking the ASSOCIATION rather than the value is the whole point and is
	 not a detail: a compiled method that already refers to the undeclared
	 name holds that Association, so moving the object is what makes the
	 method see the value once the name is declared.  Copying the value
	 into a new Association would leave every existing reference bound to
	 the old one forever.

	 1983's, and superseding Dictionary lost it.  Two live senders wanted
	 it: Class>>declare:, which is how a class gains a class variable, and
	 Metaclass>>name:inEnvironment:subclassOf:..., which is how a class is
	 defined at all.  Both move a name out of Undeclared, and both would
	 have failed the first time somebody defined a class in the Browser.
	 The same method is in lib/Collections-Protocol for the 1983 Dictionary.

	 aDictionary is Undeclared at the sender that comes here,
	 Class>>declare:, which is how a class gains a class variable (Smalltalk
	 has a declare:from: of its own), and Undeclared is written by every
	 worker that compiles: the removal here raced those stores and lost
	 entries either way (Bugs6 KERNB-6).  So the Undeclared side -- find
	 the Association, take it out -- runs under the lock Smalltalk's own
	 writers hold, and the receiver's side outside it: a class pool is the
	 class's own, and Smalltalk, whose declare:from: is its own method,
	 holds the lock for the whole of it."
	| pending |
	(self includesKey: key) ifTrue: [^self].
	pending := LibraryLocks holdingGlobals: [ | found |
		found := aDictionary associationAt: key ifAbsent: [nil].
		found isNil ifFalse: [aDictionary removeKey: key].
		found].
	pending isNil
		ifTrue: [self at: key put: nil]
		ifFalse: [self add: pending]
]

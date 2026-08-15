Extension { #name : 'Dictionary' }

{ #category : '*Collections-Compat' }
Dictionary >> declare: key from: aDictionary [
	"Add key to me.  If I already have it, do nothing.  If aDictionary has
	 it, MOVE it here -- taking its Association, not a copy of its value --
	 and remove it there.

	 1983's, and superseding Dictionary lost it.  Two live senders wanted
	 it: Class>>declare:, which is how a class gains a class variable, and
	 Metaclass>>name:inEnvironment:subclassOf:..., which is how a class is
	 defined at all.  Both move a name out of Undeclared, and both would
	 have failed the first time somebody defined a class in the Browser.

	 Taking the ASSOCIATION rather than the value is the whole point and is
	 not a detail: a compiled method that already refers to the undeclared
	 name holds that Association, so moving the object is what makes the
	 method see the value once the name is declared.  Copying the value
	 into a new Association would leave every existing reference bound to
	 the old one forever."
	(self includesKey: key) ifTrue: [^self].
	(aDictionary includesKey: key)
		ifTrue: [
			self add: (aDictionary associationAt: key).
			aDictionary removeKey: key]
		ifFalse: [self at: key put: nil]
]

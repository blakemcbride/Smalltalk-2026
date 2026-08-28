Extension { #name : 'Symbol' }

{ #category : '*Kernel-Protocol' }
Symbol >> displayString [
	^self asString
]

{ #category : '*Kernel-Protocol' }
Symbol >> isSymbol [
	^true
]

{ #category : '*Kernel-Protocol' }
Symbol class >> lookup: aString [
	"The Symbol whose characters those are, or nil when there is none.

	 asSymbol MAKES one, and a Symbol is never collected: it is put in
	 USTable and stays in the image for the life of the image.  That is
	 exactly what a symbol is for when the name comes from the program, and
	 exactly wrong when the name comes off a network.  Bugs3 B55: a REST
	 request's `_class' and `_method' were interned before anything checked
	 whether there was such a class or such a method, so eight thousand
	 requests naming eight thousand nonexistent methods added eight thousand
	 Symbols that two full garbage collections did not touch.  A caller
	 that wants to LOOK a name up asks for it this way and fails on nil;
	 only a name that turns out to be real is interned.

	 hasInterned:ifTrue: is the unlocked read the Concurrency extension
	 describes: a bucket is replaced by a copy and never edited in place, so
	 a reader sees the old bucket or the new one and never a torn one.  This
	 method therefore needs no lock and takes none.

	 A one-character string always answers, because the 128 single-character
	 Symbols are made once at build time and are not interned on demand.
	 That is not a leak -- there is nothing to create -- and a caller
	 checking a name still has to check what the name NAMES."

	self hasInterned: aString ifTrue: [:existing | ^existing].
	^nil
]

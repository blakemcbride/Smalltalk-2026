"
The protocol modern Smalltalk assumes and 1983 does not have.

Two of these are worth a note.

ifNil: and friends are REAL SENDS here.  Pharo's compiler inlines them, so
in Pharo they cost nothing and the argument must be a literal block.  This
compiler does not inline them, which makes them slower and, in exchange,
makes them ordinary: any object that answers value works, and a variable
holding a block works.  If they ever show up in a profile, inlining them is
a compiler change, not a library change.

displayString is not printString.  A String prints as 'abc', with the
quotes, because printString's job is to produce text that reads back in as
the object.  displayString's job is to produce text for a person, so a
String displays as abc.  Everything else, having nothing better to say,
says its printString.
"
Extension { #name : 'Object' }

{ #category : '*Kernel-Protocol' }
Object >> ifNil: aBlock [
	"The receiver is not nil, so the block is not run and the receiver is
	 the answer -- which is what lets ifNil: be chained onto an expression
	 without a temporary."
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> ifNotNil: aBlock [
	"Run the block on the receiver.  The block may take the receiver or
	 take nothing; cull: sorts that out, because both spellings are
	 common and neither is wrong."
	^aBlock cull: self
]

{ #category : '*Kernel-Protocol' }
Object >> ifNil: nilBlock ifNotNil: notNilBlock [
	^notNilBlock cull: self
]

{ #category : '*Kernel-Protocol' }
Object >> ifNotNil: notNilBlock ifNil: nilBlock [
	^notNilBlock cull: self
]

{ #category : '*Kernel-Protocol' }
Object >> displayString [
	"Text for a person to read, as against printString's text for the
	 compiler to read back.  They differ only where the printed form
	 carries syntax -- a String's quotes, a Symbol's hash."
	^self printString
]

{ #category : '*Kernel-Protocol' }
Object >> asString [
	^self displayString
]

{ #category : '*Kernel-Protocol' }
Object >> printNl [
	"Print on standard error, not on the Transcript.

	 Transcript draws, so a headless image cannot use it, and the whole
	 point of printNl is to work anywhere."
	^self reportOnStandardError: self printString
]

{ #category : '*Kernel-Protocol' }
Object >> displayNl [
	^self reportOnStandardError: self displayString
]

{ #category : '*Kernel-Protocol' }
Object >> assert: aBooleanOrBlock [
	"Signal unless the argument is, or answers, true.

	 It takes a block as readily as a boolean so that the check can be
	 written where it is cheap to skip, and cull: means either spelling
	 works."
	(aBooleanOrBlock cull: nil) == true
		ifFalse: [^AssertionFailure new signal: 'assertion failed']
]

{ #category : '*Kernel-Protocol' }
Object >> assert: aBooleanOrBlock description: aString [
	(aBooleanOrBlock cull: nil) == true
		ifFalse: [^AssertionFailure new signal: aString]
]

{ #category : '*Kernel-Protocol' }
Object >> deny: aBooleanOrBlock [
	(aBooleanOrBlock cull: nil) == true
		ifTrue: [^AssertionFailure new signal: 'denial failed']
]

{ #category : '*Kernel-Protocol' }
Object >> -> anObject [
	"An Association, which is how a pair is written."
	^Association key: self value: anObject
]

{ #category : '*Kernel-Protocol' }
Object >> identityHash [
	"The hash of the OBJECT rather than of its value, which is what an
	 identity collection needs: two equal Strings have one hash and two
	 identityHashes.

	 1983 has the primitive -- 75, and Object>>hash declares it -- but
	 never gave it this name, because in a system where hash IS identity
	 the distinction has nowhere to show.  Pharo's identity collections
	 send it, and they are right to."
	<primitive: 75>
	^self hash
]

{ #category : '*Kernel-Protocol' }
Object >> ~~ anObject [
	"Not the same object.  The primitive is the whole of it; the fallback
	 says the same thing one send more slowly."
	<primitive: 169>
	^(self == anObject) not
]

{ #category : '*Kernel-Protocol' }
Object >> shallowCopy [
	"Same class, same fields, new identity.  The primitive declines a
	 SmallInteger, which is its own copy and has no fields to walk."
	<primitive: 148>
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> isString [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isSymbol [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isCharacter [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isNumber [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isInteger [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isCollection [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isArray [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isBlock [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isBehavior [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> initialize [
	"Do nothing, and answer the receiver.

	 Pharo's Object defines this, and a great deal of Pharo depends on it
	 without ever saying so: every class whose own initialize begins
	 `super initialize' -- which is the convention, not the exception --
	 needs a method here to stop at.  Announcer and SubscriptionRegistry
	 both do, and both failed with 'Message not understood: initialize'
	 the first time a Pharo package was loaded.

	 1983 had no such convention: a class that wanted initialization wrote
	 `^super new initialize' on the class side and defined initialize
	 itself, so nothing ever sent it to an Object that had not asked for
	 it.  This is the one method that makes the two conventions meet, and
	 it belongs here rather than as an edit to imported source."
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> isNotNil [
	"Pharo's spelling of notNil.  Both exist there and both are used."
	^self notNil
]

{ #category : '*Kernel-Protocol' }
Object >> flag: aSymbolOrString [
	"A note to a future reader, and nothing at run time.

	 Pharo's sources are full of these -- they are how a maintainer leaves a
	 doubt where the code is rather than in an issue tracker -- and a system
	 that does not answer it turns every such note into a failure in code
	 that was working."
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> className [
	"My class's name as a String."
	^self class name
]

{ #category : '*Kernel-Protocol' }
Object >> asCollectionElement [
	"Me, as something a Set can hold.

	 Pharo's Set stores its elements in an Array and uses nil to mean an
	 empty slot, so nil itself cannot be stored directly -- it wraps nil in
	 a CollectionElement instead, and asks every element for its wrapper on
	 the way in.  For everything that is not nil the answer is the object
	 itself, which is why Pharo's own version has no explicit return.

	 See enclosedElement below, and UndefinedObject>>asCollectionElement
	 after it: the three are one mechanism, and adding any of them without
	 the others is worse than adding none."
	^self
]

{ #category : '*Kernel-Protocol' }
UndefinedObject >> asCollectionElement [
	"nil, boxed, because a Set cannot store me as myself.

	 A Set reads a nil slot as empty, so storing nil there does not add an
	 element -- it removes the evidence that a slot was ever used.  Boxing is
	 what lets `aSet add: nil' mean what it says, and the box comes straight
	 back off in enclosedElement, so nothing outside the Set's own array ever
	 sees one."
	^CollectionElement with: nil
]

{ #category : '*Kernel-Protocol' }
Object >> enclosedElement [
	"Me, unwrapped, on the way back OUT of a Set.

	 The other half of asCollectionElement, and the half I forgot.  Pharo's
	 Set wraps on the way in and unwraps on the way out, so a Set whose
	 elements can be stored and not read answers nil for every one of them
	 -- and nil then flows into whatever asked, which sends it something,
	 which fails somewhere else entirely.  One bootstrap produced 110170
	 `nil generality', 6024 `nil at:ifAbsent:' and 6008 `nil
	 activeController' from this single omission, and not one of those
	 messages names a Set.

	 Only a wrapper answers anything but itself."
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> split: aSequenceableCollection [
	"Cut that collection wherever I match.

	 On Object because I am the SPLITTER, not the thing being split, and a
	 splitter may be an element, a subsequence or a block -- Pharo puts the
	 protocol here for exactly that reason.  The work is in
	 SequenceableCollection>>splitOn:; this is the other spelling of it."
	^aSequenceableCollection splitOn: self
]

{ #category : '*Kernel-Protocol' }
Object >> join: aCollection [
	"Run that collection's elements together with me between them.

	 On Object because I am the JOINER, the mirror of split: above -- and a
	 joiner may be a single element or a sequence."
	^aCollection joinUsing: self
]

{ #category : '*Kernel-Protocol' }
Object >> copy [
	"Another instance just like me, its parts fixed up afterwards.

	 1983 answers a bare shallowCopy and leaves every class that needs more
	 to override copy itself.  Pharo splits it: copy is shallowCopy followed
	 by postCopy, and a class says what a copy needs by implementing
	 postCopy alone.  Both conventions are defensible and only one can be in
	 force, because a class written to the second one is SILENT under the
	 first -- Set, Bag, Dictionary, SmallDictionary and MethodDictionary all
	 define postCopy to copy their internal array and nothing was calling
	 it, so every copy of a Pharo collection shared its storage with the
	 original.  `aSet union: anotherSet' answered the right set and left the
	 receiver holding the union too."
	^self shallowCopy postCopy
]

{ #category : '*Kernel-Protocol' }
Object >> postCopy [
	"Fix up a fresh shallowCopy of me.  Nothing to do by default; a class
	 with internal storage copies it here.  This also gives the `super
	 postCopy' those classes all start with something to reach."
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> basicIdentityHash [
	"My identity hash, taken from the object pointer and not from my value.

	 Primitive 75 directly rather than `^self hash', and the difference
	 matters here more than it used to: lib/ gives Symbol a value-based
	 hash so that it agrees with a value-based =, so for a Symbol the two
	 answers are now different numbers.  Anything asking for the IDENTITY
	 hash has to get the identity one."
	<primitive: 75>
	^0
]

{ #category : '*Kernel-Protocol' }
Object >> identityHash [
	^self basicIdentityHash
]

{ #category : '*Kernel-Protocol' }
Object >> flushCache [
	"Discard whatever the VM cached about me.  Nothing, here.

	 Pharo's MethodDictionary sends this on every change, because a real
	 method lookup cache has to be told when a dictionary it has read from
	 is edited.  This VM has no such cache -- primitive 89 is a no-op and
	 doc/CONCURRENCY.md records why a future one would be per-worker with a
	 global epoch -- so there is nothing to discard and answering the
	 receiver is the truthful implementation rather than a stub.

	 If a cache is ever added, this method is one of the places that has to
	 stop being empty."
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> isDictionary [
	"Am I a Dictionary?  No, unless I say otherwise -- Pharo's Dictionary
	 does, in the imported package."
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> isCollection [
	^false
]

{ #category : '*Kernel-Protocol' }
Object >> value [
	"Myself.

	 Pharo defines this on Object, and the collection tests depend on it in
	 a way that is worth spelling out.  TSetArithmetic is one suite shared
	 by every collection, and it says

	     self assert: (inter includes: self anotherElementOrAssociationIn value)

	 because for a Dictionary that method answers an ASSOCIATION and for a
	 Bag or a Set it answers a plain element.  Sending #value to both is how
	 one test serves both, and it only works if every object answers it.

	 The cost is real and worth naming: a #value sent to something that was
	 never meant to receive it now answers the object instead of reporting a
	 doesNotUnderstand.  Association, BlockClosure and the exception classes
	 all override it, so what is left is the case where a plain object is
	 asked to evaluate itself, and Pharo has decided that answers itself."
	^self
]

{ #category : '*Kernel-Protocol' }
Object >> copyFrom: anotherObject [
	"Take anotherObject's state into myself, in place.

	 One argument, and that is the whole distinction from
	 SequenceableCollection>>copyFrom:to:, which answers a new collection.
	 This one MUTATES the receiver, which is why it exists: Pharo's
	 MethodDictionary grows by building a bigger dictionary, filling it, and
	 then pouring it back into the original -- so that every class already
	 pointing at that dictionary keeps pointing at it.  Answering a copy
	 would leave them all attached to the old one.

	 Named instance variables first, then the indexed part, each limited to
	 what both objects have.  Nothing is assumed about the two being the
	 same class: the shorter one decides, which is what makes it safe to use
	 while a class is being reshaped."
	| count |
	count := self class instSize min: anotherObject class instSize.
	1 to: count do: [:i |
		self instVarAt: i put: (anotherObject instVarAt: i)].
	(self class isVariable and: [anotherObject class isVariable]) ifTrue: [
		count := self basicSize min: anotherObject basicSize.
		1 to: count do: [:i |
			self basicAt: i put: (anotherObject basicAt: i)]].
	^self
]

{ #category : '*Kernel-Protocol' }
Association >> storeOn: aStream [
	"An expression that rebuilds me: (key->value).

	 Without this an Association stores as the generic
	 `Association basicNew instVarAt: 1 put: ...; instVarAt: 2 put: ...',
	 which is correct, unreadable, and depends on the field order never
	 changing."
	| stored |
	aStream nextPut: $(.
	key storeOn: aStream.
	aStream nextPutAll: '->'.
	stored := String streamContents: [:each | value storeOn: each].
	"
	 A space before a negative value, because `->-1' does not read back as
	 what it printed: the scanner takes `->-' as one binary selector and
	 then finds a 1 with nothing to send it to.  Pharo's own store tests
	 say `(#d-> -2)', and this is why.
	"
	(stored isEmpty not and: [stored first = $-])
		ifTrue: [aStream nextPut: $ ].
	aStream nextPutAll: stored.
	aStream nextPut: $)
]

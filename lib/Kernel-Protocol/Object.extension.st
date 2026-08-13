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

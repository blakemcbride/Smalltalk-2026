Extension { #name : 'Behavior' }

{ #category : '*Kernel-Protocol' }
Behavior >> isBehavior [
	^true
]

{ #category : '*Kernel-Protocol' }
Behavior >> includesBehavior: aClass [
	"True if I am aClass or inherit from it.

	 Pharo's name for a test 1983 spells two ways and never named once.
	 Announcements asks it of every subscription on every announcement, so
	 it was eleven of the twenty-two errors in AnnouncerTest by itself --
	 one method, and the largest single group."
	^self == aClass or: [self inheritsFrom: aClass]
]

{ #category : '*Kernel-Protocol' }
Behavior >> environment [
	"Where my global names live.

	 Pharo's classes each carry an environment so that one image can hold
	 several namespaces.  This system has the one SystemDictionary, so the
	 answer is always the same -- but the SENDERS are worth supporting as
	 written, because `self environment at: #Foo ifAbsent: [...]' is how
	 ported code asks for a class that may not have been loaded."
	^Smalltalk
]

{ #category : '*Kernel-Protocol' }
Behavior >> >> aSelector [
	"The compiled method I hold for that selector.

	 `Dictionary >> #at:put:' is how Pharo names a method as a value, and
	 its tests use it constantly -- MethodDictionaryTest sent it 36 times in
	 one run.  Two characters, and it reads the way the literature writes
	 it."
	^self compiledMethodAt: aSelector
]

{ #category : '*Kernel-Protocol' }
Behavior >> methodDict [
	"The dictionary mapping my selectors to their compiled methods.

	 1983 keeps it in an instance variable and reaches it directly from
	 within Behavior, so it never needed a reader.  Anything OUTSIDE the
	 class does -- MethodDictionaryTest exists precisely to poke at one --
	 and answering the real dictionary rather than a copy is deliberate:
	 the tests edit it."
	^methodDict
]

{ #category : '*Kernel-Protocol' }
Behavior >> superclass: aClass [
	"Change my superclass to be aClass -- unless that would make my chain
	 go round (Bugs3 B6).

	 1983 checks only that the argument is a Behavior.  `CA superclass: CB.
	 CB superclass: CA' then made a chain with no top, and the next send to
	 an instance walked it in C for ever: no safepoint, so SIGTERM could not
	 stop the worker and the next collection parked every other worker
	 behind it.  The interpreter now bounds the walk and answers `not
	 understood' for a chain that long; this refuses to build one, which is
	 the answer that names the mistake where it is made.

	 A class is its own ancestor for this purpose, and nil is always
	 allowed: it is the top of every chain."
	aClass isNil ifTrue: [^superclass := nil].
	(aClass isKindOf: Behavior)
		ifFalse: [^self error: 'superclass must be a class-describing object'].
	(aClass == self or: [aClass inheritsFrom: self])
		ifTrue: [^self error: aClass name , ' inherits from ' , self name
			, ', so making it the superclass would put a cycle in the class chain'].
	superclass := aClass
]

{ #category : '*Kernel-Protocol' }
Behavior >> deepCopy [
	"Myself.  A class or metaclass reached through an instance variable
	 -- a Browser's selected class, a Set's species, anything that keeps a
	 class as a value -- is a reference into the system, not a part of the
	 object being copied, and deep-copying it copies the method
	 dictionary, every method's literals, the superclass chain and the
	 subclasses, which is the image (Bugs3 B47).  A copy of a class is
	 made with subclass:instanceVariableNames:..., on purpose, by name."
	^self
]

{ #category : '*Kernel-Protocol' }
Behavior >> isWeak [
	"Whether the collector leaves my instances' indexed fields alone, so
	 that what is reached only through one of them is collected and the
	 field set to nil.  WeakArray in lib/Kernel is the one that says so.

	 The bit has been in the format word since the loader learned to read
	 `#type : 'weak'' -- bit 11 of the value, the one the Blue Book leaves
	 unused between the indexable flag and the instance size, and the VM's
	 shape_of_class reads it there -- but nothing in the image asked, so
	 TonelWriter wrote every weak class back as `#type : 'variable'' and
	 a reload of its own output made it strong (Bugs3 B61).  Read the way
	 isVariable and isBytes read their bits: from the format itself, with
	 no primitive to keep in step."
	^(format bitAnd: 2048) ~= 0
]

{ #category : '*Kernel-Protocol' }
Behavior >> isEphemeron [
	"Whether my instances are ephemerons: the first named field is a key
	 held weakly, and the whole object stays strong exactly as long as
	 that key is reachable some other way.  Pharo's WeakKeyAssociation is
	 one; `#type : 'ephemeron'' in a Tonel header makes one.

	 Bit 15 of the value, above the whole Blue Book layout, because bits
	 12 to 14 are taken and 11 is isWeak's -- src/boot/bootstrap.c's
	 make_format is where the layout is laid down.  Not an indexed shape:
	 isVariable answers false for an ephemeron class.

	 Only in a format that is not negative.  1983's
	 format:variable:words:pointers: writes the pointers flag as -16384,
	 the sign bit of a 15-bit SmallInteger, and here the sign extends
	 through bit 15 and everything above it: a class defined in the
	 Browser has a format of -4096 or so, and reading bit 15 alone called
	 every such class an ephemeron.  (The VM read it the same way and
	 allocated their instances as ephemerons; src/interp/prim.c's
	 shape_of_class now applies this rule too.)  The bootstrap writes
	 positive words and nothing of 1983's makes an ephemeron, so the sign
	 settles it."
	^format >= 0 and: [(format bitAnd: 32768) ~= 0]
]

{ #category : '*Kernel-Protocol' }
Behavior >> beWeak [
	"Set the weak bit in my format, so that instances made from now on
	 have their indexed fields left untraced.  For Class>>weakSubclass:...
	 and the Tonel reader, which build a weak class at run time out of an
	 ordinary variable one, since the 1983 definition messages have no
	 word for weakness and Metaclass>>name:inEnvironment:... recomputes
	 the format from the three words it does have.

	 Instances that already exist keep the strength they were made with:
	 the flag that matters at collection time is copied into each object's
	 header when it is allocated (om_mt.c, OM_instantiate_weak), which is
	 what lets the collector run over the raw table without asking a
	 class anything.  A class is made weak before it has instances, so
	 that is a statement about redefinition, not about use."
	format := format bitOr: 2048
]

{ #category : '*Kernel-Protocol' }
Behavior >> beEphemeron [
	"Set the ephemeron bit in my format; see beWeak for when and why.

	 The word is first brought to the bootstrap's convention -- its low
	 fifteen bits, which are the same in both, with the sign gone -- so
	 that bit 15 can mean what isEphemeron and the VM read it as.  A
	 format 1983's definition messages wrote is negative, and setting a
	 bit that the sign extension already has set would change nothing."
	format := (format bitAnd: 16r7FFF) bitOr: 32768
]

{ #category : '*Kernel-Protocol' }
Behavior >> beStrong [
	"Clear the weak and ephemeron bits: my instances made from now on are
	 traced whole.  What TonelReader sends when a header that said weak
	 or ephemeron says so no longer, since format:variable:words:pointers:
	 below carries the bits and nothing else takes them away.  Bit 15 is
	 touched only in a positive word: in a negative one it is the sign,
	 which isEphemeron does not read and which must stay what it is."
	format := format bitAnd: 2048 bitInvert.
	format >= 0 ifTrue: [format := format bitAnd: 32768 bitInvert]
]

{ #category : '*Kernel-Protocol' }
Behavior >> format: nInstVars variable: isVar words: isWords pointers: isPointers [
	"Set the format for the receiver (a Class): 1983's method, with the
	 two bits its four arguments cannot express carried over from the
	 format I have now.

	 1983's version recomputes the word from scratch, and it is what every
	 definition message ends in -- subclass:..., variableSubclass:...,
	 addInstVarName:, the Browser's class template.  A weak class put
	 through any of them came out strong, silently, because the message
	 has no word for weak: WeakArray given an instance variable in the
	 Browser was an ordinary Array afterwards, and TonelReader redefining
	 a weak class into the same shape got a new class object every time,
	 since the recomputed word differed from the old one by the bit
	 (Bugs3 B61).  Weakness is a fact about the class that the message
	 did not mention, so it is kept; beStrong is the way to say otherwise
	 and the Tonel reader says it when a header does.

	 The pointers flag is written as -16384 exactly as 1983 wrote it --
	 the sign bit of a 15-bit SmallInteger -- and every reader of the
	 word here (isBits, isVariable, isBytes, the VM's shape_of_class,
	 isWeak, isEphemeron) knows both that word and the bootstrap's
	 positive one.  An ephemeron is carried in the positive form, since
	 bit 15 means nothing in a negative word; see isEphemeron."
	| weak ephemeron |
	weak := self isWeak.
	ephemeron := self isEphemeron.
	format := nInstVars +
				(isVar ifTrue: [4096] ifFalse: [0]) +
				(isWords ifTrue: [8192] ifFalse: [0]) +
				(isPointers ifTrue: [-16384] ifFalse: [0]).
	weak ifTrue: [format := format bitOr: 2048].
	ephemeron ifTrue: [format := (format bitAnd: 16r7FFF) bitOr: 32768]
]

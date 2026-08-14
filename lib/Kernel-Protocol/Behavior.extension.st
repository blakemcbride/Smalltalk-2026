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

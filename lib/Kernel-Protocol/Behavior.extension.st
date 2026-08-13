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

Extension { #name : 'Dictionary' }

{ #category : '*Collections-Protocol' }
Dictionary >> at: key ifAbsentPut: aBlock [
	"Answer the value, adding the block's answer first if there is none.

	 Under threads this is check-then-act and two workers can each build
	 one; see doc/ParallelAudit.md when that day comes.  Single-threaded
	 it is the idiom every cache is written with."
	^self at: key ifAbsent: [self at: key put: aBlock value]
]

{ #category : '*Collections-Protocol' }
Dictionary >> keysAndValuesDo: aBlock [
	self associationsDo: [:each | aBlock value: each key value: each value]
]

{ #category : '*Collections-Protocol' }
Dictionary >> keysSortedSafely [
	^self keys asSortedCollection asArray
]

{ #category : '*Collections-Protocol' }
Dictionary >> associationsSortedByKey [
	^(self associations asSortedCollection: [:a :b | a key <= b key]) asArray
]

{ #category : '*Collections-Protocol' }
Dictionary >> , aDictionary [
	"Both dictionaries merged, the argument's bindings winning.

	 Overridden from Collection>>, because that one appends what do: yields,
	 and a Dictionary's do: yields VALUES -- so the generic version added
	 values where associations belong and the result then sent #key to a
	 SmallInteger.  Concatenating two dictionaries means merging their
	 KEYS, and the tests say the later one wins: `dictionary1 , dictionary2'
	 has dictionary2's size when the keys coincide."
	| merged |
	merged := self copy.
	aDictionary associationsDo: [:each |
		merged at: each key put: each value].
	^merged
]

{ #category : '*Collections-Protocol' }
Dictionary >> inspect [
	"Open an inspector on me, or say what I am when there is nowhere to
	 open one.

	 Object>>inspect and Object>>basicInspect are guarded in
	 lib/Kernel-Exceptions for the reason given there -- an InspectorView
	 opened with no screen parks the process on the input semaphore for
	 ever -- and this is the second door into the same room: 1983's
	 Dictionary>>inspect opens a DictionaryInspector directly rather than
	 going through either of them."
	self canOpenUserInterface ifFalse: [^super inspect].
	InspectorView open: (DictionaryInspector inspect: self)
]

"
The one operation a lock-free algorithm cannot be written without.
"
Extension { #name : 'Object' }

{ #category : '*Concurrency' }
Object >> compareAndSwapSlot: index from: expected to: wanted [
	"Store wanted in the indexed field only if it currently holds
	 expected, and answer whether it did.

	 It answers the BOOLEAN rather than the old value, because that is
	 what every caller tests and it leaves no room to forget the
	 comparison.  The primitive fails for a receiver with no pointer
	 fields, or an index outside them, rather than writing somewhere
	 else."
	<primitive: 245>
	^self primitiveFailed
]

{ #category : '*Concurrency' }
Object >> addDependent: anObject [
	"Add anObject as one of the receiver's dependents.

	 DependentsFields is one IdentityDictionary for every model in the
	 image, and 1983 wrote to it without a lock: two workers each adding a
	 dependent to their own model both grew the same table, and 143 of
	 1,600 dependents were lost at eight workers -- 18 of 400 at two.  Every
	 writer now holds LibraryLocks' dependents lock; setDependents is 1983's
	 and is only ever sent from inside it."
	LibraryLocks holdingDependents: [
		(DependentsFields at: self ifAbsent: [self setDependents]) add: anObject].
	^anObject
]

{ #category : '*Concurrency' }
Object >> removeDependent: anObject [
	"Remove anObject as one of the receiver's dependents.  The table entry
	 goes with the last dependent, here rather than through breakDependents,
	 which takes the lock this already holds."
	LibraryLocks holdingDependents: [ | dependents |
		dependents := DependentsFields at: self ifAbsent: [nil].
		dependents == nil ifFalse: [
			dependents remove: anObject ifAbsent: [].
			dependents isEmpty ifTrue: [DependentsFields removeKey: self ifAbsent: []]]].
	^anObject
]

{ #category : '*Concurrency' }
Object >> dependents [
	"An OrderedCollection of the objects that depend on the receiver -- a
	 copy, so that changed: can walk it while another worker adds one."
	^LibraryLocks holdingDependents: [ | dependents |
		dependents := DependentsFields at: self ifAbsent: [nil].
		dependents == nil ifTrue: [OrderedCollection new] ifFalse: [dependents copy]]
]

{ #category : '*Concurrency' }
Object >> breakDependents [
	"Remove all of the receiver's dependents."
	LibraryLocks holdingDependents: [DependentsFields removeKey: self ifAbsent: []]
]

{ #category : '*Concurrency' }
Object >> withArgs: anArray executeMethod: aCompiledMethod [
	"Run aCompiledMethod on me with those arguments, without its being
	 installed in any method dictionary.  Primitive 188, Pharo's number and
	 name; the compiler evaluates expressions through it so that two
	 workers evaluating at once do not share one #DoIt slot."
	<primitive: 188>
	anArray isArray
		ifFalse: [^self error: 'withArgs:executeMethod: takes an Array of arguments'].
	(aCompiledMethod isKindOf: CompiledMethod)
		ifFalse: [^self error: 'withArgs:executeMethod: takes a CompiledMethod'].
	aCompiledMethod numArgs = anArray size
		ifFalse: [^self error: 'withArgs:executeMethod: needs an Array of the arguments the method takes'].
	"The elements are spread onto the calling method's stack, in a frame
	 the compiler sized for that method's own pushes (Bugs3 B1)."
	^self error: 'withArgs:executeMethod: cannot spread ' , anArray size printString
		, ' arguments: the calling method''s frame has no room for them'
]

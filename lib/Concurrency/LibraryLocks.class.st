"
The locks the 1983 library never had.

Five pieces of state in sources/ are shared by the whole image and written
without any lock, which was correct under a scheduler that never preempts a
process by one of the same priority and is wrong on cores: the Symbol table
(Symbol class>>intern:), the system dictionary (Smalltalk at:put:), the
dependents table (Object>>addDependent:, one IdentityDictionary for every
model in the image), the list of open directories (FileDirectory's
ExternalReferences), and the method dictionaries (Behavior>>addSelector:
withMethod:, one per class but written by every worker that compiles).  Each one lost entries when real workers wrote to it
together -- two workers interning the same new string made two Symbols,
which is the worst of them, because a Symbol that is not identical to
itself cannot be found as a selector.

They are serialized here, one Mutex each, held by the methods in lib/ that
replace the 1983 writers.  The locks live in this class rather than in the
classes they guard because a class in sources/ cannot be given a new class
variable from lib/, and a global would be a lock anyone could forget.

The holding... methods run their block without a lock when the lock does
not exist yet.  That is the bootstrap: class initializers run one after
another on one thread before any worker pool exists, and some of them
intern symbols and declare globals before this class's own initializer has
run.  Once it has, the check is never true again, so the check-then-act
that doc/CONCURRENCY.md forbids is not one here -- nothing else runs until
every initializer has.

Reads are not locked.  A reader probing a Dictionary while a writer grows
it sees the old table or the new one, both consistent, because growth
copies into a fresh object and become:s it -- and a Symbol table bucket is
replaced by a copy, never edited.  What the locks prevent is two WRITERS
each seeing the same empty slot.
"
Class {
	#name : 'LibraryLocks',
	#superclass : 'Object',
	#classVars : [
		'Dependents',
		'Files',
		'Globals',
		'Methods',
		'Symbols'
	],
	#category : 'Concurrency'
}

{ #category : 'class initialization' }
LibraryLocks class >> initialize [
	"LibraryLocks initialize"
	Symbols := Mutex new.
	Globals := Mutex new.
	Dependents := Mutex new.
	Files := Mutex new.
	Methods := Mutex new
]

{ #category : 'holding' }
LibraryLocks class >> holdingMethods: aBlock [
	"Evaluate aBlock holding the lock on every method dictionary."
	^Methods isNil ifTrue: [aBlock value] ifFalse: [Methods critical: aBlock]
]

{ #category : 'holding' }
LibraryLocks class >> holdingSymbols: aBlock [
	"Evaluate aBlock holding the lock on the Symbol table."
	^Symbols isNil ifTrue: [aBlock value] ifFalse: [Symbols critical: aBlock]
]

{ #category : 'holding' }
LibraryLocks class >> holdingGlobals: aBlock [
	"Evaluate aBlock holding the lock on Smalltalk's entries."
	^Globals isNil ifTrue: [aBlock value] ifFalse: [Globals critical: aBlock]
]

{ #category : 'holding' }
LibraryLocks class >> holdingDependents: aBlock [
	"Evaluate aBlock holding the lock on the dependents table."
	^Dependents isNil ifTrue: [aBlock value] ifFalse: [Dependents critical: aBlock]
]

{ #category : 'holding' }
LibraryLocks class >> holdingFiles: aBlock [
	"Evaluate aBlock holding the lock on the open-directory list."
	^Files isNil ifTrue: [aBlock value] ifFalse: [Files critical: aBlock]
]

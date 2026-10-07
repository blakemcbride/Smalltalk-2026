"
The locks the 1983 library never had.

Eight pieces of state in sources/ are shared by the whole image and written
without any lock, which was correct under a scheduler that never preempts a
process by one of the same priority and is wrong on cores: the Symbol table
(Symbol class>>intern:), the system dictionary (Smalltalk at:put:), the
dependents table (Object>>addDependent:, one IdentityDictionary for every
model in the image), the list of open directories (FileDirectory's
ExternalReferences), the method dictionaries (Behavior>>addSelector:
withMethod:, one per class but written by every worker that compiles), and
the Transcript's entry stream (TextCollector>>nextPutAll:, one WriteStream
on one String, which grows by become: under whoever is writing to it), and
the source files (SourceFiles, whose two streams every compile writes and
every sourceCodeAt: repositions -- see holdingSources:), and what a class
definition writes besides the global: the organizers (ClassOrganizer, three
parallel Arrays replaced one at a time), every class's Set of subclasses
(grown by become: under whoever is adding), and the system change set --
see holdingClasses:.  Each one lost entries when real workers wrote to it
together -- two workers interning the same new string made two Symbols,
which is the worst of them, because a Symbol that is not identical to
itself cannot be found as a selector.

They are serialized here, one Mutex each (a Monitor where the protected
methods send one another), held by the methods in lib/ that replace the
1983 writers.  The locks live in this class rather than in the classes they
guard because a class in sources/ cannot be given a new class variable
from lib/, and a global would be a lock anyone could forget.

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
		'Classes',
		'Dependents',
		'Files',
		'Globals',
		'Methods',
		'Sources',
		'Symbols',
		'Transcript'
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
	Methods := Mutex new.
	Transcript := Mutex new.
	Sources := Monitor new.
	Classes := Monitor new
]

{ #category : 'holding' }
LibraryLocks class >> holdingTranscript: aBlock [
	"Evaluate aBlock holding the lock on the Transcript's entry stream."
	^Transcript isNil ifTrue: [aBlock value] ifFalse: [Transcript critical: aBlock]
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

{ #category : 'holding' }
LibraryLocks class >> holdingSources: aBlock [
	"Evaluate aBlock holding the lock on the source files -- SourceFiles,
	 the .sources and .changes streams every compile writes and every
	 sourceCodeAt: reads.

	 One FileStream each, shared by the whole image, and every use of one
	 moves its position: setToEnd, write, readOnly to log a method;
	 position:, nextChunk to read one back.  Eight workers compiling at
	 once interleaved all of that in one stream -- `no writing allowed'
	 when one flipped it readOnly under another, and source pointers that
	 named somebody else's text (Bugs5 FILES-2).

	 A Monitor, not a Mutex, because condenseChanges holds it while it
	 reads and writes every changed method; reentrant for any scheduled
	 process, which is everything that runs it."
	^Sources isNil ifTrue: [aBlock value] ifFalse: [Sources critical: aBlock]
]

{ #category : 'holding' }
LibraryLocks class >> holdingClasses: aBlock [
	"Evaluate aBlock holding the lock on what a class definition writes
	 besides the global: every ClassOrganizer -- SystemOrganization and
	 each class's own -- every class's Set of subclasses, and the system
	 change set.

	 Metaclass>>name:inEnvironment:subclassOf:... declares the global
	 under the Globals lock and then, with no lock at all, classifies the
	 class in SystemOrganization, adds it to its superclass's subclasses
	 and tells Smalltalk changes.  Eight workers defining fifty classes
	 each under one superclass lost 14-125 of the 400 subclasses and
	 205-229 of the 400 organizer entries every run, printed
	 SubscriptOutOfBounds from the organizer, and one run in six looped
	 for ever on a nil read out of a half-replaced array (Bugs6 KERNB-1).
	 Two REST services loading on their first requests is this case.

	 One lock for the three, because one definition writes all three and
	 nothing else orders them.  A Monitor, not a Mutex: the organizer's
	 methods send one another (classify:under: sends addCategory:,
	 removeElement: and removeCategory:) and each takes the lock for
	 itself, so that every entry is safe whoever sends it.  Readers take
	 it too -- the three arrays are replaced one at a time, and a reader
	 between two of the replacements indexes one array by the other's
	 stops.  Held only inside those methods, never across a whole
	 definition: a definition also compiles, and compiling takes the
	 Methods and Sources locks, which would otherwise have to be ordered
	 against this one.

	 Unlocked, too, while there is no active process.  The bootstrap's
	 last phases -- the subclass graph, every class's organization, the
	 system organization -- run after this class's initializer, each on a
	 bare context that is no Process, and a Monitor tells re-entry from a
	 second process by asking which process is in it: with nobody there
	 it takes the second entry for a second process and waits on a lock
	 it holds itself.  With no process there is nobody else either, which
	 is the same argument as the nil lock above; and nothing runs without
	 a process once the image is up."
	^(Classes isNil or: [Processor activeProcess isNil])
		ifTrue: [aBlock value]
		ifFalse: [Classes critical: aBlock]
]

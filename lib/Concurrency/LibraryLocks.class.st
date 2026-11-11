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

What the locks prevent first is two WRITERS each seeing the same empty
slot.  Whether a lock's READERS hold it too depends on how its table
changes, and the answer is not the same for every lock.

A 1983 Dictionary or Set that grows copies itself into a bigger object and
become:s it, and that does not give a reader the old table or the new one,
both consistent, as it may seem to: a reader that had read the old
basicSize and computed its probe from it goes on probing the NEW table
with the old arithmetic, and misses a key that is there (Bugs6 DOCS-22).
So Smalltalk's readers hold the globals lock, as its writers do (see
SystemDictionary>>at:ifAbsent: in lib/System), and Object>>dependents
holds the dependents lock while it copies a model's dependents; the
organizers' and the source files' readers hold their locks for reasons
of their own, given in holdingClasses: and holdingSources:.  Smalltalk's
methods run only their own code inside that lock, never a block they
were handed: the lock is a Mutex, which refuses re-entry, and a caller's
block can do anything, including take the Symbol lock, which is ordered
before this one.

Three are read with no lock, each for a reason that is its own.  Symbol
class>>hasInterned:ifTrue:, the fast path of every asSymbol, reads the
Symbol table once into a temporary: growTable puts a complete new table in
place with one store, and a bucket is replaced by a copy and never edited,
so a reader looks in one whole table and one whole bucket, and a name it
misses is looked for again under the lock.  Sends read the method
dictionaries in the VM, not in Smalltalk; why they need no lock is in
lib/Concurrency/Behavior.extension.st.  And a weak dictionary, which its
owner shares with the Finalizer, is read whole by the owner because the
Finalizer puts its rebuilt table in place with one store; see
holdingMourning:.
"
Class {
	#name : 'LibraryLocks',
	#superclass : 'Object',
	#classVars : [
		'Classes',
		'Dependents',
		'Mourning',
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
	Classes := Monitor new.
	Mourning := Monitor new
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
LibraryLocks class >> readingGlobals: aBlock [
	"Evaluate aBlock holding the lock on Smalltalk's entries, or without
	 taking it when the active process holds it already.

	 For Smalltalk's readers, and only for blocks of their own that read
	 the table -- never a caller's block (Bugs07 KERNB-2).  A reader runs
	 inside a writer in one ordinary case: 1983's Dictionary>>grow, sent
	 from the middle of a declaration, copies the table with `self
	 associationsDo:', and Smalltalk's associationsDo: is a reader.  The
	 lock is a Mutex and refuses re-entry, so without this the first
	 declaration that grew the table raised.  Nothing is lost by it: the
	 lock keeps OTHER processes' writes out, and the process holding it
	 is the only one that can be writing.

	 Asking the Mutex for its owner is safe for this one question even
	 though the answer to it can be stale in general: no process but
	 this one ever makes this process the owner or stops it being one,
	 so `owner == me' is true exactly while this process holds it."
	| me |
	me := Processor activeProcess.
	(me notNil and: [Globals notNil and: [Globals owner == me]])
		ifTrue: [^aBlock value].
	^self holdingGlobals: aBlock
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

{ #category : 'holding' }
LibraryLocks class >> holdingMourning: aBlock [
	"Evaluate aBlock holding the lock on every weak-keyed dictionary's
	 table: what WeakKeyDictionary>>at:put: and removeKey:ifAbsent: take,
	 and what the Finalizer takes through removeMourned: and
	 finishMourning.

	 The second writer to a WeakKeyDictionary is the system.  When a key
	 dies the collector queues the association and the finalization
	 process, on another worker at userInterruptPriority, sends it mourn,
	 which is `container removeKey: key' -- into a dictionary whose owner
	 may be inside at:put:, growing or rehashing it.  The two walks lost
	 each other's updates: associations whose key was dead stayed in the
	 array after their one mourn (the ephemeron bit is cleared when it is
	 queued, so they were never queued again) and held the dead key
	 strongly for ever -- six per cent of twenty thousand on eight
	 workers, none on one (Bugs6 OM-6).  doc/CONCURRENCY.md leaves
	 unsynchronised collections to their owner, but an owner cannot lock
	 against a process it cannot see, so the dictionary's writers and the
	 mourning take one lock here.  A Monitor, because grow re-adds through
	 the same methods.  The same for WeakIdentityKeyDictionary, which is
	 not a WeakKeyDictionary and had no lock at all.

	 Readers are not held, for a reason of this lock's own; the class
	 comment says which locks' readers are held, and why.  Mourning used
	 to edit the table in place -- a nil, then the rest of the cluster
	 swapped back one entry at a time -- and a reader probing for a live
	 key further down stopped at the nil and answered absent (Bugs07
	 FIXES-4).  The Finalizer's removal now builds a new table and puts
	 it in place with one store, and the readers in
	 lib/Collections-Weak-Compat read the table once, so a reader sees one
	 whole table or the other.

	 One lock for every weak dictionary means nothing slow is done under
	 it.  A mourn holds it for one store, the mark on its entry, and the
	 marked entries are taken out of each table once per drain of the
	 Finalizer, in one rebuild; removing them one at a time, each walking
	 its cluster, stalled the owner's writes for minutes on eight workers
	 (Bugs07 FIXES-2)."
	^(Mourning isNil or: [Processor activeProcess isNil])
		ifTrue: [aBlock value]
		ifFalse: [Mourning critical: aBlock]
]

"
Method dictionaries, written under a lock.

A method dictionary is an IdentityDictionary, and installing or removing a
method is find-the-slot-then-write, like any other.  Two workers compiling
into the same class at once -- or one compiling while another removes --
could each find the same empty slot, and a class would lose a method it
had just been given.  Sends are not locked and need not be: the VM probes
the dictionary the way it was filled and stops at the first nil, and a
dictionary that grows is replaced whole.

Only the write to methodDict is held.  What 1983 does around it -- the
cache flush, the bookkeeping of copied and conflicting selectors, telling
the change set -- stays outside, unchanged, because some of it reaches
other classes' dictionaries and the lock is not re-entrant.

The list of subclasses, the same way, under the Classes lock (Bugs6
KERNB-1).  It is a Set that grows by become: under whoever is adding to
it, and eight workers each defining fifty subclasses of one class lost
14-125 of the 400 every run.  addSubclass:, removeSubclass:, the copy
that subclasses answers -- 1983 already answered a copy, but copied it
while another worker could be growing it -- and Behavior>>copy, which
nils the variable while it copies the class and would have a concurrent
addSubclass: start a second Set that the restore then threw away.
"
Extension { #name : 'Behavior' }

{ #category : '*Concurrency' }
Behavior >> addSelector: selector withMethod: compiledMethod [
	"Add the message selector with the corresponding compiled method to
	 the receiver's method dictionary."
	| wasThere |
	wasThere := LibraryLocks holdingMethods: [ | had |
		had := methodDict includesKey: selector.
		methodDict at: selector put: compiledMethod.
		had].
	self flushCache.
	((SelectorsOfConflictMethods at: selector ifAbsent: [Array new]) includes: self)
		ifTrue: [self removeClass: self selector: selector in: SelectorsOfConflictMethods].
	((SelectorsOfCopiedMethods at: selector ifAbsent: [Array new]) includes: self)
		ifTrue: [self removeClass: self selector: selector in: SelectorsOfCopiedMethods].
	wasThere
		ifTrue: [self checkChangeSelector: selector]
		ifFalse: [self subclasses do: [:sub | sub checkSuperAddSelector: selector]]
]

{ #category : '*Concurrency' }
Behavior >> removeSelector: selector [
	"Remove the selector from the receiver's method dictionary, or create
	 an error notification if it is not there."
	LibraryLocks holdingMethods: [methodDict removeKey: selector].
	self flushCache.
	self checkChangeSelector: selector
]

{ #category : '*Concurrency' }
Behavior >> removeSelectorSimply: selector [
	"Remove the selector from the receiver's method dictionary, quietly."
	| had |
	had := LibraryLocks holdingMethods: [
		methodDict removeKey: selector ifAbsent: [nil].
		true].
	had ifTrue: [self flushCache]
]

{ #category : '*Concurrency' }
Behavior >> addSubclass: aSubclass [
	"Make the argument, aSubclass, be one of the subclasses of the receiver."

	(aSubclass superclasses includes: self)
		ifTrue: [LibraryLocks holdingClasses: [
			subclasses == nil
				ifTrue: [subclasses := Set with: aSubclass]
				ifFalse: [subclasses add: aSubclass]]]
		ifFalse: [self error: aSubclass name , ' is not my subclass']
]

{ #category : '*Concurrency' }
Behavior >> removeSubclass: aSubclass [
	"If the argument, aSubclass, is one of the receiver's subclasses, remove it."
	LibraryLocks holdingClasses: [
		subclasses == nil ifFalse:
			[subclasses remove: aSubclass ifAbsent: [].
			subclasses isEmpty ifTrue: [subclasses := nil]]]
]

{ #category : '*Concurrency' }
Behavior >> subclasses [
	"Answer the receiver's subclasses.  Return a copy so that callers who
	add or delete subclasses won't get confused."

	^LibraryLocks holdingClasses: [
		subclasses == nil
			ifTrue: [Set new]
			ifFalse: [subclasses copy]]
]

{ #category : '*Concurrency' }
Behavior >> copy [
	"Make a copy of the receiver without a list of subclasses."

	| myCopy savedSubclasses |
	^LibraryLocks holdingClasses: [
		savedSubclasses := subclasses.
		subclasses := nil.
		myCopy := self shallowCopy.
		subclasses := savedSubclasses.
		myCopy methodDictionary: methodDict copy]
]

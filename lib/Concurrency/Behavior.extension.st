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

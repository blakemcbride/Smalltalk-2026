"
Interning, serialized.

Symbol class>>intern: is a read-modify-write on USTable: look the string
up, and if it is not there make a Symbol and put it in its bucket.  Two
workers interning the same new string both looked, both missed, and both
made one -- 132 of 600 symbols disagreed across two workers, 1,170 of
2,400 across eight.  A Symbol that is not identical to itself is the worst
kind of wrong: `#foo == 'foo' asSymbol' answers false, perform: with a
selector made at run time does not understand, and a Dictionary keyed by
Symbols cannot find what it holds.

intern: now holds LibraryLocks' Symbol lock around the 1983 body, which is
basicIntern: below, unchanged.  The re-check is inside the lock, which is
the whole point: the second worker, once it gets the lock, finds the
first's Symbol.  Reads (hasInterned:ifTrue:, sent by asSymbol before
anything else) stay unlocked: a bucket is replaced by a copy, never edited
in place, so a reader sees the old bucket or the new one.

rehash rebuilds the table, so it holds the lock too, and interns with
basicIntern: because the lock is not re-entrant.
"
Extension { #name : 'Symbol' }

{ #category : '*Concurrency' }
Symbol class >> intern: aString [
	"Answer a unique Symbol whose characters are those of aString."
	^LibraryLocks holdingSymbols: [self basicIntern: aString]
]

{ #category : '*Concurrency' }
Symbol class >> basicIntern: aString [
	"1983's intern:, to be run with the Symbol lock held."
	| sym index |
	self hasInterned: aString ifTrue: [:existing | ^existing].
	sym := (aString isMemberOf: Symbol)
		ifTrue: [aString]
		ifFalse: [(Symbol new: aString size) string: aString].
	index := aString stringhash \\ USTable size + 1.
	USTable at: index put: ((USTable at: index) copyWith: sym).
	^sym
]

{ #category : '*Concurrency' }
Symbol class >> rehash [
	"Rebuild the hash table that holds all the unique Symbols."
	LibraryLocks holdingSymbols: [
		USTable := USTable collect: [:each | Array new: 0].
		Symbol allInstancesDo: [:sym | self basicIntern: sym]]
]

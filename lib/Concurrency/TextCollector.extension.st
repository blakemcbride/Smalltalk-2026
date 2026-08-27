"
The Transcript, written to by many workers at once.

A TextCollector appends everything it is shown to one WriteStream on one
String, and a WriteStream that runs out of room grows its String with
become:.  Eight workers showing a line each at once were eight threads
writing into one String while it was being replaced under them: the bytes
of one worker's line landed in another's, and ThreadSanitizer saw the
freed String's memory handed to a new object while a worker still wrote
into it.  doc/CONCURRENCY.md had promised only that Transcript output
`may interleave'; it did worse.

Every method that touches the entry stream now holds LibraryLocks'
Transcript lock for the length of one operation.  Whole sends are atomic
-- one show: is one line -- and lines from different workers still come in
whatever order they were sent, which is what interleaving now means.
print: prints outside the lock and appends inside it, because printOn: is
the receiver's own code and may show something itself; the lock is not
re-entrant.  show: is 1983's -- nextPutAll: then endEntry -- and takes the
lock twice, once for each.
"
Extension { #name : 'TextCollector' }

{ #category : '*Concurrency' }
TextCollector >> nextPut: aCharacter [
	"Append aCharacter to the text."
	^LibraryLocks holdingTranscript: [entryStream nextPut: aCharacter]
]

{ #category : '*Concurrency' }
TextCollector >> nextPutAll: aCollection [
	"Append all the characters in aCollection to the text."
	^LibraryLocks holdingTranscript: [entryStream nextPutAll: aCollection]
]

{ #category : '*Concurrency' }
TextCollector >> next: anInteger put: aCharacter [
	"Append aCharacter anInteger times."
	^LibraryLocks holdingTranscript: [entryStream next: anInteger put: aCharacter]
]

{ #category : '*Concurrency' }
TextCollector >> cr [
	^LibraryLocks holdingTranscript: [entryStream cr]
]

{ #category : '*Concurrency' }
TextCollector >> crtab [
	^LibraryLocks holdingTranscript: [entryStream crtab]
]

{ #category : '*Concurrency' }
TextCollector >> crtab: anInteger [
	^LibraryLocks holdingTranscript: [entryStream crtab: anInteger]
]

{ #category : '*Concurrency' }
TextCollector >> space [
	^LibraryLocks holdingTranscript: [entryStream space]
]

{ #category : '*Concurrency' }
TextCollector >> tab [
	^LibraryLocks holdingTranscript: [entryStream tab]
]

{ #category : '*Concurrency' }
TextCollector >> print: anObject [
	"Append a description of anObject -- printed first, appended under the
	 lock, since printOn: is anObject's own code."
	^self nextPutAll: anObject printString
]

{ #category : '*Concurrency' }
TextCollector >> endEntry [
	"If the entry stream is not empty, hand its text to my dependents and
	 start a fresh one -- under the lock, so that the text handed on is
	 one worker's whole entry and the fresh stream is seen by everyone.

	 With no dependents the text goes to STANDARD ERROR instead of being
	 dropped.  A headless image has no Transcript window, so `self changed:
	 #appendEntry' reached nobody and every entry was built, locked, and
	 thrown away.  `st80 -serve' asked to evaluate a show: followed by a
	 literal answered the literal and put nothing on stdout, nothing on
	 stderr, and said nothing about it.

	 printNl and displayNl are the supported spelling and say so in
	 lib/Kernel-Protocol, but `Transcript show:' is what forty years of
	 Smalltalk writes and what doc/CONCURRENCY.md documents the locking of.
	 Failing without a word is the one behaviour it should not have.

	 Standard error, not standard output, because that is where printNl and
	 displayNl already go: two spellings of `say something' landing in two
	 different streams is worse than either choice.

	 Line ends are translated on the way out: Smalltalk separates lines with
	 a carriage return and a terminal wants a newline.  A separator at
	 either END of an entry is dropped and an entry that is nothing but
	 separators is not written at all, because the primitive supplies the
	 final newline itself and `Transcript show: ''a''; cr; show: ''b''; cr'
	 is FOUR entries -- `a', then a carriage return with `b' behind it,
	 then a lone carriage return.  Printed literally that is two lines and
	 two blank ones.  Trimmed, it is the two lines that were written."
	| entry |
	LibraryLocks holdingTranscript: [
		entryStream isEmpty ifFalse: [
			self dependents isEmpty
				ifTrue: [entry := entryStream contents]
				ifFalse: [self changed: #appendEntry].
			entryStream := WriteStream on: (String new: 200)].
		entry isNil ifFalse: [
			entry := entry collect: [:each |
				each = Character cr ifTrue: [Character nl] ifFalse: [each]].
			[entry notEmpty and: [entry last = Character nl]]
				whileTrue: [entry := entry copyFrom: 1 to: entry size - 1].
			[entry notEmpty and: [entry first = Character nl]]
				whileTrue: [entry := entry copyFrom: 2 to: entry size].
			entry isEmpty ifFalse: [self reportOnStandardError: entry]]]
]

{ #category : '*Concurrency' }
TextCollector >> clear [
	"Re-initialize the text to contain no characters."
	LibraryLocks holdingTranscript: [
		contents := Text new.
		entryStream := WriteStream on: (String new: 200)].
	self changed: #update
]

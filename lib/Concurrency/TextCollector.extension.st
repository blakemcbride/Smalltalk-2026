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
	 one worker's whole entry and the fresh stream is seen by everyone."
	LibraryLocks holdingTranscript: [
		entryStream isEmpty ifFalse: [
			self changed: #appendEntry.
			entryStream := WriteStream on: (String new: 200)]]
]

{ #category : '*Concurrency' }
TextCollector >> clear [
	"Re-initialize the text to contain no characters."
	LibraryLocks holdingTranscript: [
		contents := Text new.
		entryStream := WriteStream on: (String new: 200)].
	self changed: #update
]

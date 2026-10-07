"
Growing a WriteStream must not reach back into the collection it was given.

1983's pastEndPut: sends `collection grow', and Object>>grow mutates the
receiver in place -- the object table makes that one atomic swap, so it is
cheap and it is faithful to Chapter 30.  It also means anything else holding
that collection sees writes it never asked for, and sees them only past the
original end, which is as confusing a rule as one could invent.

Every dialect since grows by REPLACEMENT: the stream takes a bigger
collection and the original is left exactly as it was.  Ported code depends
on it, and depends on it silently.  DateAndTime class>>fuzzyReadFrom: reads
fractional seconds by writing digits over a nine-character buffer and then
asking the BUFFER for its value -- so a timestamp carrying picoseconds is
supposed to leave nine digits behind and let the extra three fall into a
copy nobody reads.  Growing in place kept all twelve, and 2006-08-28T00:00
:00.123456789000 parsed as two minutes and three seconds past midnight.

This is a divergence from 1983 and it is deliberate.  It cannot reach the
Blue Book image: that one is built from sources/ alone and never loads lib/.
"
Extension { #name : 'WriteStream' }

{ #category : '*Streams-Protocol' }
WriteStream >> pastEndPut: anObject [
	"Make room by taking a larger collection, leaving the old one alone.

	 Unless the collection is an OrderedCollection, which makes its own
	 room.  `OrderedCollection new: 40' is an EMPTY collection with space
	 for forty, not forty slots, and at:put: past its last element is
	 refused -- so a WriteStream on one could not take a single nextPut:,
	 and neither could anything built on `collection species new:', which
	 is every result-building method of a ReadStream over one: upToEnd,
	 next:, upTo:, nextLine (Bugs3 B46).  Growing by addLast: is what an
	 OrderedCollection is for, and every nextPut: at the end comes through
	 here, because writeLimit is kept equal to its size.  A nextPut: BEFORE
	 the end still goes through at:put:, overwriting, exactly as on an
	 Array.  isKindOf: rather than a respondsTo: because it is cheap where
	 it is asked -- once per growth for an Array, once per element for an
	 OrderedCollection, and a walk of one or two superclasses either way
	 -- and because a SortedCollection is the one subclass that must
	 answer yes here and then refuse: it refuses addLast:, and so it
	 should."
	| grown |
	(collection isKindOf: OrderedCollection) ifTrue: [
		collection addLast: anObject.
		position := position + 1.
		writeLimit := collection size.
		^anObject].
	grown := collection species new: (collection size max: 20) * 2.
	grown replaceFrom: 1 to: collection size with: collection startingAt: 1.
	collection := grown.
	writeLimit := collection size.
	^collection at: (position := position + 1) put: anObject
]

{ #category : '*Streams-Protocol' }
WriteStream >> setToEnd [
	"Set the position to the end of everything written or read so far.

	 1983's PositionableStream>>setToEnd sets the position to readLimit,
	 and a WriteStream raises readLimit only when it is asked for its
	 contents, its size, a reset or a position: -- so a stream that has
	 been written to and asked none of those answered setToEnd by going
	 back to where it was before the writes, and the next write began on
	 top of them.  `s nextPutAll: ''abc''; setToEnd; nextPutAll: ''def''.
	 s contents' answered 'def' (Bugs5 KERN-8).

	 This was first found, and first fixed, on ReadWriteStream alone.  The
	 changes stream of a bootstrap that is not writing an image is a
	 ReadWriteStream on a String, putSource:class:category:inFile: begins
	 with `file setToEnd', and so every method compiled in a -bootstrap
	 -tests or -eval run was written at the same place, each on top of the
	 last: `sourceCodeAt:' answered the LAST method compiled whatever
	 selector it was asked for.  But the fault is WriteStream's -- the
	 readLimit it forgets to raise is the one its own contents, size,
	 reset and position: raise -- and a plain WriteStream lost its
	 contents the same way.  So the fix lives here, where ReadWriteStream
	 inherits it and nothing has to remember a second copy.  A FileStream
	 keeps its end on disk and overrides this.

	 This is what contents does to readLimit, done where setToEnd needs
	 it."
	readLimit := readLimit max: position.
	position := readLimit
]

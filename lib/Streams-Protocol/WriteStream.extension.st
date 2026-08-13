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
	"Make room by taking a larger collection, leaving the old one alone."
	| grown |
	grown := collection species new: (collection size max: 20) * 2.
	grown replaceFrom: 1 to: collection size with: collection startingAt: 1.
	collection := grown.
	writeLimit := collection size.
	^collection at: (position := position + 1) put: anObject
]

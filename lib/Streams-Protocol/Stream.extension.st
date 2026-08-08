"
<< and streamContents:, the two pieces of stream protocol modern source
assumes.

<< is double dispatch, as Pharo does it: the stream sends putOn: to the
argument and the argument decides how it goes on.  The alternative -- a
chain of isString / isCharacter tests inside << -- puts the knowledge of
every printable class inside Stream, where a new class cannot reach it.
This way a class says how it writes itself and << needs no changes.

What << writes is the DISPLAY form, not the printed one: 'abc' goes on as
abc, without the quotes, because << is for building text a person will read.
print: is still there for the other job.
"
Extension { #name : 'Stream' }

{ #category : '*Streams-Protocol' }
Stream >> << anObject [
	anObject putOn: self.
	^self
]

{ #category : '*Streams-Protocol' }
Stream >> nl [
	"A line ending, which on this system is a line feed.

	 Character cr is 13 here because that is what it was on the Alto, and
	 it stays 13 -- the 1983 source means it.  nl is the separate name for
	 what a Unix file wants, so that the two are never confused."
	self nextPut: Character nl
]

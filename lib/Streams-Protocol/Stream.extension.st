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

{ #category : '*Streams-Protocol' }
Stream >> space: anInteger [
	"That many spaces, for any stream that can write one.

	 Squeak's and Pharo's name for a column of padding, and missing here:
	 the singular space is 1983's, on WriteStream, and every stream that
	 answers it answers this (Bugs3 B51)."
	anInteger timesRepeat: [self space]
]

{ #category : '*Streams-Protocol' }
Stream >> tab: anInteger [
	"That many tabs, as space: is that many spaces."
	anInteger timesRepeat: [self tab]
]

{ #category : '*Streams-Protocol' }
Stream >> display: anObject [
	"Write anObject's display form -- the text a person reads, without a
	 String's quotes or a Symbol's hash -- the way print: writes its
	 printed form.

	 The manual has shown `Transcript display: 42; cr' since its streams
	 chapter was written, and the image had print: and << but not this
	 name, so the example was a doesNotUnderstand (Bugs3 B64).  It is the
	 ANSI name and Pharo's, and it is what << does when the argument has
	 nothing better to say; it is spelled out here because a cascade wants
	 a keyword.  Not displayOn: -- that selector is the graphics one, and a
	 Form asked to displayOn: a stream would try to draw itself on it."
	self nextPutAll: anObject displayString
]

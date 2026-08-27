"
Look-ahead that puts the stream back.

A parser that asks 'does the next bit look like this?' has to be able to ask
without consuming, and 1983 gives it peek for one element and nothing for
several.  DateParser asks whether the pattern begins 'yyy' before deciding
how to read a year, which is three elements of look-ahead, and there is no
way to spell that with peek alone.
"
Extension { #name : 'PositionableStream' }

{ #category : '*Streams-Protocol' }
PositionableStream >> nextMatchAll: aCollection [
	"True if the next elements are aCollection, consuming them if so and
	 leaving the stream exactly where it was if not.

	 The position is restored on failure and NOT on success: a caller that
	 matched wants to carry on past what it matched, and one that did not
	 wants to try something else against the same input.  Getting that
	 backwards makes a parser that loops or one that silently skips."
	| start |
	start := self position.
	aCollection do: [:each |
		(self atEnd or: [self next ~= each]) ifTrue: [
			self position: start.
			^false]].
	^true
]

{ #category : '*Streams-Protocol' }
PositionableStream >> next: anInteger [
	"The next anInteger elements, or as many as are left.

	 1983 builds the answer at full size and fills it with self next, and
	 past the end self next is nil -- so asking a String stream for more
	 than it has does not answer a short String, it fails inside at:put:
	 with 'Strings only store Characters'.  The error names the symptom and
	 not one thing about the cause.

	 Answering short is what every caller actually wants, and what makes the
	 shortfall detectable: DateParser reads a fixed-width field and then
	 compares the size it got against the size it asked for, which is a test
	 that cannot be written against a reader that raises instead."
	| result |
	result := WriteStream on: (self contents species new: anInteger).
	anInteger timesRepeat: [
		self atEnd ifFalse: [result nextPut: self next]].
	^result contents
]

{ #category : '*Streams-Protocol' }
PositionableStream >> upToEnd [
	"Everything from here to the end, leaving me at the end."
	| result |
	result := WriteStream on: (self contents species new: 0).
	[self atEnd] whileFalse: [result nextPut: self next].
	^result contents
]

{ #category : '*Streams-Protocol' }
PositionableStream >> nextLine [
	"The next line, without its line ending, or nil when there is nothing
	 left.

	 A CR before an LF goes with it, so text written on any of the three
	 conventions reads back the same -- which is the same rule
	 lib/Network's SocketStream>>nextLine states, and the reason this is
	 not simply `self upTo: Character lf'.

	 Answers nil at the end rather than an empty String, so that
	 `[stream nextLine] whileNotNil:' terminates; an empty LINE still
	 answers an empty String, and those two have to be told apart."
	| line |
	self atEnd ifTrue: [^nil].
	line := self upTo: Character lf.
	^(line isEmpty not and: [line last = Character cr])
		ifTrue: [line copyFrom: 1 to: line size - 1]
		ifFalse: [line]
]

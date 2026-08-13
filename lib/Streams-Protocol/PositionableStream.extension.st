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

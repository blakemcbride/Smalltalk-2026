"
String protocol that forty years of use added and 1983 does not have.

Two notes.

substrings answers an OrderedCollection of Strings, and it never answers an
empty one for a run of separators -- 'a,,b' substrings: ',' is two pieces,
not three.  That is the behaviour every caller of a split actually wants,
and it is what Pharo does.

format: takes {1}-style positions rather than %1 or printf's %s, because a
position that names its argument survives being reordered by a translator
and a positional %s does not.
"
Extension { #name : 'String' }

{ #category : '*Strings-Protocol' }
String >> startsWith: aString [
	^self beginsWith: aString
]

{ #category : '*Strings-Protocol' }
String >> beginsWith: aString [
	aString size > self size ifTrue: [^false].
	aString size = 0 ifTrue: [^true].
	1 to: aString size do: [:i |
		(self at: i) = (aString at: i) ifFalse: [^false]].
	^true
]

{ #category : '*Strings-Protocol' }
String >> endsWith: aString [
	| offset |
	aString size > self size ifTrue: [^false].
	aString size = 0 ifTrue: [^true].
	offset := self size - aString size.
	1 to: aString size do: [:i |
		(self at: offset + i) = (aString at: i) ifFalse: [^false]].
	^true
]

{ #category : '*Strings-Protocol' }
String >> includesSubstring: aString [
	^(self indexOfSubstring: aString startingAt: 1) > 0
]

{ #category : '*Strings-Protocol' }
String >> indexOfSubstring: aString startingAt: start [
	"Where aString next appears at or after start, or 0.

	 The plain quadratic search.  Every caller here is looking through a
	 selector or a line of text, so the constant matters more than the
	 exponent and this has the smaller constant."
	| limit |
	aString size = 0 ifTrue: [^start].
	limit := self size - aString size + 1.
	start to: limit do: [:i |
		| matches |
		matches := true.
		1 to: aString size do: [:j |
			(self at: i + j - 1) = (aString at: j) ifFalse: [matches := false]].
		matches ifTrue: [^i]].
	^0
]

{ #category : '*Strings-Protocol' }
String >> substrings [
	^self substrings: ' '
]

{ #category : '*Strings-Protocol' }
String >> substrings: separators [
	"Split on any of the separator characters.

	 A run of separators is one break, not several, so no piece is ever
	 empty -- which is what a caller splitting a line into words means,
	 and what makes 'a,,b' substrings: ',' answer two pieces."
	| pieces current |
	pieces := OrderedCollection new.
	current := WriteStream on: (String new: 16).
	self do: [:ch |
		(separators includes: ch)
			ifTrue: [
				current contents isEmpty ifFalse: [
					pieces add: current contents.
					current := WriteStream on: (String new: 16)]]
			ifFalse: [current nextPut: ch]].
	current contents isEmpty ifFalse: [pieces add: current contents].
	^pieces
]

{ #category : '*Strings-Protocol' }
String >> splitOn: aCharacter [
	"Split on every occurrence, keeping empty pieces -- the other half of
	 what substrings: deliberately does not do.  'a,,b' splitOn: $, is
	 three pieces, and a caller reading a comma-separated record needs
	 that: the empty field is a field."
	| pieces current |
	pieces := OrderedCollection new.
	current := WriteStream on: (String new: 16).
	self do: [:ch |
		ch = aCharacter
			ifTrue: [
				pieces add: current contents.
				current := WriteStream on: (String new: 16)]
			ifFalse: [current nextPut: ch]].
	pieces add: current contents.
	^pieces
]

{ #category : '*Strings-Protocol' }
String >> join: aCollection [
	"The receiver is the separator: ', ' join: #('a' 'b') is 'a, b'."
	^String streamContents: [:stream |
		aCollection
			do: [:each | stream << each]
			separatedBy: [stream << self]]
]

{ #category : '*Strings-Protocol' }
String >> trimLeft [
	| first |
	first := 1.
	[first <= self size and: [(self at: first) isSeparator]]
		whileTrue: [first := first + 1].
	^self copyFrom: first to: self size
]

{ #category : '*Strings-Protocol' }
String >> trimRight [
	| last |
	last := self size.
	[last >= 1 and: [(self at: last) isSeparator]]
		whileTrue: [last := last - 1].
	^self copyFrom: 1 to: last
]

{ #category : '*Strings-Protocol' }
String >> trimBoth [
	^self trimLeft trimRight
]

{ #category : '*Strings-Protocol' }
String >> trimmed [
	^self trimBoth
]

{ #category : '*Strings-Protocol' }
String >> format: aCollection [
	"Substitute {1}, {2} ... with the collection's elements.

	 Positions rather than printf's %s because a position survives being
	 reordered -- which is the whole reason a message ever gets formatted
	 rather than concatenated."
	| out i |
	out := WriteStream on: (String new: self size + 16).
	i := 1.
	[i <= self size] whileTrue: [
		| ch |
		ch := self at: i.
		(ch = ${ and: [i < self size])
			ifTrue: [
				| close digits |
				close := self indexOfSubstring: '}' startingAt: i.
				digits := close > 0
					ifTrue: [self copyFrom: i + 1 to: close - 1]
					ifFalse: [''].
				(close > 0 and: [digits notEmpty
					and: [digits allSatisfy: [:d | d isDigit]]])
					ifTrue: [
						out nextPutAll:
							(aCollection at: digits asNumber) displayString.
						i := close]
					ifFalse: [out nextPut: ch]]
			ifFalse: [out nextPut: ch].
		i := i + 1].
	^out contents
]

{ #category : '*Strings-Protocol' }
String >> asInteger [
	"The leading integer, or nil when there is not one.

	 nil rather than 0, because '0' and 'banana' are different answers and
	 a caller that cannot tell them apart has a bug waiting."
	| stream negative digits value |
	stream := ReadStream on: self trimBoth.
	negative := false.
	stream atEnd ifTrue: [^nil].
	(stream peek = $-) ifTrue: [negative := true. stream next].
	digits := 0.
	value := 0.
	[stream atEnd not and: [stream peek isDigit]] whileTrue: [
		value := value * 10 + (stream next asInteger - $0 asInteger).
		digits := digits + 1].
	digits = 0 ifTrue: [^nil].
	^negative ifTrue: [value negated] ifFalse: [value]
]

{ #category : '*Strings-Protocol' }
String >> lines [
	^self splitOn: Character nl
]

{ #category : '*Strings-Protocol' }
String >> beginsWith: aPrefix caseSensitive: aBoolean [
	"Whether the receiver starts with aPrefix, optionally ignoring case.

	 Pharo's Date class>>indexOfMonth: asks this of a month name, so
	 anything that parses a date reaches it.  An empty prefix matches
	 everything, which is Pharo's documented answer and the one the callers
	 rely on."
	| n |
	n := aPrefix size.
	n = 0 ifTrue: [^true].
	n > self size ifTrue: [^false].
	1 to: n do: [:i |
		| a b |
		a := self at: i.
		b := aPrefix at: i.
		aBoolean
			ifTrue: [a = b ifFalse: [^false]]
			ifFalse: [a asUppercase = b asUppercase ifFalse: [^false]]].
	^true
]

{ #category : '*Strings-Protocol' }
String >> padLeftTo: width with: aCharacter [
	"Me, widened on the left with that character until I am at least that
	 wide.  Already wide enough answers a copy of me, never a truncation --
	 a pad that could shorten its argument would silently corrupt the very
	 fields it is used to line up."
	| pad |
	pad := width - self size.
	pad <= 0 ifTrue: [^self copy].
	^(String new: pad withAll: aCharacter), self
]

{ #category : '*Strings-Protocol' }
String >> padLeftTo: width [
	^self padLeftTo: width with: $ 
]

{ #category : '*Strings-Protocol' }
String >> padRightTo: width with: aCharacter [
	| pad |
	pad := width - self size.
	pad <= 0 ifTrue: [^self copy].
	^self, (String new: pad withAll: aCharacter)
]

{ #category : '*Strings-Protocol' }
String >> padRightTo: width [
	^self padRightTo: width with: $ 
]

{ #category : '*Strings-Protocol' }
String >> asPluralBasedOn: aNumber [
	"Me, with an s if that count is not one.

	 Duration>>humanReadablePrintString builds '2 hours 1 minute' from
	 singular nouns, so the noun must not know the count in advance."
	^aNumber = 1 ifTrue: [self copy] ifFalse: [self, 's']
]

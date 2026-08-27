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
	"An explicit plus is a sign too, and skipping it is not cosmetic:
	 Duration's ANSI format writes '+0:01:02:55', and a reader that answers
	 nil for the plus form makes every signed duration unparseable while
	 the negative one works."
	(stream peek = $+) ifTrue: [stream next].
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
	 rely on.

	 The type is checked before that, and the order matters.  Object>>size
	 answers 0 for anything not indexable, so `'January' beginsWith: 1'
	 reached the empty-prefix shortcut and answered TRUE -- which made
	 Month class>>indexOfMonth: answer 1 for the integer 1 instead of
	 refusing it, and the test that asks it to refuse saw nothing raised.
	 An argument of the wrong type is a fault to report, not an empty
	 sequence to agree with."
	| n |
	(aPrefix isKindOf: String)
		ifFalse: [^self error: 'a prefix must be a String, not a ',
			aPrefix class name].
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

{ #category : '*Strings-Protocol' }
String >> trimBoth: aBlock [
	"Me without leading or trailing characters the block accepts."
	| first last |
	first := 1.
	last := self size.
	[first <= last and: [aBlock value: (self at: first)]]
		whileTrue: [first := first + 1].
	[last >= first and: [aBlock value: (self at: last)]]
		whileTrue: [last := last - 1].
	^self copyFrom: first to: last
]

{ #category : '*Strings-Protocol' }
String >> findBetweenSubstrings: delimiters [
	"My substrings, split on any of those delimiters, empty pieces dropped
	 -- which is what distinguishes it from splitOn:.

	 A delimiter may be a Character or a String, and the callers mix them:
	 Pharo's collection printing test splits on `{ ' '. '('. ')' }', three
	 one-character STRINGS.  Testing `delimiters includes: aCharacter'
	 against those never matches, so the whole input came back as a single
	 piece -- which is what `expected 6 but got 1' was."
	| pieces current marks |
	marks := Set new.
	delimiters do: [:each |
		(each isKindOf: Character)
			ifTrue: [marks add: each]
			ifFalse: [each do: [:c | marks add: c]]].
	pieces := OrderedCollection new.
	current := WriteStream on: (String new: 16).
	self do: [:c |
		(marks includes: c)
			ifTrue: [
				current contents isEmpty ifFalse: [pieces add: current contents].
				current := WriteStream on: (String new: 16)]
			ifFalse: [current nextPut: c]].
	current contents isEmpty ifFalse: [pieces add: current contents].
	^pieces
]

{ #category : '*Strings-Protocol' }
String >> article [
	"`a' or `an', by whether I begin with a vowel.

	 Wrong for `an hour' and `a unicorn', and right often enough that every
	 Smalltalk prints `an OrderedCollection' this way."
	| first |
	self isEmpty ifTrue: [^'a'].
	first := (self at: 1) asUppercase.
	^('AEIOU' includes: first) ifTrue: ['an'] ifFalse: ['a']
]

{ #category : '*Strings-Protocol' }
String >> tokenize: aSeparator [
	"My pieces between runs of the separator, with no empty piece among
	 them.

	 The name is Squeak's and Pharo's, and the receiver is what ported code
	 sends it to.  aSeparator may be a Character or a String of characters,
	 either of which is what substrings: takes -- so this is substrings:
	 under the name the other dialects use, and the only reason it exists
	 is that the other name is the one ported source writes.

	 Pharo's version is a regular-expression split, and this is not: there
	 is no regular-expression engine here, and pretending otherwise by
	 accepting the syntax and treating it as literal characters would be a
	 wrong answer rather than a doesNotUnderstand.  A separator of '\\s+'
	 splits on backslash and s and plus, which is exactly what it says it
	 does."
	^self substrings: (aSeparator isCharacter
		ifTrue: [String with: aSeparator]
		ifFalse: [aSeparator])
]

{ #category : '*Strings-Protocol' }
String >> trimSeparators [
	"trimBoth under the name Pharo and Squeak give it, which is the name
	 forty years of ported code writes."
	^self trimBoth
]

{ #category : '*Strings-Protocol' }
String >> asNumber [
	"The number this text spells, or NIL when it does not spell one.

	 1983 answers `Number readFromString: self', and that reader answers 0
	 for anything it cannot read -- so `'abc' asNumber' was 0 and could not
	 be told from a parse of `'0''.  A parse failure that answers a number
	 is the worst possible answer, because every caller downstream treats
	 it as data.  String>>asInteger in this same file already answers nil
	 for the same reason, in the same words; the two disagreeing about the
	 same text was the odd part.

	 A leading number followed by other text still parses -- `'12abc'
	 asNumber' is 12, as it is in Pharo -- because that is the reader's
	 documented behaviour and callers rely on it.  Only text with no
	 number at the front at all answers nil."
	| stream |
	stream := ReadStream on: self trimLeft.
	(stream atEnd not and: [stream peek = $- or: [stream peek = $+]])
		ifTrue: [stream next].
	(stream atEnd not and: [stream peek isDigit]) ifFalse: [^nil].
	^Number readFromString: self trimLeft
]

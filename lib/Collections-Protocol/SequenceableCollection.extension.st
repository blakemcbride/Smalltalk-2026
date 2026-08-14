"
Order-dependent protocol.  reverse already exists here and answers a copy;
reversed is Pharo's name for the same thing, and both are common enough in
ported source that having only one of them is a papercut.
"
Extension { #name : 'SequenceableCollection' }

{ #category : '*Collections-Protocol' }
SequenceableCollection >> reversed [
	^self reverse
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButFirst [
	^self copyFrom: 2 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButLast [
	^self copyFrom: 1 to: self size - 1
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButFirst: n [
	^self copyFrom: n + 1 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButLast: n [
	^self copyFrom: 1 to: self size - n
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> first: n [
	^self copyFrom: 1 to: n
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> last: n [
	^self copyFrom: self size - n + 1 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> with: aCollection collect: aBlock [
	"Element by element, in step.  Signals if the two are different
	 lengths, because quietly stopping at the shorter one is how a
	 mismatch turns into a wrong answer instead of an error."
	| result |
	aCollection size = self size
		ifFalse: [^self error: 'collections are not the same size'].
	result := self species new: self size.
	1 to: self size do: [:i |
		result at: i put: (aBlock value: (self at: i)
		                          value: (aCollection at: i))].
	^result
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> doWithIndex: aBlock [
	1 to: self size do: [:i | aBlock value: (self at: i) value: i]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> withIndexDo: aBlock [
	1 to: self size do: [:i | aBlock value: (self at: i) value: i]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> keysAndValuesDo: aBlock [
	"The index first, the element second -- the opposite order from
	 withIndexDo:, which is Pharo's convention and not a slip."
	1 to: self size do: [:i | aBlock value: i value: (self at: i)]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> sorted: aBlock [
	"Sorted, and the SAME KIND of collection -- 'hello' sorted is 'ehllo'
	 and not an Array of five Characters.

	 Collection>>sorted answers an Array because a sorted Set is not a
	 Set and there is nothing better it could answer.  Here there is:
	 order is what a sequenceable collection is for, so sorting one has
	 an obvious answer and the species is it.  Pharo's own doctest for
	 this method is what said so."
	| ordered result |
	ordered := (self asSortedCollection: aBlock) asArray.
	result := self species new: ordered size.
	1 to: ordered size do: [:i | result at: i put: (ordered at: i)].
	^result
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> sorted [
	^self sorted: [:a :b | a <= b]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> readStream [
	"A stream reading me from the beginning.

	 1983 has ReadStream, WriteStream and ReadWriteStream and every one of
	 their `on:' constructors; what it never grew is the habit of asking the
	 COLLECTION for one.  Ported source asks constantly -- `aString
	 readStream' is how Pharo spells the start of nearly every parse -- so
	 the gap shows up not as one missing method but as a hundred failures
	 in whatever was being parsed.  It was 166 of the 354 errors in the
	 Chronology suites."
	^ReadStream on: self
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> writeStream [
	"A stream writing INTO me, from the start.

	 Into me, not into a fresh empty collection of my species -- that was
	 the first version and it is subtly, expensively wrong.  The idiom it
	 breaks is everywhere in ported code: take a buffer, write over it, then
	 read the BUFFER back.  DateAndTime class>>fuzzyReadFrom: does exactly
	 that four times over, once per field, and with a writeStream that
	 wrote somewhere else every read came back as the untouched template --
	 so every fractional second and every timezone offset in the suite
	 silently parsed as zero.  Ten tests, and not one of them failed in a
	 way that pointed here."
	^WriteStream on: self
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> readWriteStream [
	"A stream over me that can do both, positioned at the start."
	^ReadWriteStream on: self
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> second [
	"The second element.  1983 names only first and last; ported source
	 counts further along, and reads much better for it than at: 2 does."
	^self at: 2
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> third [
	^self at: 3
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> fourth [
	^self at: 4
]

{ #category : '*Collections-Protocol' }
SequenceableCollection class >> new: sizeHint streamContents: aBlock [
	"Answer what aBlock writes into a stream on a new instance of me.

	 The size is a hint and nothing more -- a WriteStream grows -- so it is
	 not an error to write more or less than it says.  Ported code passes
	 its best guess to save the growing, and passing a wrong guess must
	 still produce the right answer."
	| stream |
	stream := WriteStream on: (self new: sizeHint).
	aBlock value: stream.
	^stream contents
]

{ #category : '*Collections-Protocol' }
SequenceableCollection class >> streamContents: aBlock [
	^self new: 16 streamContents: aBlock
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> from: start to: stop do: aBlock [
	"Evaluate aBlock with each of my elements between those indices."
	start to: stop do: [:i | aBlock value: (self at: i)]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> from: start to: stop doWithIndex: aBlock [
	start to: stop do: [:i | aBlock value: (self at: i) value: i]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> at: index ifAbsent: exceptionBlock [
	"The element at that index, or the block's value if there is no such
	 index.

	 1983 answers this on Dictionary and not on indexed collections, where
	 an out-of-range subscript is always an error.  Ported code uses it as
	 the bounds-checked read -- Pharo's HashTableSizes looks up a cached
	 size this way, and reached it 6346 times in one bootstrap -- so the
	 index is checked here rather than trapped."
	(index isInteger and: [index >= 1 and: [index <= self size]])
		ifFalse: [^exceptionBlock value].
	^self at: index
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> reduceLeft: aBlock [
	"Fold left, with no initial value: the first element is the seed.

	 aBlock may take more than two arguments, which is not a curiosity --
	 it is how `#(1 + 3 - 5) reduceLeft: [:a :op :b | a perform: op with: b]'
	 evaluates an infix expression, and Pharo's own tests use exactly that."
	| count taken arity |
	self isEmpty ifTrue: [^self errorEmptyCollection].
	"
	 A Symbol counts its arguments differently from a block, and the
	 difference is exactly one.  `#+ numArgs' is 1 -- the arity of the
	 SELECTOR -- while the equivalent block [:a :b | a + b] answers 2,
	 because a Symbol used as a block supplies the receiver as well.
	 Reading them the same way made this take zero elements per step, build
	 a one-element argument array, and send #+ with no arguments at all:
	 `0 arguments sent to #+, which expects 1'.
	"
	arity := (aBlock isKindOf: Symbol)
		ifTrue: [aBlock numArgs + 1]
		ifFalse: [aBlock numArgs].
	count := arity - 1.
	taken := self first.
	2 to: self size by: count do: [:i |
		| arguments |
		arguments := (i to: (i + count - 1 min: self size))
			inject: OrderedCollection new
			into: [:acc :k | acc add: (self at: k); yourself].
		arguments addFirst: taken.
		taken := aBlock valueWithArguments: arguments asArray].
	^taken
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> reduceRight: aBlock [
	"Fold right: the LAST element is the seed and the walk runs backwards,
	 so `#(1 2 3) reduceRight: [:a :b | a - b]' is 1 - (2 - 3) and not
	 ((1 - 2) - 3)."
	| taken |
	self isEmpty ifTrue: [^self errorEmptyCollection].
	taken := self last.
	self size - 1 to: 1 by: -1 do: [:i |
		taken := aBlock value: (self at: i) value: taken].
	^taken
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> splitOn: splitter [
	"Me, cut into pieces wherever splitter matches, as an OrderedCollection.

	 The splitter is an element, a subsequence, or a block that tests one
	 element.  Pharo dispatches this through a `split:' protocol on the
	 splitter itself; done here directly, because the three cases are the
	 whole of it and a protocol on Object to reach them is more machinery
	 than the answer is worth.

	 Empty pieces are KEPT -- two adjacent separators produce an empty
	 piece between them, and the tests say so: `#(2 2 3 3 4 1 2 3 3 5)
	 splitOn: [:e | e > 3]' answers three pieces, the last of them empty."
	| pieces current width matches |
	pieces := OrderedCollection new.
	current := OrderedCollection new.
	width := (splitter isKindOf: SequenceableCollection)
		ifTrue: [splitter size]
		ifFalse: [1].
	matches := [:index |
		(splitter isKindOf: BlockClosure)
			ifTrue: [splitter value: (self at: index)]
			ifFalse: [(splitter isKindOf: SequenceableCollection)
				ifTrue: [(index + width - 1 <= self size)
					and: [(self copyFrom: index to: index + width - 1) = splitter]]
				ifFalse: [(self at: index) = splitter]]].
	self doSplit: matches width: width into: pieces current: current.
	^pieces
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> doSplit: matches width: width into: pieces current: current [
	"The walk behind splitOn:, kept separate so the dispatch above reads."
	| index |
	index := 1.
	[index <= self size] whileTrue: [
		(matches value: index)
			ifTrue: [
				pieces add: current asArray.
				current := OrderedCollection new.
				index := index + width]
			ifFalse: [
				current add: (self at: index).
				index := index + 1]].
	pieces add: current asArray
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> joinUsing: joiner [
	"My elements run together with joiner between them.

	 The pieces are themselves collections and are spliced in; a joiner
	 that is a single element is treated as a collection of one."
	| result separator |
	separator := (joiner isKindOf: SequenceableCollection)
		ifTrue: [joiner]
		ifFalse: [Array with: joiner].
	result := OrderedCollection new.
	self doWithIndex: [:each :i |
		i > 1 ifTrue: [result addAll: separator].
		(each isKindOf: SequenceableCollection)
			ifTrue: [result addAll: each]
			ifFalse: [result add: each]].
	^(joiner isKindOf: SequenceableCollection)
		ifTrue: [joiner species withAll: result]
		ifFalse: [Array withAll: result]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> sort: aBlock [
	"Sort me IN PLACE by that block, and answer me.

	 In place, where sorted: answers a copy -- the two exist side by side in
	 Pharo and the difference is the whole reason both are there."
	| ordered |
	ordered := (self asSortedCollection: aBlock) asArray.
	1 to: ordered size do: [:i | self at: i put: (ordered at: i)].
	^self
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> sort [
	^self sort: [:a :b | a <= b]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> associationsDo: aBlock [
	"Evaluate aBlock with each of my elements, which are Associations.

	 A Dictionary answers this from its own storage; a plain sequence of
	 Associations -- which is what `aDictionary associations' hands back --
	 has to answer it too, or code that walks either one breaks on the
	 second."
	self do: aBlock
]

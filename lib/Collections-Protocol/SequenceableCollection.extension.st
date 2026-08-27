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
	"
	 A fold with an n-argument block eats n-1 elements a step after the seed,
	 so a collection fits only when its size is 1 + k(n-1).  When it does not,
	 that is an ERROR and not a short final group: a block called with fewer
	 arguments than it declares fails inside the VM, where no handler can
	 reach it, and `#(1 2) reduce: [:a :b :c | ...]' is exactly the case
	 Pharo's tests require to raise Error.  Clamping the last group to what
	 was left is what produced the uncatchable failure."
	count < 1 ifTrue: [^self error: 'reduce needs a block of two arguments or more'].
	(self size - 1 \\ count) = 0 ifFalse: [
		^self error: 'a block of ', arity printString,
			' arguments folds 1 plus a multiple of ', count printString,
			' elements, and there are ', self size printString].
	taken := self first.
	2 to: self size by: count do: [:i |
		| arguments |
		arguments := (i to: i + count - 1)
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
	 ((1 - 2) - 3).

	 Blocks of more than two arguments are taken in groups, the same way
	 reduceLeft: takes them and for the same reason -- an infix expression
	 folded from the right."
	| taken count arity index |
	self isEmpty ifTrue: [^self errorEmptyCollection].
	arity := (aBlock isKindOf: Symbol)
		ifTrue: [aBlock numArgs + 1]
		ifFalse: [aBlock numArgs].
	count := arity - 1.
	"
	 A fold with an n-argument block eats n-1 elements a step after the seed,
	 so a collection fits only when its size is 1 + k(n-1).  When it does not,
	 that is an ERROR and not a short final group: a block called with fewer
	 arguments than it declares fails inside the VM, where no handler can
	 reach it, and `#(1 2) reduce: [:a :b :c | ...]' is exactly the case
	 Pharo's tests require to raise Error.  Clamping the last group to what
	 was left is what produced the uncatchable failure."
	count < 1 ifTrue: [^self error: 'reduce needs a block of two arguments or more'].
	(self size - 1 \\ count) = 0 ifFalse: [
		^self error: 'a block of ', arity printString,
			' arguments folds 1 plus a multiple of ', count printString,
			' elements, and there are ', self size printString].
	taken := self last.
	index := self size - count.
	[index >= 1] whileTrue: [
		| arguments |
		arguments := OrderedCollection new.
		index to: index + count - 1 do: [:k | arguments add: (self at: k)].
		arguments add: taken.
		taken := aBlock valueWithArguments: arguments asArray.
		index := index - count].
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

	 Each piece is of MY species, not an Array: splitting an
	 OrderedCollection gives OrderedCollections, splitting a String gives
	 Strings, and a caller that goes on to send `,' or `asUppercase' to a
	 piece needs that.  An Interval's species is Array, which is the right
	 answer for the one receiver that cannot hold arbitrary elements.

	 Empty pieces are KEPT -- two adjacent separators produce an empty
	 piece between them, and the tests say so: `'the banana man' splitOn:
	 'an'' answers `'the b' '' 'a m'', the empty one being what sits
	 between the two `an's of `banana'."
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
				ifTrue: ["An empty splitter matches nowhere.  It cannot match
					  everywhere: a zero-width match advances the walk by
					  zero, and `'' split: 'abc'' would not terminate."
					 width > 0
						and: [(index + width - 1 <= self size)
						and: [(self copyFrom: index to: index + width - 1) = splitter]]]
				ifFalse: [(self at: index) = splitter]]].
	self doSplit: matches width: width into: pieces current: current.
	^pieces
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> doSplit: matches width: width into: pieces current: current [
	"The walk behind splitOn:, kept separate so the dispatch above reads."
	| index piece |
	index := 1.
	piece := current.
	[index <= self size] whileTrue: [
		(matches value: index)
			ifTrue: [
				pieces add: (self species withAll: piece).
				piece := OrderedCollection new.
				index := index + width]
			ifFalse: [
				piece add: (self at: index).
				index := index + 1]].
	pieces add: (self species withAll: piece)
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
	^((joiner isKindOf: String) or: [joiner isKindOf: Character])
		ifTrue: [
			"
			 Into a String, so every piece has to BE characters.  An
			 element that is not a String contributes its printString --
			 `#(1 2) joinUsing: ', '' is '1, 2' and not an error about
			 Strings storing only Characters, which is what building the
			 answer out of the raw elements produced.
			"
			| text |
			text := WriteStream on: (String new: 32).
			result do: [:each |
				(each isKindOf: Character)
					ifTrue: [text nextPut: each]
					ifFalse: [text nextPutAll: each printString]].
			text contents]
		ifFalse: [
			(joiner isKindOf: SequenceableCollection)
				ifTrue: [
					"
					 A SortedCollection joiner answers an OrderedCollection: the
					 whole point of a join is the order the pieces were in, and a
					 SortedCollection would throw it away -- `(1 to: 4) joinUsing:
					 #(8 9) asSortedCollection' is 1 8 9 2 8 9 3 8 9 4, and not
					 1 2 3 4 8 8 8 9 9 9.
					"
					(joiner isKindOf: SortedCollection)
						ifTrue: [OrderedCollection withAll: result]
						ifFalse: [joiner species withAll: result]]
				ifFalse: [Array withAll: result]]
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

{ #category : '*Collections-Protocol' }
SequenceableCollection >> isSorted [
	^self isSortedBy: [:a :b | a <= b]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> isSortedBy: aBlock [
	"Whether my elements are already in that order.

	 Answered by walking adjacent pairs, so an empty collection and a
	 one-element one are sorted -- which is the convention, and the answer
	 that makes a sort check idempotent."
	2 to: self size do: [:i |
		(aBlock value: (self at: i - 1) value: (self at: i))
			ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> joinUsing: joiner last: last [
	"My elements run together, with joiner between them and `last' before
	 the final one -- `#(1 2 3 4) joinUsing: ', ' last: ' and '' is
	 '1, 2, 3 and 4'.  The shape English wants and comma-joining does not."
	| pieces |
	self size < 2 ifTrue: [^self joinUsing: joiner].
	pieces := self copyFrom: 1 to: self size - 1.
	^(pieces joinUsing: joiner), last, (self last isString
		ifTrue: [self last]
		ifFalse: [self last printString])
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> identityIndexOf: anObject [
	^self identityIndexOf: anObject ifAbsent: [0]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> identityIndexOf: anObject ifAbsent: aBlock [
	"Where that object sits, by identity rather than by =.

	 The distinction is the whole point for a SmallIdentityDictionary: two
	 equal Associations are one element to indexOf: and two to this."
	1 to: self size do: [:i |
		(self at: i) == anObject ifTrue: [^i]].
	^aBlock value
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> hash [
	"A hash over every element, because = compares every element.

	 SequenceableCollection>>= is structural -- same species, same size,
	 element by element -- and 1983 defines hash on Array, String, Symbol
	 and Interval alone.  So OrderedCollection, SortedCollection, ByteArray
	 and every user subclass had value equality and Object's IDENTITY hash,
	 which is the one pair a hashed collection cannot hold:

		(OrderedCollection with: 1) = (OrderedCollection with: 1)   true
		(OrderedCollection with: 1) hash = ... hash                 false

	 and the symptom is worse than wrong, it is nondeterministic.  A Set
	 given 300 distinct one-element OrderedCollections twice over ended up
	 with 599 elements rather than 600: one pair happened to probe into its
	 own twin before it reached an empty slot, and WHICH pair that is
	 depends on where the collector put the objects.  The same program
	 answers 599 today and 600 tomorrow.  A Dictionary keyed by one could
	 not find a key it held.  `'ab' asByteArray' is what DbRecord>>bytesAt:
	 and the crypto library hand back, so this reached real data.

	 The function is String>>hash's, FNV-1a, over the elements' hashes
	 rather than over characters -- see lib/Collections-Protocol's String
	 extension for why the arithmetic is written the way it is, why the
	 fold at the end matters for a table that buckets by `\\ basicSize',
	 and why it never leaves SmallInteger.  Each element's hash is masked
	 to 32 bits first because a hash may be negative (`-5 hash' is -5) and
	 the mixing has to stay non-negative.

	 The species is deliberately NOT folded in.  An Array and an
	 OrderedCollection holding the same elements are not equal and would
	 be allowed to hash differently, but a class's hash here is derived
	 from its pointer, and a hash that moves when the collector moves a
	 class is not a hash a Set can be built on.  They collide; collisions
	 are cheap and wrong answers are not."
	| h |
	h := 2166136261.
	self do: [:each |
		h := ((h bitXor: (each hash bitAnd: 16rFFFFFFFF)) * 16777619)
				bitAnd: 16rFFFFFFFF].
	^(h bitXor: (h bitShift: -16)) bitAnd: 16rFFFFFFF
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> replaceAll: oldObject with: newObject [
	"Replace every element equal to oldObject with newObject, IN PLACE, and
	 answer me.

	 In place and answering the receiver, which is Pharo's contract and not
	 the obvious one: the copying version is copyReplaceAll:with:, which
	 1983 already has and which takes SUBSEQUENCES rather than elements.
	 The two names differ by four characters and mean different things in
	 both dimensions, so it is worth saying which is which:

		'hello' replaceAll: $l with: $L         'heLLo', changed in place
		'hello' copyReplaceAll: 'll' with: 'LL' 'heLLo', a copy"
	1 to: self size do: [:i |
		(self at: i) = oldObject ifTrue: [self at: i put: newObject]].
	^self
]

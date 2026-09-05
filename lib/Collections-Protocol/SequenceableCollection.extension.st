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
	"All but my first n elements; nothing when n is my size or more,
	 since there is nothing after the first n of three but five."
	self checkCount: n.
	^self copyFrom: n + 1 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> allButLast: n [
	self checkCount: n.
	^self copyFrom: 1 to: self size - n
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> first: n [
	"My first n elements.  More than I have is a subscript error, as it
	 is in Pharo; fewer than none is refused by name."
	self checkCount: n.
	^self copyFrom: 1 to: n
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> last: n [
	self checkCount: n.
	^self copyFrom: self size - n + 1 to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> checkCount: n [
	"Refuse a negative count, in words.

	 `first: -1' asked copyFrom:to: for a copy of size -1, whose `species
	 new: -1' failed its primitive, and the error said `a primitive has
	 failed' -- true, and no help (Bugs3 B51).  A count is a number of
	 elements, and there is no such thing as minus one of them."
	n < 0 ifTrue: [^self error: 'a count of elements cannot be negative: ' , n printString]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> with: aCollection collect: aBlock [
	"Element by element, in step.  Signals if the two are different
	 lengths, because quietly stopping at the shorter one is how a
	 mismatch turns into a wrong answer instead of an error.

	 The answers are gathered in an Array and handed to
	 speciesFilledWith:, which knows which kinds can be filled by index
	 and which have to be filled with add:.  Filling `self species new:
	 self size' directly is what raised `attempt to index non-existent
	 element' for every OrderedCollection receiver -- the same fault as
	 sorted: and fixed in the same place (Bugs4 DOCS-1)."
	| answers |
	aCollection size = self size
		ifFalse: [^self error: 'collections are not the same size'].
	answers := Array new: self size.
	1 to: self size do: [:i |
		answers at: i put: (aBlock value: (self at: i)
		                           value: (aCollection at: i))].
	^self speciesFilledWith: answers
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
	 this method is what said so, and the manual repeats it: chapter 13
	 and appendix C both promise `the same kind of collection'.

	 It was promised and not delivered for the growable kinds.  This built
	 `self species new: ordered size' and filled it with at:put:, which is
	 right for an Array and for a String and wrong for everything else:
	 `OrderedCollection new: 3' is an EMPTY collection with room for
	 three, so the first at:put: was out of range and `(OrderedCollection
	 withAll: #(3 1 2)) sorted' answered nothing but `attempt to index
	 non-existent element in an ordered collection' -- on the most-used
	 growable collection there is (Bugs4 DOCS-1).  A SortedCollection
	 raised `you must use add:' and a LinkedList failed its primitive in
	 `new: 0'.  speciesFilledWith: is where the three kinds are told
	 apart."
	^self speciesFilledWith: (self asSortedCollection: aBlock) asArray
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> speciesFilledWith: aSequence [
	"A collection of my species holding those elements, in that order.

	 `self species new: n' followed by at:put: is the 1983 idiom and it
	 only works for a kind whose `new: n' answers a collection that is
	 ALREADY n elements long -- an Array, a String, a ByteArray.  For
	 OrderedCollection and SortedCollection `new: n' is a capacity hint
	 and the answer is empty; for LinkedList, which is not indexable at
	 all, `new: n' fails its primitive.  sorted: and with:collect: are the
	 two methods here that had that shape and no override to rescue them,
	 and each failed for all three kinds (Bugs4 DOCS-1); copyFrom:to: and
	 reverse have the shape too and OrderedCollection overrides both, so
	 they answered all along.

	 So the species is asked rather than assumed: a fixed-size class is
	 not sent `new:' at all, and a variable one is sent it and then asked
	 how long the answer came out.  A collection that came out the right
	 length is filled by index, which preserves the order exactly and is
	 what an arrayed kind wants; anything else is filled with add:, which
	 is the only protocol a growable kind has and which appends in order
	 for an OrderedCollection.

	 A SortedCollection re-sorts what it is given by its OWN sort block,
	 which is right for it -- a SortedCollection is sorted, that is what
	 it is -- but wrong for `sorted: aBlock', which asks for a different
	 order.  SortedCollection>>sorted: therefore does not come through
	 here; see there."
	| species result |
	species := self species.
	species isVariable ifTrue: [
		result := species new: aSequence size.
		result size = aSequence size ifTrue: [
			1 to: aSequence size do: [:i | result at: i put: (aSequence at: i)].
			^result]].
	result := species new.
	aSequence do: [:each | result add: each].
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
	 Pharo and the difference is the whole reason both are there.

	 A merge sort, and STABLE for it: elements the block ranks equal stay
	 in the order they were in.  This went through asSortedCollection:,
	 whose large-addition path is 1983's quicksort, so sorting pairs by
	 their first element shuffled the second elements -- and did so
	 differently for every size, which is the kind of fault that passes
	 every small test (Bugs3 B51).  Stability holds when the block answers
	 true for equal elements, as `<=' does and the default does; a strict
	 `<' has said that equal elements may go either way, and they do.
	 SortedCollection>>reSort sorts through here for the same reason."
	| size |
	size := self size.
	size <= 1 ifTrue: [^self].
	self mergeSortFrom: 1 to: size by: aBlock into: (Array new: size).
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
	 are cheap and wrong answers are not.

	 THIRTY-TWO ELEMENTS, THEN A SAMPLE OF SIXTEEN (Bugs3 B33).  As first
	 written this walked every element, which is the right function for
	 the keys a program actually makes -- rows, coordinates, key paths,
	 digests, all of them short -- and the wrong cost for an Interval,
	 which is equal to the Array of its elements and so must hash as one:
	 `(1 to: 1000000) hash' walked a million elements and a Set of twenty
	 thousand Intervals did not finish in two minutes.  So a collection of
	 up to thirty-two elements hashes every one of them, exactly as
	 before, and a longer one hashes its size and sixteen elements spaced
	 evenly from its first to its last.  Two long collections that differ
	 only at a position the sample misses collide, and a Dictionary keyed
	 by such things is slow rather than wrong; a thirty-two-byte digest,
	 a row of thirty columns and every literal array anybody writes are
	 still hashed whole.  Sixteen rather than thirty-two because the
	 sample is the whole cost of an Interval's hash, and a Set of twenty
	 thousand Intervals has to fill in a fraction of a second; a longer
	 sample bought nothing measurable for the arrays that reach it.

	 The Bugs2 fix that made this walk every element was for the shape
	 (0, i, 0) -- first and last alone cannot see the middle -- and the
	 sample keeps that: a three-element Array hashes all three.

	 The sample positions are 1 + (k * (size - 1) // 15) for k from 0 to
	 15: the first element, the last, and fourteen between.  Interval>>hash
	 computes the same elements arithmetically instead of sending at:,
	 which costs it a bounds check and a size per element, and the two
	 must stay one function: tests/unit/test_image.c asserts that an
	 Interval and the Array of its elements hash alike on both sides of
	 the threshold.

	 A collection that holds ITSELF contributes nothing for that element
	 rather than recursing into its own hash (Bugs3 B34)."
	| h size each |
	h := 2166136261.
	size := self size.
	size <= 32
		ifTrue: [
			self do: [:element |
				h := ((h bitXor: ((element == self ifTrue: [0] ifFalse: [element hash])
							bitAnd: 16rFFFFFFFF)) * 16777619)
						bitAnd: 16rFFFFFFFF]]
		ifFalse: [
			h := ((h bitXor: (size bitAnd: 16rFFFFFFFF)) * 16777619) bitAnd: 16rFFFFFFFF.
			0 to: 15 do: [:k |
				each := self at: 1 + (k * (size - 1) // 15).
				h := ((h bitXor: ((each == self ifTrue: [0] ifFalse: [each hash])
							bitAnd: 16rFFFFFFFF)) * 16777619)
						bitAnd: 16rFFFFFFFF]].
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

{ #category : '*Collections-Protocol' }
SequenceableCollection >> copyFrom: start to: stop [
	"The elements from start to stop, or nothing when stop is before
	 start.

	 1983 computes the new size as `stop - start + 1' and asks my species
	 for that many, which for an empty range is fine and for an INVERTED
	 one is a negative size, and `species new: -1' fails its primitive:
	 `#() allButFirst' and `#(1 2 3) allButFirst: 5' both died with `a
	 primitive has failed' (Bugs3 B51).  An empty answer is what an
	 inverted range means, and it is what 1983's own OrderedCollection
	 answers for the same question, so the two now agree.  A range that
	 reaches outside me still fails, in replaceFrom:to:with:startingAt:,
	 with the subscript that did."
	| newSize |
	stop < start ifTrue: [^self species new: 0].
	newSize := stop - start + 1.
	^(self species new: newSize)
		replaceFrom: 1
		to: newSize
		with: self
		startingAt: start
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> copyFrom: start [
	"The elements from start to my end.

	 Pharo's, and without it `#(1 2 3) copyFrom: 2' found Object>>copyFrom:
	 -- which copies the ARGUMENT'S instance variables into the receiver,
	 and answered the whole array unchanged (Bugs3 B51).  For a sequence
	 the one-argument form can only mean this."
	^self copyFrom: start to: self size
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> , aSequenceableCollection [
	"A copy of me with that collection's elements after mine -- and a
	 refusal when the argument is not a collection at all.

	 1983 hands the argument to copyReplaceFrom:to:with:, which asks it
	 for its size, and Object>>size answers 0 for a SmallInteger and for
	 nil: `'abc' , 3' and `'abc' , nil' both answered 'abc', quietly
	 (Bugs3 B51).  A collection of the wrong kind of element is still
	 refused where it always was, by the store."
	aSequenceableCollection isCollection
		ifFalse: [^self error: 'cannot append a ' , aSequenceableCollection class name
					, ' to a ' , self class name , ': it is not a collection'].
	^self copyReplaceFrom: self size + 1
		to: self size
		with: aSequenceableCollection
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> mergeSortFrom: first to: last by: aBlock into: scratch [
	"Sort my elements from first to last in place, keeping those aBlock
	 ranks equal in the order they were in; scratch is an Array at least
	 last long that the merge copies through.

	 The usual top-down merge sort.  Each half is sorted, and when the
	 last of the left half may precede the first of the right there is
	 nothing to merge -- which is what makes an already sorted collection
	 cost one comparison per element.  The merge takes from the LEFT run
	 whenever the block allows it, which is the whole of the stability
	 argument: an element from the left half precedes every element of
	 the right half that the block calls its equal, as it did before."
	| middle i j k |
	last - first < 1 ifTrue: [^self].
	middle := first + last // 2.
	self mergeSortFrom: first to: middle by: aBlock into: scratch.
	self mergeSortFrom: middle + 1 to: last by: aBlock into: scratch.
	(aBlock value: (self at: middle) value: (self at: middle + 1)) ifTrue: [^self].
	first to: last do: [:n | scratch at: n put: (self at: n)].
	i := first.
	j := middle + 1.
	k := first.
	[i <= middle and: [j <= last]] whileTrue: [
		(aBlock value: (scratch at: i) value: (scratch at: j))
			ifTrue: [self at: k put: (scratch at: i). i := i + 1]
			ifFalse: [self at: k put: (scratch at: j). j := j + 1].
		k := k + 1].
	[i <= middle] whileTrue: [self at: k put: (scratch at: i). i := i + 1. k := k + 1].
	[j <= last] whileTrue: [self at: k put: (scratch at: j). j := j + 1. k := k + 1]
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> indexOfSubCollection: aSubCollection startingAt: anIndex ifAbsent: exceptionBlock [
	"Where aSubCollection first occurs in me at or after anIndex, or the
	 block's value.

	 The same clamp String>>findString:startingAt: needed and for the same
	 reason (Bugs4 COLL-5): 1983 writes `anIndex to: self size -
	 aSubCollection size + 1 do:', so an index past my end answers the
	 block cleanly -- the loop never runs -- while an index of zero or
	 less made the first thing the loop did `self at: 0'.  One end of the
	 range was outside me and answered; the other was outside me and
	 raised.  Before my beginning means from my beginning.

	 The rest is 1983's, including its habit of comparing the whole
	 subcollection rather than stopping at the first mismatch."
	| firstElement eq |
	aSubCollection size = 0 ifTrue: [^exceptionBlock value].
	firstElement := aSubCollection at: 1.
	(anIndex max: 1) to: self size - aSubCollection size + 1 do: [:i |
		eq := true.
		(self at: i) = firstElement ifTrue: [
			1 to: aSubCollection size do: [:index |
				(self at: i + index - 1) = (aSubCollection at: index)
					ifFalse: [eq := false]].
			eq ifTrue: [^i]]].
	^exceptionBlock value
]

{ #category : '*Collections-Protocol' }
SequenceableCollection >> copyReplaceAll: oldSubCollection with: newSubCollection [
	"A copy of me with every run of oldSubCollection replaced by
	 newSubCollection.

	 Both arguments must be COLLECTIONS, and that is the whole of the
	 change.  1983 measures the replacement with `size' and copies it in
	 with replaceFrom:to:with:startingAt:, and `Character>>size' is zero
	 -- a Character is not a collection and answers the basicSize every
	 Object answers -- so `'abc' copyReplaceAll: 'b' with: $z' found the
	 `b', replaced it with nothing, and answered 'ac' (Bugs4 COLL-4).  A
	 method that DELETES what it was asked to substitute, and says
	 nothing, is the worst shape a papercut can take: the answer is a
	 String of the right kind and the wrong contents, and it flows on into
	 whatever asked for it.

	 The argument is refused rather than wrapped up as a one-element
	 sequence, and the decision is symmetric on purpose.  Wrapping the
	 REPLACEMENT would fix the line above and leave `'abc' copyReplaceAll:
	 $b with: 'z'' answering 'abc' unchanged, since indexOfSubCollection:
	 startingAt: reads the pattern's size the same way and finds a run of
	 zero characters; a coercion applied to one argument of a pair is how
	 the confusion got here.  1983 coerces nowhere else in this protocol
	 either.  So both are checked, in a sentence that names what was
	 passed, and `String with: $z' is one message away for a caller that
	 meant a one-character String.

	 Text is a Collection, so the Parser's copyReplaceAll:with: over Texts
	 is unaffected."
	| answer startSearch currentIndex |
	oldSubCollection isCollection
		ifFalse: [^self error: 'copyReplaceAll: needs a collection to look for, not ' , oldSubCollection class name].
	newSubCollection isCollection
		ifFalse: [^self error: 'copyReplaceAll:with: needs a collection to put in, not ' , newSubCollection class name].
	answer := self.
	startSearch := 1.
	[(currentIndex := answer indexOfSubCollection: oldSubCollection startingAt: startSearch) > 0]
		whileTrue: [
			answer := answer
				copyReplaceFrom: currentIndex
				to: currentIndex + oldSubCollection size - 1
				with: newSubCollection.
			startSearch := currentIndex + newSubCollection size].
	^answer
]

"
Forty years of collection protocol that 1983 does not have.

One decision runs through all of it and is worth stating once rather than
apologising for repeatedly: where Pharo answers `self species` -- so that
collecting over an OrderedCollection gives you an OrderedCollection -- the
methods here that CHANGE the shape of the result answer an **Array**.

flatCollect: is the clearest case.  A sorted Set is not a Set,
and a flattened Bag is not a Bag, so `species` is already the wrong answer
for two of the three most interesting receivers; Pharo works around that
with a family of `as:` variants.  An Array is what every one of them can
answer honestly and it is predictable without knowing the receiver.

sorted and sorted: are the exception, and Pharo's own doctest is what
pointed it out: a SEQUENCEABLE collection has an obvious species to answer,
because order is what it is for, so SequenceableCollection overrides both
and 'hello' sorted is 'ehllo'.  The Array answer stands for everything
else, where there is nothing better.  The methods that PRESERVE shape
-- select:, reject:, collect:, copyWithout: -- go on using species, because
there the 1983 answer was already right.
"
Extension { #name : 'Collection' }

{ #category : '*Collections-Protocol' }
Collection >> do: elementBlock separatedBy: separatorBlock [
	"Run the separator between elements, never before the first and never
	 after the last -- which is the whole reason this is not a do: with an
	 nextPutAll: at the end."
	| first |
	first := true.
	self do: [:each |
		first
			ifTrue: [first := false]
			ifFalse: [separatorBlock value].
		elementBlock value: each]
]

{ #category : '*Collections-Protocol' }
Collection >> asArray [
	| result index |
	result := Array new: self size.
	index := 0.
	self do: [:each | result at: (index := index + 1) put: each].
	^result
]

{ #category : '*Collections-Protocol' }
Collection >> sorted [
	"An Array, sorted.  See the class comment on why not the species."
	^self asSortedCollection asArray
]

{ #category : '*Collections-Protocol' }
Collection >> sorted: aBlock [
	^(self asSortedCollection: aBlock) asArray
]

{ #category : '*Collections-Protocol' }
Collection >> flatCollect: aBlock [
	"Collect, then concatenate what each answer contains, one level deep."
	| out |
	out := OrderedCollection new.
	self do: [:each | out addAll: (aBlock value: each)].
	^out asArray
]

{ #category : '*Collections-Protocol' }
Collection >> count: aBlock [
	| n |
	n := 0.
	self do: [:each | (aBlock value: each) ifTrue: [n := n + 1]].
	^n
]

{ #category : '*Collections-Protocol' }
Collection >> anySatisfy: aBlock [
	"Stops at the first one that does, which is why it is a non-local
	 return out of the block rather than a flag."
	self do: [:each | (aBlock value: each) ifTrue: [^true]].
	^false
]

{ #category : '*Collections-Protocol' }
Collection >> allSatisfy: aBlock [
	self do: [:each | (aBlock value: each) ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Collection >> noneSatisfy: aBlock [
	self do: [:each | (aBlock value: each) ifTrue: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Collection >> notEmpty [
	^self isEmpty not
]

{ #category : '*Collections-Protocol' }
Collection >> ifEmpty: aBlock [
	^self isEmpty
		ifTrue: [aBlock cull: self]
		ifFalse: [self]
]

{ #category : '*Collections-Protocol' }
Collection >> ifNotEmpty: aBlock [
	^self isEmpty
		ifTrue: [nil]
		ifFalse: [aBlock cull: self]
]

{ #category : '*Collections-Protocol' }
Collection >> ifEmpty: emptyBlock ifNotEmpty: notEmptyBlock [
	^self isEmpty
		ifTrue: [emptyBlock cull: self]
		ifFalse: [notEmptyBlock cull: self]
]

{ #category : '*Collections-Protocol' }
Collection >> ifNotEmpty: notEmptyBlock ifEmpty: emptyBlock [
	^self isEmpty
		ifTrue: [emptyBlock cull: self]
		ifFalse: [notEmptyBlock cull: self]
]

{ #category : '*Collections-Protocol' }
Collection >> anyOne [
	"Some element, unspecified which.  Meaningful for a Set, where there
	 is no first."
	self do: [:each | ^each].
	^self emptyCheck
]

{ #category : '*Collections-Protocol' }
Collection >> sum [
	"Signals on an empty collection rather than answering zero: the sum of
	 nothing depends on what was going to be added, and this collection
	 cannot say."
	| total first |
	total := nil.
	first := true.
	self do: [:each |
		first
			ifTrue: [total := each. first := false]
			ifFalse: [total := total + each]].
	first ifTrue: [^self emptyCheck].
	^total
]

{ #category : '*Collections-Protocol' }
Collection >> sum: aBlock [
	^(self collect: aBlock) sum
]

{ #category : '*Collections-Protocol' }
Collection >> average [
	^self sum / self size
]

{ #category : '*Collections-Protocol' }
Collection >> max [
	| best first |
	best := nil.
	first := true.
	self do: [:each |
		(first or: [each > best]) ifTrue: [best := each. first := false]].
	first ifTrue: [^self emptyCheck].
	^best
]

{ #category : '*Collections-Protocol' }
Collection >> min [
	| best first |
	best := nil.
	first := true.
	self do: [:each |
		(first or: [each < best]) ifTrue: [best := each. first := false]].
	first ifTrue: [^self emptyCheck].
	^best
]

{ #category : '*Collections-Protocol' }
Collection >> copyWithout: anObject [
	^self reject: [:each | each = anObject]
]

{ #category : '*Collections-Protocol' }
Collection >> isCollection [
	^true
]

{ #category : '*Collections-Protocol' }
Collection >> contents [
	"My elements, as a collection of my own kind.

	 A copy rather than the receiver: contents is what a caller asks for
	 when it wants the values without holding on to the thing that owns
	 them, and answering self makes that caller's later mutation somebody
	 else's surprise.  Streams answer contents too, meaning the same thing
	 -- what has been accumulated -- which is why the name is worth having
	 on both."
	^self copy
]

{ #category : '*Collections-Protocol' }
Set >> = aCollection [
	"Same class, same size, same elements.

	 1983 leaves Set with Object's identity =, so two Sets built the same
	 way are never equal and `assert: x equals: y' on them can only fail --
	 which it did, reporting `expected Set (1 7 ) but got Set (1 7 )', a
	 message that reads like a bug in the printer.

	 Defined on Set and not on HashedCollection, which is where Pharo puts
	 it, because Dictionary is a HashedCollection here too and equality for
	 a Dictionary has to consider keys as well as values.  Getting that
	 wrong quietly is worse than not answering it."
	self == aCollection ifTrue: [^true].
	self class == aCollection class ifFalse: [^false].
	self size = aCollection size ifFalse: [^false].
	self do: [:each | (aCollection includes: each) ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Set >> hash [
	"Derived from my elements, so that it agrees with the = above.

	 Added in the same breath as Set>>= and for the same reason that
	 Symbol>>hash had to follow Symbol>>=: giving a class value equality and
	 leaving it Object's identity hash produces objects that are equal and
	 hash differently, which is precisely the pair a hashed collection
	 cannot hold.  Defining = without hash does not leave things as they
	 were -- it breaks them in a new place, one indirection further away.

	 Combined with bitXor:, which is commutative, because a Set has no
	 order and two Sets that are equal must hash the same however they were
	 built up.  The size is folded in so that sets whose elements happen to
	 cancel are still told apart.

	 A Set that holds ITSELF is skipped over rather than recursed into
	 (Bugs3 B34): its own hash is what is being computed, so there is
	 nothing to ask it for, and asking recursed until
	 RecursionDepthExceeded.  Dictionary, which inherits everything else
	 here, has a hash of its own over its keys, because a Dictionary's
	 self-reference lives in a value -- TextConstants under #TextConstants,
	 Smalltalk under #Smalltalk -- and do: yields the values."
	| h |
	h := self size hash.
	self do: [:each | each == self ifFalse: [h := h bitXor: each hash]].
	^h
]

{ #category : '*Collections-Protocol' }
Collection class >> with: a with: b with: c with: d with: e [
	"1983 stops at four.  Pharo's collection tests build five-element
	 literals constantly -- it is the smallest size at which an ordering
	 bug shows -- so the fifth is worth having."
	^(self with: a with: b with: c with: d) add: e; yourself
]

{ #category : '*Collections-Protocol' }
Collection class >> with: a with: b with: c with: d with: e with: f [
	^(self with: a with: b with: c with: d with: e) add: f; yourself
]

{ #category : '*Collections-Protocol' }
Collection >> copyEmpty [
	"An empty collection of my kind.  Pharo asks this of a receiver rather
	 than of its class, so that a collection carrying extra state -- a
	 sort block, an equality block -- can hand that state on."
	^self species new
]

{ #category : '*Collections-Protocol' }
Collection >> as: aClass [
	"Me, converted to that class.

	 Through newFrom: and not withAll:, and the difference is not stylistic.
	 withAll: adds my elements one at a time, which is right for a Set and
	 wrong for a Dictionary: `{2 -> 10. 100 -> 5} as: Dictionary' has to
	 UNWRAP each Association into a key and a value, and adding them as
	 elements produced a Dictionary whose values were Associations.  It did
	 not fail -- `at: 2' answered `100->5', a wrong value of a wrong type,
	 and the tests then sent it #+.

	 newFrom: is where a class says how to be built from a collection, and
	 Dictionary answers it properly.  Collection class>>newFrom: below
	 falls back to withAll: for everything that does not care."
	^aClass newFrom: self
]

{ #category : '*Collections-Protocol' }
Collection class >> withAll: aCollection [
	"A new collection of my kind holding all of those elements."
	^self new addAll: aCollection; yourself
]

{ #category : '*Collections-Protocol' }
Collection >> identityIncludes: anObject [
	"Whether anObject is one of my elements, by identity rather than by =.
	 Two equal Strings are one element to includes: and two to this."
	self do: [:each | anObject == each ifTrue: [^true]].
	^false
]

{ #category : '*Collections-Protocol' }
Collection >> flatCollect: aBlock as: aClass [
	"Collect, then flatten one level, into a collection of that class."
	| all |
	all := OrderedCollection new.
	self do: [:each | all addAll: (aBlock value: each)].
	^aClass withAll: all
]

{ #category : '*Collections-Protocol' }
Collection >> flatCollect: aBlock [
	^self flatCollect: aBlock as: self species
]

{ #category : '*Collections-Protocol' }
Collection >> select: selectBlock thenCollect: collectBlock [
	"Filter then map, in one pass.

	 Not the same as `(self select: a) collect: b' for every receiver: a
	 Dictionary's select: answers a Dictionary and its collect: a Bag, so
	 chaining them changes the species twice.  Doing both in one pass keeps
	 the answer the species collect: would give."
	| result |
	result := OrderedCollection new.
	self do: [:each |
		(selectBlock value: each)
			ifTrue: [result add: (collectBlock value: each)]].
	^self species withAll: result
]

{ #category : '*Collections-Protocol' }
Collection >> collect: collectBlock thenSelect: selectBlock [
	| result |
	result := OrderedCollection new.
	self do: [:each |
		| mapped |
		mapped := collectBlock value: each.
		(selectBlock value: mapped) ifTrue: [result add: mapped]].
	^self species withAll: result
]

{ #category : '*Collections-Protocol' }
Collection >> reduce: aBlock [
	"Fold with no initial value, the first element being the seed.

	 Collection, not SequenceableCollection: a Set has no order, and reduce:
	 is defined for it anyway because the block is meant to be associative.
	 Answering the elements in whatever order do: gives them is the honest
	 reading of that.

	 Answered through reduceLeft: rather than a do: loop of value:value:,
	 because the block need not take two arguments -- `reduce: [:a :b :c |
	 a + b + c]' folds three at a time, and a do: loop can only ever send
	 one selector."
	^self asOrderedCollection reduceLeft: aBlock
]

{ #category : '*Collections-Protocol' }
Collection >> includesAny: aCollection [
	"True if any element of that collection is one of mine."
	aCollection do: [:each | (self includes: each) ifTrue: [^true]].
	^false
]

{ #category : '*Collections-Protocol' }
Collection >> includesAll: aCollection [
	aCollection do: [:each | (self includes: each) ifFalse: [^false]].
	^true
]

{ #category : '*Collections-Protocol' }
Collection class >> newFrom: aCollection [
	"Build one of me from that collection.

	 The default is to add the elements one at a time, which is what most
	 classes want.  Dictionary overrides it, because an Association is a
	 pair to be unwrapped and not an element to be added."
	^self withAll: aCollection
]

{ #category : '*Collections-Protocol' }
Collection >> copyWith: anObject [
	"A copy of me with that added.

	 On Collection, not only on SequenceableCollection where 1983 puts it: a
	 Set and a Bag answer copyWith: too, and for them `added' means whatever
	 add: means -- absorbed for a Set, counted again for a Bag."
	| copy |
	copy := self copy.
	copy add: anObject.
	^copy
]

{ #category : '*Collections-Protocol' }
Collection >> copyWithout: anObject [
	"A copy of me with every occurrence of that removed.

	 Through reject: rather than by adding to `self species new', and the
	 difference is a Dictionary.  Its do: yields VALUES, so rebuilding it by
	 adding what do: gives adds values where associations belong -- the
	 dictionary then sends #key to a SmallInteger and the failure surfaces
	 nowhere near here.  reject: is the operation each class already
	 implements correctly for its own shape."
	^self reject: [:each | each = anObject]
]

{ #category : '*Collections-Protocol' }
Collection >> , aCollection [
	"My elements followed by that collection's.

	 For an unordered receiver `followed by' has no meaning beyond union
	 with repetition, which is what adding them one after another gives --
	 and is what a Bag's tests expect."
	| joined |
	joined := self copy.
	aCollection do: [:each | joined add: each].
	^joined
]

{ #category : '*Collections-Protocol' }
Collection >> intersection: aCollection [
	"The elements I share with that collection, as a collection of my kind."
	| result |
	result := self species new.
	self do: [:each |
		((aCollection includes: each) and: [(result includes: each) not])
			ifTrue: [result add: each]].
	^result
]

{ #category : '*Collections-Protocol' }
Collection >> union: aCollection [
	"Everything in either of us, without repetition."
	| result |
	result := Set new.
	self do: [:each | result add: each].
	aCollection do: [:each | result add: each].
	^self species withAll: result
]

{ #category : '*Collections-Protocol' }
Collection >> difference: aCollection [
	"What I have that it does not."
	^self reject: [:each | aCollection includes: each]
]

{ #category : '*Collections-Protocol' }
Collection >> associationsDo: aBlock [
	"Evaluate aBlock with each of my elements, which are Associations.

	 A Dictionary answers this from its own storage.  A plain COLLECTION of
	 Associations has to answer it too, and the reason is not hypothetical:
	 Dictionary>>intersection: is `self species newFrom: (self associations
	 asSet intersection: ...)', so the intermediate is a Set of Associations
	 and newFrom: walks it with exactly this message."
	self do: aBlock
]

{ #category : '*Collections-Protocol' }
Collection >> copyWithoutAll: aCollection [
	"A copy of me with every element of that collection removed.
	 Through reject:, for the reason given at copyWithout: above."
	^self reject: [:each | aCollection includes: each]
]

{ #category : '*Collections-Protocol' }
Collection >> isCollection [
	^true
]

{ #category : '*Collections-Protocol' }
Collection >> printElementsOn: aStream [
	"My elements between parentheses, separated by spaces.

	 Factored out of printOn: in Pharo so that a subclass can keep the
	 element listing and change what surrounds it -- which is why the tests
	 send it directly rather than reading printString."
	aStream nextPut: $(.
	self do: [:element | aStream print: element]
		separatedBy: [aStream space].
	aStream nextPut: $)
]

{ #category : '*Collections-Protocol' }
Collection >> addIfNotPresent: anObject [
	"Add it unless I already have it, and answer it either way.

	 Meaningless for a Set, which absorbs duplicates anyway, and not for a
	 Bag or an OrderedCollection -- which is why it is worth having on
	 Collection rather than being written off as a Set synonym."
	(self includes: anObject) ifFalse: [self add: anObject].
	^anObject
]

{ #category : '*Collections-Protocol' }
Collection >> asByteArray [
	"My elements as a ByteArray.  Every element must be an integer 0..255,
	 which is the point of asking: it fails loudly on anything else rather
	 than truncating."
	| bytes index |
	bytes := ByteArray new: self size.
	index := 0.
	self do: [:each | bytes at: (index := index + 1) put: each].
	^bytes
]

{ #category : '*Collections-Protocol' }
Collection >> collect: aBlock as: aClass [
	"Collect into a collection of that class rather than of my species.

	 `#(1 2 3) collect: [:e | e * 2] as: Set' -- the point being to choose
	 the answer's class at the call site instead of accepting whatever the
	 receiver's species happens to be."
	| result |
	result := OrderedCollection new.
	self do: [:each | result add: (aBlock value: each)].
	^aClass newFrom: result
]

{ #category : '*Collections-Protocol' }
Collection >> removeAllSuchThat: aBlock [
	"Remove every element the block accepts, and answer me.

	 Gathered first and removed afterwards, because removing from a
	 collection while enumerating it is undefined for a Set -- the scan
	 relocates on every removal."
	| doomed |
	doomed := OrderedCollection new.
	self do: [:each | (aBlock value: each) ifTrue: [doomed add: each]].
	doomed do: [:each | self remove: each ifAbsent: [nil]].
	^self
]

{ #category : '*Collections-Protocol' }
Collection >> addAll: aCollection [
	"1983's, guarded against being given myself.

	 `aCollection do: [:each | self add: each]' enumerates the argument
	 while adding to the receiver, and when they are the same object the
	 enumeration walks over what it is adding: an OrderedCollection of
	 1 2 3 answered 1 2 3 1 2 3 1, stopping only when its growth happened
	 to move the elements out from under the walk (Bugs3 B49).  What
	 `addAll: self' can only mean is `add every element I had', so the
	 argument is copied first, once, and the answer is still the argument
	 as 1983 promises.  OrderedCollection and SortedCollection have their
	 own addAll: and carry the same guard."
	aCollection == self ifTrue: [self addAll: self copy. ^aCollection].
	aCollection do: [:each | self add: each].
	^aCollection
]

{ #category : '*Collections-Protocol' }
Collection >> removeAll: aCollection [
	"1983's, guarded against being given myself.

	 Removing from the receiver while enumerating it is undefined for
	 every kind of collection here -- an OrderedCollection shifts the rest
	 down over the index being walked, so `removeAll: self' on 1 2 3 4
	 removed 1 and 3 and left 2 4 (Bugs3 B49); a Set relocates on every
	 removal.  The copy makes the enumeration walk something that stays
	 put, which is the same thing removeAllSuchThat: below does by
	 gathering first."
	aCollection == self ifTrue: [self removeAll: self copy. ^aCollection].
	aCollection do: [:each | self remove: each].
	^aCollection
]

{ #category : '*Collections-Protocol' }
Collection >> removeAllFoundIn: aCollection [
	"Remove whichever of that collection's elements I have, ignoring the
	 ones I do not -- which is the whole difference from removeAll:."
	aCollection do: [:each | self remove: each ifAbsent: [nil]].
	^aCollection
]

{ #category : '*Collections-Protocol' }
Collection >> printNameOn: aStream [
	"How I name myself before my elements.  Split out from printOn: so a
	 subclass can change the name and keep the listing, which is the mirror
	 of printElementsOn:."
	aStream nextPutAll: self class name article;
		nextPut: $ ;
		nextPutAll: self class name
]

{ #category : '*Collections-Protocol' }
Collection class >> newFromArray: anArray [
	"One of me built from that array, and the receiver decides what its
	 elements mean.

	 Through newFrom: and not withAll:, for the same reason as Collection>>as:
	 above: an Array of Associations means one thing to a Set and another to
	 a Dictionary, and only the class being built knows which.  Sent
	 withAll:, a Dictionary answered its addAll:, which reads an Array as a
	 KEYED collection -- index 1, index 2 -- and built `1 -> (#a->1)'.  The
	 result printed almost right and answered nil to `at: #a'."
	^self newFrom: anArray
]

{ #category : '*Collections-Protocol' }
Collection >> printOn: aStream [
	"My name, then my elements.

	 Split in two where 1983 has one method, because Pharo's collections
	 override the halves separately: Dictionary and SmallDictionary each
	 define printElementsOn: and neither defines printOn:, so with 1983's
	 single method their printing was simply never reached.  The name half
	 also carries the article -- `a Set(1 2 3 )', not `Set (1 2 3 )' -- which
	 is what the printing tests split on."
	self printNameOn: aStream.
	self printElementsOn: aStream
]

{ #category : '*Collections-Protocol' }
Collection >> printElementsOn: aStream [
	"My elements, parenthesised and space separated.

	 The maxPrint ceiling is 1983's and is kept: printing an unbounded
	 collection into a debugger transcript is how a slow inspector becomes a
	 hung one."
	| tooMany |
	tooMany := aStream position + self maxPrint.
	aStream nextPut: $(.
	self do: [:element |
		aStream position > tooMany
			ifTrue: [aStream nextPutAll: '...etc...)'. ^self].
		element printOn: aStream.
		aStream space].
	aStream nextPut: $)
]

{ #category : '*Collections-Protocol' }
Collection class >> <- anArray [
	"Literal-ish construction: `Set <- #(1 2 3)'.  Pharo's shorthand, and
	 the reason the lexer had to accept binary selectors longer than the two
	 characters Chapter 30 allows."
	^self newFromArray: anArray
]

{ #category : '*Collections-Protocol' }
Collection class >> empty [
	^self new
]

{ #category : '*Collections-Protocol' }
Collection >> do: aBlock without: anObject [
	"Every element except that one."
	self do: [:each | each = anObject ifFalse: [aBlock value: each]]
]

{ #category : '*Collections-Protocol' }
Collection >> maxSize [
	"The largest basicSize a collection of my kind may have.

	 1983 answers 65486 and says why in its own comment: `for VM3
	 interpreter DoradoST80Aug19'.  It is a sixteen-bit machine's limit,
	 written down in 1983 for a machine that no longer exists, and it is
	 wrong here by four orders of magnitude -- this object memory keeps an
	 object's size in a 32-bit field and its real ceiling is how much the
	 allocator will give.

	 It is not a dormant number.  A SharedQueue is an OrderedCollection
	 underneath, and OrderedCollection grows by doubling, so a queue that
	 reaches 32768 items asks for 65536 and is refused -- `unable to grow
	 this collection', raised inside Mutex>>critical:, inside ensure:,
	 while holding the lock.  Every other worker then blocks on a mutex
	 nobody will release.  test_parallel_lib queues workers x 2000 items,
	 so it passed at 32 workers and deadlocked at 40, and passed at 31 on
	 an idle machine while failing there under load -- which reads exactly
	 like a race and is a constant.

	 A billion rather than the 32-bit maximum: the point is to stop being
	 the binding limit, not to promise an allocation nothing could satisfy."
	^1073741823
]

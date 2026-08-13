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

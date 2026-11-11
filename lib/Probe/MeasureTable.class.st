"
Measures kept together, one table shared by the whole image as the default.

The default is any worker's, so it is made once and its entries are
written under a Mutex of its own (Bugs07 KERNB-10): see default and lock.
"
Class {
	#name : 'MeasureTable',
	#superclass : 'Object',
	#instVars : [ 'entries', 'lock' ],
	#classInstVars : [ 'default' ],
	#category : 'Probe-Core'
}

{ #category : 'instance creation' }
MeasureTable class >> default [
	"The one shared table, made on first use by whichever worker asks
	 first.

	 It was `default isNil ifTrue: [default := self new]', a check and
	 then an act, and two workers asking at once each made one: eight
	 workers came away with two different defaults, and what was added
	 to the first was lost when the second was stored over it (Bugs07
	 KERNB-10).  The new table is stored only over nil, by
	 compare-and-swap on this class's own field; a worker that lost the
	 race drops the table it made and answers the one stored."
	default isNil ifTrue: [
		self
			compareAndSwapSlot: (self class allInstVarNames indexOf: 'default')
			from: nil
			to: self new].
	^default
]

{ #category : 'initialization' }
MeasureTable >> initialize [
	entries := OrderedCollection new.
	lock := Mutex new
]

{ #category : 'private' }
MeasureTable >> lock [
	"The Mutex every reader and writer of entries holds.

	 An OrderedCollection is not safe to share, and the default table is
	 shared by every worker: eight adding at once lost two thousand of
	 sixteen thousand entries and raised on others (Bugs07 KERNB-10).
	 new makes the lock in initialize; this makes it for a table that
	 was not made by new, once, by compare-and-swap, so two first users
	 cannot each make one."
	lock isNil ifTrue: [
		self
			compareAndSwapSlot: (self class allInstVarNames indexOf: 'lock')
			from: nil
			to: Mutex new].
	^lock
]

{ #category : 'adding' }
MeasureTable >> add: aMeasure [
	self lock critical: [
		entries isNil ifTrue: [entries := OrderedCollection new].
		entries add: aMeasure].
	^aMeasure
]

{ #category : 'accessing' }
MeasureTable >> total [
	^self lock critical: [
		entries isNil
			ifTrue: [0]
			ifFalse: [entries inject: 0 into: [:sum :each | sum + each amount]]]
]

{ #category : 'accessing' }
MeasureTable >> size [
	^self lock critical: [entries isNil ifTrue: [0] ifFalse: [entries size]]
]

{ #category : 'tricky' }
MeasureTable >> bracketsEverywhere [
	"A ] in a comment, then in a string, a character and a literal array."
	^#($[ $] 'a ] b') size + '] [' size + [3] value
]

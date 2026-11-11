"
A quantity with a unit, and the root of this package's small hierarchy.

The package exists to prove the Tonel loader end to end: five classes, a
superclass chain three deep, class variables, class-side instance
variables, an extension to a 1983 class, and methods that exercise the
bracket cases a body scanner has to get right.
"
Class {
	#name : 'Measure',
	#superclass : 'Object',
	#instVars : [ 'amount' ],
	#classVars : [ 'Made' ],
	#category : 'Probe-Core'
}

{ #category : 'instance creation' }
Measure class >> of: aNumber [
	self countMade.
	^self new setAmount: aNumber
]

{ #category : 'private' }
Measure class >> countMade [
	"Add one to Made, on any worker, without losing a count.

	 `Made := Made + 1' reads, adds and stores, and two workers between
	 the read and the store both stored the same sum: eight workers
	 making two thousand Measures each counted 15,892 of 16,000 (Bugs07
	 KERNB-10).  The count is stored only over the value it was computed
	 from, by compare-and-swap on the class variable's binding -- the
	 Association every method of this class that names Made holds --
	 and computed again when another worker got there first.  Lock-free
	 rather than under a Mutex because a class variable is all this
	 class has to keep one in, and the package exists to show the
	 loader handles exactly that shape."
	| binding old |
	binding := Measure classPool associationAt: #Made.
	[old := binding value.
	 binding
		compareAndSwapSlot: 2
		from: old
		to: (old isNil ifTrue: [1] ifFalse: [old + 1])] whileFalse
]

{ #category : 'instance creation' }
Measure class >> made [
	^Made isNil ifTrue: [0] ifFalse: [Made]
]

{ #category : 'private' }
Measure >> setAmount: aNumber [
	amount := aNumber
]

{ #category : 'accessing' }
Measure >> amount [
	^amount
]

{ #category : 'accessing' }
Measure >> unit [
	^self subclassResponsibility
]

{ #category : 'printing' }
Measure >> describe [
	"Answers something like '3 metres'.  The comment holds a ] on purpose."
	^amount printString , ' ' , self unit
]

{ #category : 'comparing' }
Measure >> + another [
	^self class of: amount + another amount
]

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
	Made := (Made isNil ifTrue: [0] ifFalse: [Made]) + 1.
	^self new setAmount: aNumber
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

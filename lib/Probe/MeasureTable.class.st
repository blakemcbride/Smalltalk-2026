Class {
	#name : 'MeasureTable',
	#superclass : 'Object',
	#instVars : [ 'entries' ],
	#classInstVars : [ 'default' ],
	#category : 'Probe-Core'
}

{ #category : 'instance creation' }
MeasureTable class >> default [
	default isNil ifTrue: [default := self new].
	^default
]

{ #category : 'initialization' }
MeasureTable >> initialize [
	entries := OrderedCollection new
]

{ #category : 'adding' }
MeasureTable >> add: aMeasure [
	entries isNil ifTrue: [self initialize].
	entries add: aMeasure.
	^aMeasure
]

{ #category : 'accessing' }
MeasureTable >> total [
	entries isNil ifTrue: [^0].
	^entries inject: 0 into: [:sum :each | sum + each amount]
]

{ #category : 'accessing' }
MeasureTable >> size [
	^entries isNil ifTrue: [0] ifFalse: [entries size]
]

{ #category : 'tricky' }
MeasureTable >> bracketsEverywhere [
	"A ] in a comment, then in a string, a character and a literal array."
	^#($[ $] 'a ] b') size + '] [' size + [3] value
]

Class {
	#name : 'Mass',
	#superclass : 'Measure',
	#instVars : [ 'tare' ],
	#category : 'Probe-Core'
}

{ #category : 'accessing' }
Mass >> unit [
	^'grams'
]

{ #category : 'accessing' }
Mass >> tare [
	^tare isNil ifTrue: [0] ifFalse: [tare]
]

{ #category : 'accessing' }
Mass >> tare: aNumber [
	tare := aNumber
]

{ #category : 'accessing' }
Mass >> net [
	^amount - self tare
]

Class {
	#name : 'Length',
	#superclass : 'Measure',
	#category : 'Probe-Core'
}

{ #category : 'accessing' }
Length >> unit [
	^'metres'
]

{ #category : 'converting' }
Length >> asCentimetres [
	^Length of: amount * 100
]

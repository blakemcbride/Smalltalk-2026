"
Pharo-flavoured code writes an initialize method and expects new to call it.
The 1983 system does not: Behavior>>new is primitive 70 and nothing else.
This class says initialize and no new, so the loader writes the 1983 idiom
for it -- ^super new initialize, the same method the thirty-four 1983
classes that want initialization already write by hand.
"
Class {
	#name : 'Initialized',
	#superclass : 'Object',
	#instVars : [ 'count' ],
	#category : 'Probe-Core'
}

{ #category : 'initialization' }
Initialized >> initialize [
	count := 1
]

{ #category : 'accessing' }
Initialized >> count [
	^count
]

{ #category : 'accessing' }
Initialized >> bump [
	count := count + 1
]

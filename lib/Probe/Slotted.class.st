"
A class written the way Tonel v3 writes one, with #slots rather than
#instVars.  For a plain slot the two say the same thing.
"
Class {
	#name : 'Slotted',
	#superclass : 'Object',
	#slots : [ 'left', 'right' ],
	#category : 'Probe-Core'
}

{ #category : 'accessing' }
Slotted >> left: a right: b [
	left := a.
	right := b
]

{ #category : 'accessing' }
Slotted >> sum [
	^left + right
]

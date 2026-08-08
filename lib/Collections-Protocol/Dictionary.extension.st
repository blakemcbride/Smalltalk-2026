Extension { #name : 'Dictionary' }

{ #category : '*Collections-Protocol' }
Dictionary >> at: key ifAbsentPut: aBlock [
	"Answer the value, adding the block's answer first if there is none.

	 Under threads this is check-then-act and two workers can each build
	 one; see doc/ParallelAudit.md when that day comes.  Single-threaded
	 it is the idiom every cache is written with."
	^self at: key ifAbsent: [self at: key put: aBlock value]
]

{ #category : '*Collections-Protocol' }
Dictionary >> keysAndValuesDo: aBlock [
	self associationsDo: [:each | aBlock value: each key value: each value]
]

{ #category : '*Collections-Protocol' }
Dictionary >> keysSortedSafely [
	^self keys asSortedCollection asArray
]

{ #category : '*Collections-Protocol' }
Dictionary >> associationsSortedByKey [
	^(self associations asSortedCollection: [:a :b | a key <= b key]) asArray
]

Extension { #name : 'ArrayedCollection' }

{ #category : '*Collections-Protocol' }
ArrayedCollection class >> withAll: aCollection [
	"One of the few pieces of class-side collection protocol that ported
	 code assumes.  Array withAll: aSet is how a Set becomes an Array
	 without going through asOrderedCollection first."
	| result index |
	result := self new: aCollection size.
	index := 0.
	aCollection do: [:each | result at: (index := index + 1) put: each].
	^result
]

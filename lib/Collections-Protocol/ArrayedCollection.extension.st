Extension { #name : 'ArrayedCollection' }

{ #category : '*Collections-Protocol' }
ArrayedCollection class >> withAll: aCollection [
	"One of the few pieces of class-side collection protocol that ported
	 code assumes.  Array withAll: aSet is how a Set becomes an Array
	 without going through asOrderedCollection first.  Fixed size, so the
	 size is taken first and the elements placed -- addAll: has nowhere
	 to put them."
	| result index |
	result := self new: aCollection size.
	index := 0.
	aCollection do: [:each | result at: (index := index + 1) put: each].
	^result
]

{ #category : '*Collections-Protocol' }
ArrayedCollection class >> with: a with: b with: c with: d with: e [
	"Five, where 1983 stops at four.

	 Overridden here as well as on Collection because an ArrayedCollection
	 is fixed size: the Collection version adds to a growing collection,
	 and `Array new add: 5' is not a thing.  1983 makes exactly this
	 distinction for one through four; this continues it."
	| made |
	made := self new: 5.
	made at: 1 put: a; at: 2 put: b; at: 3 put: c; at: 4 put: d; at: 5 put: e.
	^made
]

{ #category : '*Collections-Protocol' }
ArrayedCollection class >> with: a with: b with: c with: d with: e with: f [
	| made |
	made := self new: 6.
	made at: 1 put: a; at: 2 put: b; at: 3 put: c; at: 4 put: d;
	     at: 5 put: e; at: 6 put: f.
	^made
]

{ #category : '*Collections-Protocol' }
ArrayedCollection >> elementsForwardIdentityTo: otherArray [
	"Forward each of my elements to the element at the same index of that
	 array: every reference to the first becomes a reference to the second.

	 The primitive fails, and this reports why, when the two are not the
	 same size or when an element cannot be forwarded.  An object cannot be
	 forwarded if it is a guaranteed pointer, or if the VM holds it in a
	 register with no setter -- a context or method a worker is executing
	 in, the display form.  Those are refusals rather than crashes on
	 purpose: the alternative is a dangling pointer inside the interpreter."
	<primitive: 249>
	self size = otherArray size
		ifFalse: [^self error: 'elementsForwardIdentityTo: needs two collections of the same size'].
	^self error: 'elementsForwardIdentityTo: cannot forward one of these objects'
]

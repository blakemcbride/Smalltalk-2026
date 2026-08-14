"
Two sorted collections holding the same elements are equal.
"
Extension { #name : 'SortedCollection' }

{ #category : '*Collections-Protocol' }
SortedCollection >> = aCollection [
	"Same class, same elements, same order.

	 1983 also requires `sortBlock = aSortedCollection sortBlock', and a
	 block's = is identity -- so two collections built separately, sorted
	 identically, holding identical elements, are never equal, because the
	 two [:a :b | a <= b] blocks are two objects.  That is a distinction
	 without a difference as far as the CONTENTS go, and it is not what any
	 later dialect means by =.

	 The sort block still matters for what a collection DOES; it just does
	 not make two collections with the same elements in the same order
	 unequal."
	self class == aCollection class ifFalse: [^false].
	self size = aCollection size ifFalse: [^false].
	1 to: self size do: [:i |
		(self at: i) = (aCollection at: i) ifFalse: [^false]].
	^true
]

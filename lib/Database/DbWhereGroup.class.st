"
A node of a WHERE tree: a list of conditions joined by AND, or by OR.

The tree exists because `a AND (b OR c)' is not `a AND b OR c' and a builder
that kept conditions in one flat list could only produce the second.  Kiss's
startOr/endOr open and close a group; this is what they open and close.

A LEAF is an Association -- the condition text, associated with the Array of
values its question marks want.  A BRANCH is another of these.  Keeping the
values with the text they belong to is what makes the tree safe to rearrange:
the parameters come out in whatever order the text is finally written in,
because they are carried by the text rather than by a list beside it, and
that is the bug this shape exists to make impossible.
"
Class {
	#name : 'DbWhereGroup',
	#superclass : 'Object',
	#instVars : [ 'conjunction', 'children', 'parent' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbWhereGroup class >> and [
	^self new setConjunction: 'AND'
]

{ #category : 'instance creation' }
DbWhereGroup class >> or [
	^self new setConjunction: 'OR'
]

{ #category : 'private' }
DbWhereGroup >> setConjunction: aString [
	conjunction := aString.
	children := OrderedCollection new
]

{ #category : 'accessing' }
DbWhereGroup >> conjunction [ ^conjunction ]

{ #category : 'accessing' }
DbWhereGroup >> children [ ^children ]

{ #category : 'accessing' }
DbWhereGroup >> parent [ ^parent ]

{ #category : 'accessing' }
DbWhereGroup >> parent: aGroup [ parent := aGroup ]

{ #category : 'accessing' }
DbWhereGroup >> isEmpty [ ^children isEmpty ]

{ #category : 'building' }
DbWhereGroup >> add: aCondition with: anArrayOfValues [
	children add: aCondition -> anArrayOfValues
]

{ #category : 'building' }
DbWhereGroup >> addGroup: aGroup [
	aGroup parent: self.
	children add: aGroup.
	^aGroup
]

{ #category : 'building' }
DbWhereGroup >> writeOn: aStream parameters: aCollection [
	"Write this group's SQL, adding its values to aCollection IN THE ORDER
	 THE QUESTION MARKS ARE WRITTEN.  That correspondence is the only thing
	 that makes a bound parameter go where it was meant to, and it is
	 maintained here by doing both in one pass rather than in two."

	| first |
	first := true.
	children do:
		[:each |
		first ifFalse: [aStream nextPut: $ ; nextPutAll: conjunction; nextPut: $ ].
		first := false.
		(each isKindOf: DbWhereGroup)
			ifTrue:
				[each isEmpty ifFalse:
					[aStream nextPut: $(.
					 each writeOn: aStream parameters: aCollection.
					 aStream nextPut: $)]]
			ifFalse:
				[aStream nextPutAll: each key.
				 each value do: [:value | aCollection add: value]]]
]

{ #category : 'printing' }
DbWhereGroup >> printOn: aStream [
	| parameters |
	parameters := OrderedCollection new.
	aStream nextPutAll: 'a DbWhereGroup('.
	self writeOn: aStream parameters: parameters.
	aStream nextPut: $)
]

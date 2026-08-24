"
One foreign key: which columns of which table point at which columns of which
other table.

Composite keys are the ordinary case here rather than a special one.  A
two-column key is two columns in each list, matched pairwise and joined with
AND, and every method below reads the lists rather than a single column --
which is why the composite case cannot rot: there is no single-column path
for it to diverge from.

An edge is UNDIRECTED for finding a path and DIRECTED for writing the ON
clause.  Both are true at once and neither is a compromise: whether two
tables can be joined does not depend on which of them holds the key, but the
condition that joins them names the columns in their own tables and does.
"
Class {
	#name : 'DbForeignKey',
	#superclass : 'Object',
	#instVars : [ 'fromTable', 'fromColumns', 'toTable', 'toColumns' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbForeignKey class >> from: aTable column: aColumn to: anotherTable column: anotherColumn [
	^self from: aTable columns: (Array with: aColumn)
		to: anotherTable columns: (Array with: anotherColumn)
]

{ #category : 'instance creation' }
DbForeignKey class >> from: aTable columns: someColumns to: anotherTable columns: otherColumns [
	someColumns size = otherColumns size ifFalse:
		[^DbError signal: 'a foreign key must name the same number of ',
			'columns on both sides'].
	^self new setFrom: aTable columns: someColumns to: anotherTable columns: otherColumns
]

{ #category : 'private' }
DbForeignKey >> setFrom: aTable columns: someColumns to: anotherTable columns: otherColumns [
	"Names are folded to lower case here and nowhere else.

	 One place, because a graph half of whose names came from a driver that
	 upper-cases and half from a program that did not would have two nodes
	 for one table and no path between them."

	fromTable := aTable asLowercase.
	fromColumns := (someColumns collect: [:each | each asLowercase]) asArray.
	toTable := anotherTable asLowercase.
	toColumns := (otherColumns collect: [:each | each asLowercase]) asArray
]

{ #category : 'accessing' }
DbForeignKey >> fromTable [ ^fromTable ]

{ #category : 'accessing' }
DbForeignKey >> fromColumns [ ^fromColumns ]

{ #category : 'accessing' }
DbForeignKey >> toTable [ ^toTable ]

{ #category : 'accessing' }
DbForeignKey >> toColumns [ ^toColumns ]

{ #category : 'accessing' }
DbForeignKey >> isComposite [
	^fromColumns size > 1
]

{ #category : 'accessing' }
DbForeignKey >> otherTable: aTableName [
	"The end of this key that is not the one named.

	 A self-join -- a table whose foreign key points at itself -- answers
	 the same name, which is correct: both ends are that table."

	| name |
	name := aTableName asLowercase.
	name = fromTable ifTrue: [^toTable].
	name = toTable ifTrue: [^fromTable].
	^DbError signal: aTableName, ' is not an end of this foreign key'
]

{ #category : 'building' }
DbForeignKey >> onConditionFrom: fromAlias to: toAlias [
	"The ON clause joining the two ends, under whatever names they are going
	 by in this query.  Nil for an alias means the table's own name."

	| here there stream |
	here := fromAlias isNil ifTrue: [fromTable] ifFalse: [fromAlias].
	there := toAlias isNil ifTrue: [toTable] ifFalse: [toAlias].
	stream := WriteStream on: (String new: 64).
	fromColumns doWithIndex:
		[:each :i |
		i > 1 ifTrue: [stream nextPutAll: ' AND '].
		stream nextPutAll: here; nextPut: $.; nextPutAll: each;
			nextPutAll: ' = ';
			nextPutAll: there; nextPut: $.; nextPutAll: (toColumns at: i)].
	^stream contents
]

{ #category : 'comparing' }
DbForeignKey >> = anObject [
	self class == anObject class ifFalse: [^false].
	^fromTable = anObject fromTable
		and: [toTable = anObject toTable
		and: [fromColumns = anObject fromColumns
		and: [toColumns = anObject toColumns]]]
]

{ #category : 'comparing' }
DbForeignKey >> hash [
	^(fromTable hash bitXor: toTable hash) bitXor: fromColumns hash
]

{ #category : 'printing' }
DbForeignKey >> printOn: aStream [
	aStream nextPutAll: fromTable; nextPut: $(.
	fromColumns doWithIndex:
		[:each :i | i > 1 ifTrue: [aStream nextPutAll: ', ']. aStream nextPutAll: each].
	aStream nextPutAll: ') -> '; nextPutAll: toTable; nextPut: $(.
	toColumns doWithIndex:
		[:each :i | i > 1 ifTrue: [aStream nextPutAll: ', ']. aStream nextPutAll: each].
	aStream nextPut: $)
]

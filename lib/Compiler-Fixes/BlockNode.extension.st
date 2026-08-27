Extension { #name : 'BlockNode' }

{ #category : '*Compiler-Fixes' }
BlockNode >> declareTemporaries: variableNodes [
	"Take these on as my block temporaries.

	 They are stored as BlockTemporaryNodes at the head of my statements --
	 assignments of nil, one per name -- because that is both what has to be
	 EXECUTED on every entry and what has to be PRINTED as a declaration.
	 See BlockTemporaryNode for why the nil is not optional."
	variableNodes isEmpty ifTrue: [^self].
	statements := (variableNodes collect: [:each |
		BlockTemporaryNode new variable: each value: NodeNil]) asOrderedCollection
			, statements
]

{ #category : '*Compiler-Fixes' }
BlockNode >> blockTemporaryNodes [
	"My leading statements that declare temporaries, in the order declared."
	| temps |
	temps := OrderedCollection new.
	statements do: [:each |
		(each isKindOf: BlockTemporaryNode)
			ifFalse: [^temps].
		temps addLast: each].
	^temps
]

{ #category : '*Compiler-Fixes' }
BlockNode >> printArgumentsOn: aStream indent: level [
	"1983's, plus the temporaries between a second pair of bars.

	 Without this a block that declared `| v |' printed back as `v _ nil.'
	 with nothing declaring v, so the pretty printer produced source the
	 compiler would then read as an undeclared variable -- the same shape of
	 fault as a Float printing as `1e+16' and reading back as an addition."
	| temps |
	temps := self blockTemporaryNodes.
	(arguments size = 0 and: [temps isEmpty]) ifTrue: [^self].
	arguments isEmpty ifFalse: [
		arguments do: [:arg |
			aStream nextPut: $:.
			aStream nextPutAll: arg key.
			aStream space].
		aStream nextPutAll: '| '].
	temps isEmpty ifFalse: [
		aStream nextPutAll: '| '.
		temps do: [:each |
			aStream nextPutAll: each variable key.
			aStream space].
		aStream nextPutAll: '| '].
	"If >0 args and >1 statement, put all statements on separate lines"
	statements size - temps size > 1 ifTrue: [aStream crtab: level]
]

{ #category : '*Compiler-Fixes' }
BlockNode >> printStatementsOn: aStream indent: level [
	"1983's, starting past the temporary declarations, which
	 printArgumentsOn:indent: has already written as a declaration."
	| len shown thisStatement first |
	comment == nil
		ifFalse: [
			self printCommentOn: aStream indent: level.
			aStream crtab: level].
	first := self blockTemporaryNodes size + 1.
	len := shown := statements size.
	(level = 1 and: [statements last isReturnSelf])
		ifTrue: [shown := 1 max: shown - 1]
		ifFalse: [
			(len = 1 and: [((statements at: 1) == NodeNil) & (arguments size = 0)])
				ifTrue: [shown := shown - 1]].
	first to: shown do: [:i |
		thisStatement := statements at: i.
		thisStatement == NodeSelf ifFalse: [
			thisStatement printOn: aStream indent: level.
			i < shown ifTrue: [aStream nextPut: $.; crtab: level].
			thisStatement comment size > 0 ifTrue: [
				i = shown ifTrue: [aStream crtab: level].
				thisStatement printCommentOn: aStream indent: level.
				i < shown ifTrue: [aStream crtab: level]]]]
]

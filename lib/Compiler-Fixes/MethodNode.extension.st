"
The Browser's format keeps a method's pragmas.

1983's MethodNode>>printOn: prints a primitive and knows nothing else in angle
brackets, because 1983 had nothing else.  Parser>>method:context: reads
`<test>', `<shared: #serialize>' and the rest and keeps them, in the method's
literal frame (see Parser>>rememberPragmas:), and printOn: never asked for
them -- so formatting a method dropped every pragma it had, and accepting the
result made a test that was not one and a REST service method that was no
longer serialized (Bugs5 COMP-6).
"
Extension { #name : 'MethodNode' }

{ #category : '*Compiler-Fixes' }
MethodNode >> printOn: aStream [
	"1983's, plus the pragmas after the primitive, each on a line of its
	 own and with its arguments printed as literals."

	| args |
	precedence = 1
		ifTrue: [aStream nextPutAll: self selector]
		ifFalse: [
			args := ReadStream on: arguments.
			self selector keywords do: [:s |
				aStream nextPutAll: s.
				aStream space; nextPutAll: args next key.
				aStream space]].
	comment == nil ifFalse: [
		aStream crtab: 1.
		self printCommentOn: aStream indent: 1.
		aStream cr].
	temporaries size > 0 ifTrue: [
		aStream crtab: 1.
		aStream nextPutAll: '| '.
		temporaries do: [:s | aStream nextPutAll: s key. aStream space].
		aStream nextPut: $|].
	primitive > 0 ifTrue: [
		aStream crtab: 1.
		aStream nextPutAll: '<primitive: '; print: primitive; nextPutAll: '>'].
	encoder pragmaSpecs do: [:spec |
		aStream crtab: 1.
		self printPragma: spec on: aStream].
	aStream crtab: 1.
	^block printStatementsOn: aStream indent: 1
]

{ #category : '*Compiler-Fixes' }
MethodNode >> printPragma: spec on: aStream [
	"One pragma as Parser>>pragma read it: an Array of the keyword Symbol
	 and its arguments.  `<test>' is a one-element Array."

	| keywords |
	aStream nextPut: $<.
	spec size = 1
		ifTrue: [aStream nextPutAll: (spec at: 1)]
		ifFalse: [
			keywords := (spec at: 1) keywords.
			1 to: keywords size do: [:i |
				i > 1 ifTrue: [aStream space].
				aStream nextPutAll: (keywords at: i); space.
				LiteralNode print: (spec at: i + 1) on: aStream]].
	aStream nextPut: $>
]

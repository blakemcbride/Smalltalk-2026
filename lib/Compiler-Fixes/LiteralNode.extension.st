"
A literal the Browser's format prints is read back as the same literal.

1983's LiteralNode printed its value with storeOn:, which answers an
expression that rebuilds an equal object and is not, in general, a literal.
Formatting every method in the image and recompiling what format printed
found three ways that changed a method (Bugs5 COMP-6):

	#'hello world'        storeOn: gives `'hello world' asSymbol' -- a
	                      message send, and in a literal array a String
	#(true #[1])          an Array holding a ByteArray is not `isLiteral',
	                      so storeOn: gives `((Array new: 2) at: 1 put: ...
	                      yourself)', a new Array on every evaluation
	#(#true)              a Symbol inside an Array prints bare, and bare
	                      `true' in a literal array is the Boolean

So a literal is printed here, in the syntax the C compiler and the image's
own Scanner both read: a Symbol with its `#', quoted when its characters are
not a selector; an Array as `#( )' with every element printed the same way;
a ByteArray as `#[ ]'; anything else -- numbers, Strings, Characters, true,
false and nil -- with storeOn:, which for those is the literal.
"
Extension { #name : 'LiteralNode' }

{ #category : '*Compiler-Fixes' }
LiteralNode class >> print: anObject on: aStream [
	"anObject written as a literal: what this class's comment describes.
	 Also what MethodNode prints a pragma's arguments with."

	anObject isSymbol ifTrue: [
		aStream nextPut: $#.
		(self isBareSymbol: anObject)
			ifTrue: [aStream nextPutAll: anObject]
			ifFalse: [anObject asString storeOn: aStream].
		^self].
	anObject class == Array ifTrue: [
		aStream nextPutAll: '#('.
		anObject do: [:each | self print: each on: aStream] separatedBy: [aStream space].
		aStream nextPut: $).
		^self].
	anObject class == ByteArray ifTrue: [
		aStream nextPutAll: '#['.
		anObject do: [:each | aStream print: each] separatedBy: [aStream space].
		aStream nextPut: $].
		^self].
	anObject storeOn: aStream
]

{ #category : '*Compiler-Fixes' }
LiteralNode class >> isBareSymbol: aSymbol [
	"Whether aSymbol can be written as `#' and its characters: a run of
	 binary characters, a unary selector, or keywords, each word ASCII
	 letters and digits beginning with a letter.  Anything else is quoted,
	 which both compilers read for every Symbol -- so this may say no to a
	 Symbol that would have been fine bare, and never yes to one that
	 would not."

	| binary i word |
	aSymbol isEmpty ifTrue: [^false].
	binary := '+-*/\<>=~@%&?!,'.
	(binary includes: aSymbol first) ifTrue: [
		aSymbol do: [:c | (binary includes: c) ifFalse: [^false]].
		^true].
	word := [:c | c asciiValue < 128 and: [c isLetter or: [c isDigit]]].
	i := 1.
	[i <= aSymbol size] whileTrue: [
		((aSymbol at: i) asciiValue < 128 and: [(aSymbol at: i) isLetter]) ifFalse: [^false].
		[i <= aSymbol size and: [word value: (aSymbol at: i)]] whileTrue: [i := i + 1].
		"A word that ends the Symbol is unary only if no keyword came before it."
		i > aSymbol size ifTrue: [^(aSymbol includes: $:) not].
		(aSymbol at: i) = $: ifFalse: [^false].
		i := i + 1].
	^true
]

{ #category : '*Compiler-Fixes' }
LiteralNode >> printOn: aStream indent: level [
	LiteralNode print: key on: aStream
]

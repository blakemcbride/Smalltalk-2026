Extension { #name : 'ClassDescription' }

{ #category : '*Compiler-Fixes' }
ClassDescription >> compile: code notifying: requestor trailer: bytes ifFail: failBlock [
	"1983's, which is Behavior's plus the change log, with the parse tree
	 and its generate: replaced by the C compiler.  See the Behavior
	 extension in this package for the whole argument.

	 The Cursor and the change-log entries are 1983's and are kept: a
	 Browser accept still shows the working cursor and still records
	 whether the selector was new."

	| answer method selector |
	Cursor execute showWhile: [
		answer := Compiler
			compileSource: code
			for: self
			noPattern: false
			declaringUndeclared: requestor isNil.
		method := answer at: 1.
		method isNil ifTrue: [
			Compiler reportFailure: answer of: code to: requestor.
			^failBlock value].
		selector := answer at: 2.
		(methodDict includesKey: selector)
			ifTrue: [Smalltalk changes changeSelector: selector class: self]
			ifFalse: [Smalltalk changes addSelector: selector class: self].
		Compiler put: bytes intoTrailerOf: method.
		self addSelector: selector withMethod: method].
	^selector
]

{ #category : '*Compiler-Fixes' }
ClassDescription >> compile: code notifying: requestor [
	"compile:notifying:, filed under `as yet unclassified'.

	 Behavior's version installs the method and nothing else, which is all a
	 bare Behavior can do.  A class has an organization and a source file,
	 and a method that is in neither is a method the Browser cannot show and
	 fileOut cannot write -- so for a class this goes through
	 compile:classified:notifying:, which does both.  `as yet unclassified'
	 is the heading Squeak and Pharo use for exactly this."
	^self compile: code
		  classified: 'as yet unclassified'
		  notifying: requestor
]

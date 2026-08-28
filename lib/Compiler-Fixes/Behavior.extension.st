"
One compiler, reached from the image.

This is the seam.  Everything that defines a method at run time -- the
Browser accepting one, TonelReader loading a service file into a running
server, Behavior>>recompile:from:, a test calling basicCompile: -- funnels
through `compile:notifying:trailer:ifFail:', on Behavior or on
ClassDescription.  1983's version builds a parse tree with the compiler
written in Smalltalk and asks the tree to generate code.  These two ask
primitive 228 instead, which is the C compiler that built the image.

WHY.  The two compilers did not agree, and the disagreement was silent.  The
C one gives blocks real closures; 1983's gives them Chapter 27's
BlockContext, which reads a variable as it is NOW rather than as it was.  So

	(1 to: 3) collect: [:i | [i]]

answered 1, 2, 3 when it was compiled INTO the image and 3, 3, 3 when the
same text was recompiled inside it -- and a Tonel service file that worked
when the image was built could quietly mean something else after the hot
reload the REST server exists for.  Writing closures a second time in
Smalltalk would have made two implementations of one semantics, which is
what the eighty-six unreparseable methods were: two implementations that had
stopped agreeing.  One code generator cannot disagree with itself.

WHAT IS NOT ROUTED THROUGH C.  The 1983 Parser stays, and is still what
parseSelector:, parseArgsAndTemps:, the pretty printer and the Decompiler
use.  If it and the C compiler drift apart the symptom is a TOOL complaining
about a method that compiles -- visible, and not a wrong answer.

THE SYMBOL LOCK is held around the primitive and has to be: interning on the
C side goes through the bootstrap's table, which has no lock of its own,
while Symbol class>>intern: holds LibraryLocks' Symbol mutex.  Two workers
interning one new selector make two Symbols, and a Symbol that is not
identical to itself cannot be found as a selector.  That serialises
compiling, which is not a hot path.
"
Extension { #name : 'Behavior' }

{ #category : '*Compiler-Fixes' }
Behavior >> compile: code notifying: requestor trailer: bytes ifFail: failBlock [
	"Compile code for me and install it, answering the selector.

	 1983's shape exactly -- compile, take the selector, install with the
	 trailer -- with the parse tree and its generate: replaced by the C
	 compiler.  See this package's Behavior comment for why."

	| answer method selector |
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
	Compiler put: bytes intoTrailerOf: method.
	self addSelector: selector withMethod: method.
	^selector
]

{ #category : '*Compiler-Fixes' }
Behavior >> compileOnly: code notifying: requestor trailer: bytes ifFail: failBlock into: aTwoArgumentBlock [
	"Compile code for me and do NOT install it: hand the selector and the
	 CompiledMethod to aTwoArgumentBlock and answer its value.  The same
	 compile as compile:notifying:trailer:ifFail: above, without the
	 addSelector:withMethod: at the end.

	 For a caller that has several methods to install or none: TonelReader
	 compiles every method in a file this way and installs them together
	 once all of them have compiled, so that a mistake in one leaves the
	 class as it was (Bugs3 B63).  Kept beside compile:notifying:trailer:
	 ifFail: rather than folded into it because that one is the seam every
	 run-time compile goes through and its shape is 1983's."

	| answer method |
	answer := Compiler
		compileSource: code
		for: self
		noPattern: false
		declaringUndeclared: requestor isNil.
	method := answer at: 1.
	method isNil ifTrue: [
		Compiler reportFailure: answer of: code to: requestor.
		^failBlock value].
	Compiler put: bytes intoTrailerOf: method.
	^aTwoArgumentBlock value: (answer at: 2) value: method
]

{ #category : '*Compiler-Fixes' }
Behavior >> compile: code [
	"Compile code for me and install it, answering the selector.

	 The Blue Book gives this and compile:notifying: as THE spelling for
	 adding a method programmatically, and neither existed here: only
	 compile:classified: and compile:notifying:trailer:ifFail:, so the
	 documented one-argument form was a doesNotUnderstand.  Squeak and
	 Pharo both answer it, and generated code and tests reach for it first."
	^self compile: code notifying: nil
]

{ #category : '*Compiler-Fixes' }
Behavior >> compile: code notifying: requestor [
	"compile:, with somewhere to report a syntax error.  The Blue Book's
	 own body: the default trailer and a failure that answers nil."
	^self compile: code notifying: requestor trailer: #(0 0 0) ifFail: [^nil]
]

"
One compiler, reached from the image.

This is the seam.  Everything that defines a method at run time -- the
Browser accepting one, TonelReader loading a service file into a running
server, a reshape recompiling a class, a test calling basicCompile: --
funnels through one of the three methods below, on Behavior or on
ClassDescription.  1983's versions build a parse tree with the compiler
written in Smalltalk and ask the tree to generate code.  These ask
primitive 228 instead, which is the C compiler that built the image.

THREE, and for a while this comment said one.  It named
`compile:notifying:trailer:ifFail:' and claimed everything went through
it, and two things did not: recompile:from: and compileUnchecked: reached
`compilerClass new compile: ... in: self' and `generate:' directly.  The
seam had three doors and one of them was guarded.  What made that visible
was adding an instance variable to a class -- ClassDescription>>validate-
From:in:instanceVariableNames:methods: sends compileAllFrom:, which
recompiles every method of the class and of every subclass, and then
migrates the instances -- and every capturing block in all of them
silently went back to Chapter 27:

	Ctr compile: 'blocks ^(1 to: 3) collect: [:i | [i]]'  -->  (1 2 3)
	Ctr recompile: #blocks                                -->  (3 3 3)

Nothing was reported.  The class still worked, the methods still ran, and
one of them meant something else.  Bugs4 REFLECTION-1; the same
disagreement Bugs1 B15 found in the Browser, arriving through the door
that fix did not close.

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

Six callers still build a 1983 parse tree, and not one of them DEFINES a
method, which is the line this comment now draws.  Explainer,
CodeController (format), ContextPart>>tempNames and Debugger>>
computeSourceMap ask `parse:in:notifying:' for names and a source map and
throw the tree away.  MethodDefinitionChange>>accept:notifying: generates
code only to find out whether the text compiles, and discards the method.
Debugger>>acceptText: re-generates a method it has ALREADY installed
through the seam, because a quick method has no frame to restart in, and
puts the copy in the context rather than in a method dictionary.  A parse
is not a definition; the rule is that nothing enters a method dictionary
except from primitive 228.

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
Behavior >> recompile: selector from: oldClass [
	"Compile oldClass's source for selector again, for me, and install it
	 with the trailer the old method had -- so no new source is written and
	 the method still points at the text it came from.

	 1983's shape exactly, with `compilerClass new compile: ... in: self'
	 and `generate:' replaced by the C compiler.  This is the second door
	 into the seam and was not guarded; see this package's Behavior comment
	 for what came through it.  oldClass differs from self only while a
	 class is being reshaped, when self's instance variables have already
	 changed and the source still has to be read from the class that has
	 the old ones.

	 THE FAILURE ARM IS NOT 1983's, and cannot be.  1983 answered
	 `^self recompile: selector' -- retry after the person proceeds from
	 the SyntaxError notifier -- which with no notifier and a source that
	 will never compile is unbounded recursion, not a hang in some tool but
	 a reshape that never returns.  A failure is reported the way every
	 other compile in this package reports one, which with no requestor is
	 a SyntaxErrorNotification, and the method already installed is left
	 alone."

	| method trailer answer |
	method := oldClass compiledMethodAt: selector.
	trailer := (method size - 2 to: method size) collect: [:i | method at: i].
	answer := Compiler
		compileSource: (oldClass sourceCodeAt: selector)
		for: self
		noPattern: false
		declaringUndeclared: true.
	(answer at: 1) isNil ifTrue: [
		^Compiler
			reportFailure: answer
			of: (oldClass sourceCodeAt: selector)
			to: nil].
	selector == (answer at: 2) ifFalse: [^self error: 'selector changed!'].
	Compiler put: trailer intoTrailerOf: (answer at: 1).
	self addSelector: selector withMethod: (answer at: 1)
]

{ #category : '*Compiler-Fixes' }
Behavior >> compileUnchecked: code [
	"Compile and install without asking whether the method dictionary may
	 have it, because the caller is the machinery that decides that.

	 1983's, with the parse tree and its generate: replaced by the C
	 compiler, for the same reason as every other method here: this is the
	 third and last thing in the image that puts a method into a method
	 dictionary from text.  Only the multiple-inheritance machinery
	 (compileBroadcastCodeFor:, the directed-method and copied-method
	 installers in Behavior) sends it, and none of that is reachable in a
	 built image today -- which is exactly why it is worth routing rather
	 than excepting.  A door nothing walks through is a door nobody
	 notices has been left open.

	 THE SOURCE IS RECORDED, which 1983's did not do, and the reason is the
	 routing above.  A method with no source is recovered by DECOMPILING it,
	 and the decompiler is 1983's: it reads 1983's block bytecodes and
	 answers `unusedBytecode' on a closure.  So a method installed here and
	 then reached by recompile:from: -- which is every method of a class
	 somebody adds an instance variable to -- used to come back as text and
	 now would not, and the reshape would raise in the middle.  Behavior>>
	 putSource:inFile: is 1983's own method for precisely this case, a
	 Behavior with no organization to classify into, and nothing called it
	 until now."

	| answer selector |
	answer := Compiler
		compileSource: code
		for: self
		noPattern: false
		declaringUndeclared: true.
	(answer at: 1) isNil ifTrue: [
		Compiler reportFailure: answer of: code to: nil.
		^nil].
	selector := answer at: 2.
	self addSelectorUnchecked: selector withMethod: (answer at: 1).
	(methodDict at: selector) putSource: code asString inFile: 2.
	^selector
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

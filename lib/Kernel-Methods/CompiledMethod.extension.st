"
How much stack a method has, counted once.

numStack answered 'frameSize - numTemps - numArgs', and numTemps already
counts the arguments -- the same mistake as MethodContext>>restart and for
the same reason.  For BlockClosure>>on:do: it answered 7 where the frame
has 9 slots free.

What the compiler knows about a method and the method does not record --
which slot of which frame holds which name, which characters each send
came from -- is had by compiling the method's source again with the C
compiler, the one that laid out the bytes that run (primitive 228).  The
answer is believed only for a source that compiles to this method's own
header and bytecodes: a method whose source has changed since, or was
decompiled, or was made by the 1983 compiler, gets nil, and a debugger
then shows less rather than something wrong (Bugs07 X-26, X-27).
"
Extension { #name : 'CompiledMethod' }

{ #category : '*Kernel-Methods-Fixes' }
CompiledMethod >> numStack [
	"Answer the size of the available stack."
	self isQuick ifTrue: [^0].
	^self frameSize - self numTemps
]

{ #category : '*Kernel-Methods-Fixes' }
CompiledMethod >> recompiledFrom: source in: aClass [
	"What primitive 228 answers for source compiled in aClass -- the
	 Array of eight Compiler class>>primCompile:... describes -- when it
	 compiles to my header and my bytecodes, else nil.  See the comment
	 above."
	| answer compiled |
	(source isString and: [source notEmpty]) ifFalse: [^nil].
	answer := [Compiler
			compileSource: source
			for: aClass
			noPattern: false
			declaringUndeclared: false]
		on: Error do: [:e | e return: nil].
	(answer isNil or: [answer size < 8]) ifTrue: [^nil].
	compiled := answer at: 1.
	compiled isNil ifTrue: [^nil].
	((compiled objectAt: 1) = (self objectAt: 1)
		and: [compiled endPC = self endPC]) ifFalse: [^nil].
	self initialPC to: self endPC do: [:i |
		(compiled at: i) = (self at: i) ifFalse: [^nil]].
	((answer at: 7) isString and: [(answer at: 8) isString]) ifFalse: [^nil].
	^answer
]

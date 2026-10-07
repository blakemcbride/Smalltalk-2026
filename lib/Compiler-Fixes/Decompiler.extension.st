"
The Decompiler, taught the code this system's compiler actually writes.

1983's Decompiler reads the bytecodes 1983's compiler wrote, and recovers
control structures from the PLACES that compiler put its jumps.  Every
method in this image is compiled by the C compiler behind primitive 228,
which writes the same instructions in different places, and adds the
closure instructions 1983 never had.  So 1,646 of the image's 6,371
methods failed to decompile -- `this collection is empty' from Object>>
isKindOf:, `unusedBytecode' from every method holding a closure -- while
the manual said the decompiler worked, and Behavior>>sourceCodeForMethod:at:
falls back to it for any method with no source (Bugs5 COMP-7).

WHAT IS DIFFERENT.  The C compiler compiles every inlined message for its
VALUE, and the layouts are fixed (src/compiler/compiler.c, compile_inline_
conditional, compile_inline_and_or, compile_inline_while):

    cond  jumpIf L1  arm1  jump L2  L1: arm2  L2:          ifTrue:ifFalse:
    cond  jumpIf L1  arm1  jump L2  L1: nil   L2:          ifTrue: alone
    cond  jumpIf L1  arm1  jump L2  L1: false L2:          and:  (true: or:)
    L0: test  jumpIf L2  body pop  jump L0  L2: [nil]      whileTrue:

and an arm that returns is still followed by its jump, and a statement
whose value is not wanted is still followed by its pop, though neither can
run.  1983's compiler left those out, and 1983's Decompiler depended on
their absence: Object>>isKindOf: is `ifTrue: [^true] ifFalse: [^...]',
whose dead `pop' after the conditional found nothing to pop.

So the structures are recovered from the C layout, which is simpler than
1983's because it is uniform: every conditional's first arm ends in the
jump over the second, and that jump runs backwards exactly when the
conditional is a loop's exit.  A conditional is always pushed as a value,
as the compiler treated it, and whatever follows -- a pop, a store, a
send -- consumes it as it did when it ran.  Anything after a return in the
same run of code is skipped, because it is the dead glue above and nothing
can jump into it.  The loop's trailing nil is the one thing the compiler
sometimes takes back (discard_statement_value); when it is there the loop
is the value it was compiled as, and when it is not the loop was a
statement.  A nil that belongs to the NEXT statement is read as the loop's
own -- the loop answers nil, so the method decompiled means the same.

CLOSURES.  Bytecode 143 is a closure whose copied values were pushed just
before it and whose body follows; 140-142 reach a temporary held in a
vector shared with blocks; 138 makes such a vector, or, with its top bit
set, a brace array from the values on the stack.  A closure's body is
decompiled in a frame of its own -- arguments, then the copied values
(which ARE the outer variables, so they print with the outer names), then
its vector, then its local temporaries -- and the temporaries it uses are
declared in the block.  The nils that start each local are read as values
pushed and left behind, and removed when the body ends.

Variables are named t1, t2, ... in the order a frame first needs them, as
1983 named a method's temporaries; source is what carries real names.

One instance variable is not 1983's meaning any longer: lastJumpPc served
only the jump analysis this file replaces, and counts temporary names
here.  Decompiler is a 1983 class whose definition this library does not
redefine, and a counter needs somewhere to live that is the instance's
own: a class variable would be shared by two workers decompiling at once.
"
Extension { #name : 'Decompiler' }

{ #category : '*Compiler-Fixes' }
Decompiler >> decompile: aSelector in: aClass method: aMethod using: aConstructor [
	"Answer a parse tree, a MethodNode, for aMethod.  1983's, with the
	 method's prologue -- the vector a closure-dialect method makes for the
	 temporaries its blocks share -- read before the body, and the method's
	 temporaries those that the decompiled body names."
	| block vectorSlot vars |
	constructor := aConstructor.
	method := aMethod.
	self initSymbols: aClass.
	lastJumpPc := tempVars size.
	tempVars := tempVars , (Array new: 256 - tempVars size).
	vectorSlot := nil.
	method isQuick
		ifTrue: [block := self quickMethod]
		ifFalse: [
			stack := OrderedCollection new: method frameSize.
			statements := OrderedCollection new: 20.
			super method: method pc: method initialPC.
			vectorSlot := self methodPrologue.
			block := self blockTo: method endPC + 1.
			stack isEmpty ifFalse: [self error: 'stack not empty']].
	vars := OrderedCollection new.
	1 to: 256 do: [:i | | each |
		each := tempVars at: i.
		(vectorSlot notNil and: [i = (vectorSlot + 1)])
			ifTrue: [self vector: each do: [:node |
						(vars includes: node) ifFalse: [vars addLast: node]]]
			ifFalse: [(each isKindOf: ParseNode) ifTrue: [vars addLast: each]]].
	^constructor
		codeMethod: aSelector
		block: block
		tempVars: vars asArray
		primitive: method primitive
		class: aClass
]

{ #category : '*Compiler-Fixes' }
Decompiler >> methodPrologue [
	"Read the vector a closure-dialect method makes before its first
	 statement, if it makes one: `138 size' and a store into the slot that
	 holds it, then the arguments that live in it moved across.  Answer the
	 slot, or nil."
	| vector slot |
	(self vectorCreationAt: pc) ifFalse: [^nil].
	vector := Array new: (method at: pc + 1).
	pc := pc + 2.
	(method at: pc) // 8 = 13
		ifTrue: [slot := (method at: pc) - 104. pc := pc + 1]
		ifFalse: [
			((method at: pc) = 130 and: [(method at: pc + 1) // 64 = 1])
				ifFalse: [^self error: 'unrecognised method prologue'].
			slot := (method at: pc + 1) \\ 64.
			pc := pc + 2].
	tempVars at: slot + 1 put: vector.
	self moveArguments: method numArgs into: vector at: slot.
	^slot
]

{ #category : '*Compiler-Fixes' }
Decompiler >> vectorCreationAt: index [
	"Is there a vector made at index -- 138 with a size, not the brace
	 form, which has the top bit of its operand set?"
	^(method at: index) = 138 and: [(method at: index + 1) < 128]
]

{ #category : '*Compiler-Fixes' }
Decompiler >> moveArguments: argumentCount into: vector at: slot [
	"Skip the prologue's `push argument; popIntoRemote' pairs, which move
	 each argument a block shares and assigns into the vector, and make the
	 vector's entry the argument itself.  The compiler numbers a vector in
	 declaration order and arguments are declared first, so the k-th move
	 stores the k-th entry, from arguments in increasing order."
	| entry last argument length |
	entry := 0.
	last := -1.
	[length := 0.
	 (method at: pc) // 16 = 1
		ifTrue: [argument := (method at: pc) - 16. length := 1]
		ifFalse: [((method at: pc) = 128 and: [(method at: pc + 1) // 64 = 1])
			ifTrue: [argument := (method at: pc + 1) \\ 64. length := 2]].
	 length > 0
		and: [argument < argumentCount
		and: [argument > last
		and: [(method at: pc + length) = 142
		and: [(method at: pc + length + 1) = entry
		and: [(method at: pc + length + 2) = slot]]]]]]
			whileTrue: [
				vector at: entry + 1 put: (tempVars at: argument + 1).
				entry := entry + 1.
				last := argument.
				pc := pc + length + 3]
]

{ #category : '*Compiler-Fixes' }
Decompiler >> vector: vector do: aBlock [
	"The variables a vector holds that the code reached."
	vector do: [:each | each isNil ifFalse: [aBlock value: each]]
]

{ #category : '*Compiler-Fixes' }
Decompiler >> freshTemp [
	"A temporary with a name no other in this method has."
	lastJumpPc := lastJumpPc + 1.
	^constructor codeTemp: lastJumpPc - 1
]

{ #category : '*Compiler-Fixes' }
Decompiler >> tempAt: offset [
	"The variable in frame slot offset, made the first time it is asked
	 for: a closure's locals are not known until its body uses them."
	| node |
	node := tempVars at: offset + 1.
	node isNil ifTrue: [tempVars at: offset + 1 put: (node := self freshTemp)].
	^node
]

{ #category : '*Compiler-Fixes' }
Decompiler >> pushTemporaryVariable: offset [
	"1983's, through tempAt:, which makes a closure's local the first time
	 the body names it."
	stack addLast: (self tempAt: offset)
]

{ #category : '*Compiler-Fixes' }
Decompiler >> remote: index in: vectorSlot [
	"The variable at index in the vector in frame slot vectorSlot."
	| vector node |
	vector := tempVars at: vectorSlot + 1.
	(vector isKindOf: Array)
		ifFalse: [^self error: 'a remote temporary with no vector'].
	node := vector at: index + 1.
	node isNil ifTrue: [vector at: index + 1 put: (node := self freshTemp)].
	^node
]

{ #category : '*Compiler-Fixes' }
Decompiler >> interpretNextInstructionFor: client [
	"1983's dispatch, and the closure dialect's five instructions, which
	 1983's answers `unusedBytecode'.  Here rather than in
	 InstructionStream so that the other clients of the dispatch -- the
	 debugger's simulation, the instruction printer -- go on failing as
	 they always have and are not sent messages they do not implement."
	| byte operand |
	byte := method at: pc.
	byte = 138 ifTrue: [
		operand := method at: pc + 1.
		pc := pc + 2.
		^self pushNewArray: operand].
	(byte between: 140 and: 142) ifTrue: [
		operand := self remote: (method at: pc + 1) in: (method at: pc + 2).
		pc := pc + 3.
		stack addLast: operand.
		byte = 140 ifTrue: [^self].
		^self doStore: (byte = 141 ifTrue: [stack] ifFalse: [statements])].
	byte = 143 ifTrue: [
		operand := method at: pc + 1.
		pc := pc + 4.
		^self
			closureCopying: operand // 16
			arguments: operand \\ 16
			size: (method at: pc - 2) * 256 + (method at: pc - 1)].
	^super interpretNextInstructionFor: client
]

{ #category : '*Compiler-Fixes' }
Decompiler >> pushNewArray: operand [
	"A brace array: the elements are on the stack.  It decompiles as the
	 cascade Parser>>braceExpression builds for the same source,
	 `(Array new: n) at: 1 put: a; ...; yourself'.  A vector is made only
	 in a prologue, which is read before this could see it."
	| count elements receiver messages |
	operand < 128 ifTrue: [^self error: 'a temporary vector outside a prologue'].
	count := operand - 128.
	elements := Array new: count.
	count to: 1 by: -1 do: [:i | elements at: i put: stack removeLast].
	receiver := constructor
		codeMessage: (constructor codeAnyLitInd: (Smalltalk associationAt: #Array))
		selector: (constructor codeAnySelector: #new:)
		arguments: (Array with: (constructor codeAnyLiteral: count)).
	messages := OrderedCollection new.
	1 to: count do: [:i |
		messages addLast: (constructor
			codeCascadedMessage: (constructor codeAnySelector: #at:put:)
			arguments: (Array
				with: (constructor codeAnyLiteral: i)
				with: (elements at: i)))].
	messages addLast: (constructor
		codeCascadedMessage: (constructor codeAnySelector: #yourself)
		arguments: #()).
	stack addLast: (constructor codeCascade: receiver messages: messages asArray)
]

{ #category : '*Compiler-Fixes' }
Decompiler >> closureCopying: copiedCount arguments: argumentCount size: size [
	"A closure: copiedCount values were pushed before it, and its body is
	 the next size bytes, decompiled in a frame of its own.  See the class
	 comment for the frame's layout.  Push the BlockNode."
	| copied outerFrame frame arguments base bodyEnd stackPos blockPos returned
	  value block locals vectorSlot |
	copied := Array new: copiedCount.
	copiedCount to: 1 by: -1 do: [:i | copied at: i put: stack removeLast].
	bodyEnd := pc + size.
	outerFrame := tempVars.
	frame := Array new: 256.
	tempVars := frame.
	arguments := (1 to: argumentCount) collect: [:i | self freshTemp].
	1 to: argumentCount do: [:i | frame at: i put: (arguments at: i)].
	1 to: copiedCount do: [:i | frame at: argumentCount + i put: (copied at: i)].
	base := argumentCount + copiedCount.
	vectorSlot := nil.
	(self vectorCreationAt: pc) ifTrue: [
		vectorSlot := base.
		frame at: base + 1 put: (Array new: (method at: pc + 1)).
		base := base + 1.
		pc := pc + 2.
		self moveArguments: argumentCount into: (frame at: vectorSlot + 1) at: vectorSlot].
	stackPos := stack size.
	blockPos := statements size.
	returned := self scanTo: bodyEnd.
	value := (returned not and: [stack size > stackPos])
		ifTrue: [stack removeLast]
		ifFalse: [nil].
	"Whatever else is left is the nils that started the locals."
	[stack size > stackPos] whileTrue: [stack removeLast].
	value isNil ifFalse: [statements addLast: value].
	block := constructor codeBlock: (self popTo: blockPos) returns: returned.
	lastReturnPc := -1.
	locals := OrderedCollection new.
	base + 1 to: frame size do: [:i | | each |
		each := frame at: i.
		(each isKindOf: ParseNode) ifTrue: [locals addLast: each]].
	vectorSlot isNil ifFalse: [
		self vector: (frame at: vectorSlot + 1) do: [:each |
			(arguments includes: each) ifFalse: [locals addLast: each]]].
	block declareTemporaries: locals.
	tempVars := outerFrame.
	pc := bodyEnd.
	stack addLast: (constructor codeArguments: arguments block: block)
]

{ #category : '*Compiler-Fixes' }
Decompiler >> scanTo: end [
	"Decompile from pc up to end, leaving what it makes on the stack and in
	 statements.  Answer whether the run ended in a return; anything after
	 one is the compiler's dead glue -- a jump over an else, a pop of a
	 value that never arrives -- and is passed over."
	| here |
	[pc < end] whileTrue: [
		here := lastPc := pc.
		limit := end.
		self interpretNextInstructionFor: self.
		lastReturnPc = here ifTrue: [
			pc := end.
			lastPc := here.
			^true]].
	^false
]

{ #category : '*Compiler-Fixes' }
Decompiler >> statementsTo: end [
	"Decompile from pc up to end and answer the statements.  If a value is
	 left over it is the run's value and becomes the last statement, and
	 hasValue says so.  Leave pc = end."
	| blockPos stackPos returned |
	blockPos := statements size.
	stackPos := stack size.
	returned := self scanTo: end.
	(hasValue := returned not and: [stack size > stackPos])
		ifTrue: [statements addLast: stack removeLast].
	returned ifFalse: [lastReturnPc := -1].
	^self popTo: blockPos
]

{ #category : '*Compiler-Fixes' }
Decompiler >> methodReturnTop [
	"1983's, without the jump bookkeeping that scanTo: replaces."
	lastReturnPc := lastPc.
	statements addLast: stack removeLast
]

{ #category : '*Compiler-Fixes' }
Decompiler >> jump: dist [
	"Every unconditional jump the compiler writes is read as part of the
	 structure it belongs to -- the end of a conditional's first arm, a
	 loop's way back, the jump over a Blue Book block -- so one met on its
	 own is code this reading does not understand, and is said so rather
	 than decompiled into something that means something else."
	self error: 'a jump outside any structure'
]

{ #category : '*Compiler-Fixes' }
Decompiler >> jump: dist if: condition [
	"A conditional jump: an inlined ifTrue:ifFalse: (or one of its
	 shorter forms, which print from the same node), or the exit test of a
	 loop.  The jump at the end of the first arm says which; see the class
	 comment for the layouts."
	| condPc elseStart end skip byte skipDist cond trueBlock falseBlock thenBlock
	  elseBlock testPc test body node |
	condPc := lastPc.
	elseStart := pc + dist.
	end := limit.
	skip := elseStart - 2.
	byte := method at: skip.
	(byte between: 160 and: 167)
		ifFalse: [^self error: 'a conditional with no jump at the end of its arm'].
	skipDist := byte - 164 * 256 + (method at: skip + 1).
	cond := stack removeLast.
	skipDist < 0 ifTrue: [
		"A loop: the test runs from the jump's target up to here."
		testPc := elseStart + skipDist.
		pc := testPc.
		test := self statementsTo: condPc.
		test size - 1 timesRepeat: [statements removeLast].
		pc := condPc + 2.
		body := self statementsTo: skip.
		node := constructor
			codeMessage: (constructor codeBlock: test returns: false)
			selector: (constructor
				codeSelector: (body isEmpty
					ifTrue: [condition ifTrue: [#whileFalse] ifFalse: [#whileTrue]]
					ifFalse: [condition ifTrue: [#whileFalse:] ifFalse: [#whileTrue:]])
				code: #macro)
			arguments: (body isEmpty
				ifTrue: [#()]
				ifFalse: [Array with: (constructor codeBlock: body returns: false)]).
		pc := elseStart.
		(pc < end and: [(method at: pc) = 115])
			ifTrue: [pc := pc + 1. stack addLast: node]
			ifFalse: [statements addLast: node].
		^self].
	thenBlock := self blockTo: skip.
	pc := elseStart.
	elseBlock := self blockTo: elseStart + skipDist.
	"The arm taken when the condition is true comes first."
	condition
		ifTrue: [trueBlock := elseBlock. falseBlock := thenBlock]
		ifFalse: [trueBlock := thenBlock. falseBlock := elseBlock].
	stack addLast: (constructor
		codeMessage: cond
		selector: (constructor codeSelector: #ifTrue:ifFalse: code: #macro)
		arguments: (Array with: trueBlock with: falseBlock)).
	pc := elseStart + skipDist
]

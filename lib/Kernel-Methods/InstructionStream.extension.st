"
The instruction-length table did not know the closure bytecodes.

scanFor: steps through a method by adding each bytecode's length to pc, and
for the 128..143 group it takes that length from a literal array:

    pc + (#(2 2 2 2 3 2 3 1 1 1 ) at: byte \\\\ 16 + 1)

Ten entries, for 128..137.  In 1983 that was the whole group -- 138 to 143
were unused -- so `at:' could never go past the end.  This system compiles
V3PlusClosures, which put pushNewArray at 138 and the remote-temp and
closure-copy instructions at 140..143, and the first method containing one
walks off the array with `subscript is out of bounds: 16'.

That is not a hypothetical: it is every method this compiler emits a closure
into, and `Behavior>>whichSelectorsReferTo:' scans them all.  It stayed
hidden because that search was ALSO passing a nil byte -- see
lib/Scope-Fixes -- so the scan never ran.  Fixing the scope bug is what made
this one reachable, which is the ordinary way one fault hides another.

Six entries appended, for the six opcodes this system added:

    138 pushNewArray        2   opcode and a size
    139 unused              1
    140 pushRemoteTemp      3   opcode, index, vector
    141 storeRemoteTemp     3
    142 popIntoRemoteTemp   3
    143 pushClosureCopy     4   opcode, counts, and a two-byte jump

interpretExtension:in:for: had the same 1983 horizon -- offsets 0 to 9 of
the 128..143 group, and `unusedBytecode' for the rest -- and is here for
the same reason.  The dispatch is how the Debugger steps: `step' and `send'
run the selected frame through ContextPart>>step, which is this dispatch
with the context as its own client, and every method that makes a block
holds a 143, so the first step in ordinary code -- a collect:, a do:, an
on:do: -- opened a second notifier saying `unusedBytecode' over the
debugger (Bugs07 GUI-5).  Stepping a lower frame was worse than a refusal:
completeCallee: had already simulated the callee to its return before the
caller's own closure failed, so the debugged process had been moved on
partway.

So the five opcodes are decoded here, into messages the clients now
implement -- the operands exactly as the interpreter reads them in
src/interp/interp.c:

    138 n       pushNewArrayOfSize: n, or pushConsArrayWithElements: n
                when the top bit is set (the elements come off the stack)
    140 i v     pushRemoteTemp: i inVectorAt: v
    141 i v     storeIntoRemoteTemp: i inVectorAt: v
    142 i v     popIntoRemoteTemp: i inVectorAt: v
    143 c h l   pushClosureCopyNumCopiedValues: c >> 4 numArgs: c & 15
                blockSize: h * 256 + l

The names are Squeak's, so that a client ported from there already has
them.  139 is still unused.  The clients are ContextPart, which simulates
them (lib/Kernel-Methods/ContextPart.extension.st), and InstructionPrinter,
which prints them; the Decompiler still decodes these five for itself
before it gets here (lib/Compiler-Fixes/Decompiler.extension.st).
"
Extension { #name : 'InstructionStream' }

{ #category : '*Kernel-Methods' }
InstructionStream >> scanFor: scanBlock [
	"Answer whether the receiver contains a bytecode for which scanBlock
	 answers true."

	| method end byte type |
	method := self method.
	end := method endPC.
	[pc <= end] whileTrue:
		[(scanBlock value: (byte := method at: pc)) ifTrue: [^true].
		type := byte // 16.
		pc := type = 8
				ifTrue: ["extensions, closures included"
						pc + (#(2 2 2 2 3 2 3 1 1 1 2 1 3 3 3 4) at: byte \\ 16 + 1)]
				ifFalse: [type = 10
							ifTrue: [pc + 2	"long jumps"]
							ifFalse: [pc + 1]]].
	^false
]

{ #category : '*Kernel-Methods' }
InstructionStream >> interpretExtension: offset in: method for: client [
	"Decode one of the 128..143 group for client; pc has already passed
	 the opcode.  1983's ten, and the closure instructions; see the
	 comment above."

	| numberArguments literalNumber type offset2 byte |
	offset < 3 ifTrue: [
		"extended pushes and pops"
		type := (method at: pc) // 64.
		offset2 := (method at: pc) \\ 64.
		pc := pc + 1.
		offset = 0 ifTrue: [
			type = 0 ifTrue: [^client pushReceiverVariable: offset2].
			type = 1 ifTrue: [^client pushTemporaryVariable: offset2].
			type = 2 ifTrue: [^client pushConstant: (method literalAt: offset2 + 1)].
			type = 3 ifTrue: [^client pushLiteralVariable: (method literalAt: offset2 + 1)]].
		offset = 1 ifTrue: [
			type = 0 ifTrue: [^client storeIntoReceiverVariable: offset2].
			type = 1 ifTrue: [^client storeIntoTemporaryVariable: offset2].
			type = 2 ifTrue: [self error: 'illegalStore'].
			type = 3 ifTrue: [^client storeIntoLiteralVariable: (method literalAt: offset2 + 1)]].
		offset = 2 ifTrue: [
			type = 0 ifTrue: [^client popIntoReceiverVariable: offset2].
			type = 1 ifTrue: [^client popIntoTemporaryVariable: offset2].
			type = 2 ifTrue: [self error: 'illegalStore'].
			type = 3 ifTrue: [^client popIntoLiteralVariable: (method literalAt: offset2 + 1)]]].
	offset < 7 ifTrue: [
		"extended sends"
		offset odd
			ifTrue: [
				numberArguments := (method at: pc) // 32.
				literalNumber := (method at: pc) \\ 32.
				pc := pc + 1]
			ifFalse: [
				numberArguments := method at: pc.
				literalNumber := method at: pc + 1.
				pc := pc + 2].
		^client
			send: (method literalAt: literalNumber + 1)
			super: offset > 4
			numArgs: numberArguments].
	offset = 7 ifTrue: [^client doPop].
	offset = 8 ifTrue: [^client doDup].
	offset = 9 ifTrue: [^client pushActiveContext].
	offset = 10 ifTrue: [
		"138: a new Array, of nils or of what is on the stack"
		byte := method at: pc.
		pc := pc + 1.
		byte > 127 ifTrue: [^client pushConsArrayWithElements: byte - 128].
		^client pushNewArrayOfSize: byte].
	(offset between: 12 and: 14) ifTrue: [
		"140-142: a temporary that lives in a vector"
		byte := method at: pc.
		offset2 := method at: pc + 1.
		pc := pc + 2.
		offset = 12 ifTrue: [^client pushRemoteTemp: byte inVectorAt: offset2].
		offset = 13 ifTrue: [^client storeIntoRemoteTemp: byte inVectorAt: offset2].
		^client popIntoRemoteTemp: byte inVectorAt: offset2].
	offset = 15 ifTrue: [
		"143: a closure, and the size of the body that follows it"
		byte := method at: pc.
		offset2 := (method at: pc + 1) * 256 + (method at: pc + 2).
		pc := pc + 3.
		^client
			pushClosureCopyNumCopiedValues: byte // 16
			numArgs: byte \\ 16
			blockSize: offset2].
	self error: 'unusedBytecode'
]

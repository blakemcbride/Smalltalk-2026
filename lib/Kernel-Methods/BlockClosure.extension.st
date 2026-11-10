"
BlockClosure>>hasMethodReturn, for the simulator's runSimulated:.

ContextPart>>runSimulated:contextAtEachStep: runs a block under the
simulator until the block returns to the context that asked, and it
refuses a block that holds a ^ -- such a block returns from its home
instead, past the context that asked, and the simulation would then run
on loose through whatever the home's caller does next.  1983 asked the
block, and only BlockContext could answer: `ContextPart runSimulated: [3 +
4]' answered `hasMethodReturn' not understood once every block was a
closure (Bugs07 GUI-5, which taught the simulator closures).

The answer is in the bytes.  A closure's body is inline in its method,
right after the 143 that made it, whose last two operand bytes -- just
before startpc -- are the body's size; a ^ is a bytecode from 120 to 124
at an instruction boundary anywhere from startpc to the end of the body,
including inside a block written within this one, whose ^ leaves the same
home.
"
Extension { #name : 'BlockClosure' }

{ #category : '*Kernel-Methods-Fixes' }
BlockClosure >> hasMethodReturn [
	"Answer whether there is an ^ in the code of this block."
	| method end scanner |
	method := self method.
	end := startpc + ((method at: startpc - 2) * 256) + (method at: startpc - 1) - 1.
	scanner := InstructionStream new method: method pc: startpc.
	scanner scanFor: [:byte | (byte between: 120 and: 124) or: [scanner pc > end]].
	^scanner pc <= end
]

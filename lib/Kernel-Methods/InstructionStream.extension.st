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

Only scanFor: is corrected here.  InstructionStream>>interpretNextInstruction-
For: has the same 1983 horizon and would need the whole decompiler client
protocol taught about closures, which is a different and much larger job;
nothing reachable from the interface runs it today.
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

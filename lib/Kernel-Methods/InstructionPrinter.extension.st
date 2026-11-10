"
The instruction printer, taught the closure instructions.

CompiledMethod>>symbolic prints a method's bytecodes through
InstructionPrinter, which is a client of the same dispatch the Debugger
steps with.  When that dispatch learned the closure instructions (Bugs07
GUI-5; see InstructionStream.extension.st beside this file) every client
had to learn their messages, or the printer would have gone from
`unusedBytecode' to a doesNotUnderstand: on the same methods.

A closure's body is printed where it lies, inline after the instruction
that makes it, as Squeak's printer prints it; the line for the 143 says
where the body ends.
"
Extension { #name : 'InstructionPrinter' }

{ #category : '*Kernel-Methods-Fixes' }
InstructionPrinter >> pushNewArrayOfSize: numElements [
	self print: 'push: (Array new: ' , numElements printString , ')'
]

{ #category : '*Kernel-Methods-Fixes' }
InstructionPrinter >> pushConsArrayWithElements: numElements [
	self print: 'pop ' , numElements printString , ' into: (Array new: '
		, numElements printString , ')'
]

{ #category : '*Kernel-Methods-Fixes' }
InstructionPrinter >> pushRemoteTemp: remoteTempIndex inVectorAt: tempVectorIndex [
	self print: 'pushTemp: ' , remoteTempIndex printString
		, ' inVectorAt: ' , tempVectorIndex printString
]

{ #category : '*Kernel-Methods-Fixes' }
InstructionPrinter >> storeIntoRemoteTemp: remoteTempIndex inVectorAt: tempVectorIndex [
	self print: 'storeIntoTemp: ' , remoteTempIndex printString
		, ' inVectorAt: ' , tempVectorIndex printString
]

{ #category : '*Kernel-Methods-Fixes' }
InstructionPrinter >> popIntoRemoteTemp: remoteTempIndex inVectorAt: tempVectorIndex [
	self print: 'popIntoTemp: ' , remoteTempIndex printString
		, ' inVectorAt: ' , tempVectorIndex printString
]

{ #category : '*Kernel-Methods-Fixes' }
InstructionPrinter >> pushClosureCopyNumCopiedValues: numCopied numArgs: numArgs blockSize: blockSize [
	self print: 'closureNumCopied: ' , numCopied printString
		, ' numArgs: ' , numArgs printString
		, ' bytes ' , pc printString , ' to ' , (pc + blockSize - 1) printString
]

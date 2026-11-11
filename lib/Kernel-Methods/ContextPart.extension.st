"
The simulator, taught closures.

ContextPart is the Smalltalk machine written in Smalltalk: step decodes the
next bytecode of a context with the context as the client, and the client
does what the interpreter would.  It is what the Debugger's `step' and
`send' run, and what ContextPart class>>runSimulated: and its tallies run.
1983 had no closures, and nothing taught it them.  The dispatch raised
`unusedBytecode' at the first 143 -- the first block in the method being
stepped (Bugs07 GUI-5) -- and behind that the simulator had no way to make
a closure, nothing to run one with, and no idea where a ^ inside one goes.

Making one.  The five instructions lib/Kernel-Methods/InstructionStream
now decodes are implemented here as the interpreter implements them in
src/interp/interp.c: a new Array, empty or filled from the stack; a
temporary read, stored or popped through the vector a frame slot holds;
and a BlockClosure of the outer context, the pc just past the instruction
and the copied values off the stack, after which the simulation jumps past
the body, which is inline in the method.

Running one.  BlockClosure>>value and its relatives are primitives, and
the simulator tries a primitive by running it for real -- doPrimitive:
plants the number in a template method and sends it.  For a closure that
is the wrong thing twice over: the block would run outside the simulation,
so `send' could never step into it; and a ^ inside it would return from
its home, which is a simulated context on no real stack.  So 201 to 206,
221 and 222 build the activation the interpreter's ST_activate_closure
builds -- a MethodContext of the home method's size, the closure in its
fifth field (the one 1983 called receiverMap), the arguments and then the
copied values as its first temporaries, pc at the closure's start and the
stack pointer just above them -- and the simulation goes on inside it.

Returning from one.  A closure's activation is a MethodContext, so 1983's
`self home sender' sent a ^ inside a block back to the block's caller.  A
^ in a closure returns from the method the closure was written in: its
home is found by following each closure's outerContext out, as
return_value does; a home that is no longer on the stack gets
cannotReturn:, and an ensure: frame between here and the home gets
aboutToReturn:through:, both sent within the simulation, exactly as the
interpreter sends them.  The end of a block (125) returns to its caller,
which is MethodContext>>blockReturnTop beside this file.

Two more primitives are not run for real, for the same reason as the
closures.  246 (ContextPart>>return:) and 247 (resume:) are how an
exception handler leaves -- Exception>>return:, resume: and every
on:do: that answers a value end in one of them -- and run for real they
refuse a context that is not on the real stack, which every simulated one
is not.  Here they cut the simulated stack back and push the value, which
is what they do to a real one.

Nor is 251, restartAndJump, which is how an exception's retry and
retryUsing: start their handler's frame again.  Run for real it refuses a
context that is not on the real stack, so a retry failed in the
simulation, `a primitive has failed' (Bugs07 X-26).  Here it cuts the
simulated stack back to the frame and restarts it as MethodContext>>
restart does, temporaries nil.

What is still run for real is what 1983 ran for real: every other
primitive.  A process primitive (a Semaphore's wait, a Process's suspend)
therefore acts on the process doing the simulating, as it does in 1983
and in Pharo: stepping over `Processor activeProcess suspend' suspends
the Debugger's process, not the one being debugged.

Where a simulation ends.  runSimulated: ran the block on top of its
caller's own frame, so an exception the block did not handle was looked
for in the real frames below, and a handler found there ran inside the
simulation and returned through them -- emptying frames that were still
running and simulating the rest of the caller's program after them
(Bugs07 X-26).  The simulation now stands on a frame of its own with no
sender, as Pharo's does: it ends when that frame is returned to, and
nothing below it is reached.  An exception the block leaves unhandled is
unhandled inside the simulation.  And completeCallee:, which runs a
callee until it returns -- the Debugger's `step' over a send -- stopped
when the simulation unwound past the frame it was completing, as when the
callee's exception was handled below it, and then stepped that frame,
which was no longer on the stack; it answers the frame the simulation
reached instead, and the Debugger goes on from there.

And until now NO primitive ran, closures or not.  tryPrimitiveFor: opened
with a `fast check' that the method's flags were under 4 --
`(method objectAt: 1) > 0' -- which on the Alto, where the header was a
fifteen-bit SmallInteger with the flags at its top, was true exactly when
bit 14 was clear.  Here the header is a sixty-three-bit SmallInteger and is
never negative, so every method looked primitive-free and every primitive
was simulated by its Smalltalk fallback: `3 + 4' went to SmallInteger>>+'s
`super +', the LargeInteger arithmetic, which itself adds SmallIntegers,
and the simulation never came back -- `ContextPart runSimulated: [3 + 4]'
was still going after two hundred million bytecodes, and a Debugger `step'
over any arithmetic hung the desktop.  The flags are what the next line
already read; the fast check is gone.

And the way 1983 ran a primitive for real was to write its number into a
literal of one of five template methods in Object, flush the method cache
and send the template.  Those five methods are every process's, so two
processes simulating at once on two workers each wrote the other's number
there between the write and the send -- the class-variable scratch space
that every class in this system has to give up (doc/CONCURRENCY.md).  The
number now goes into a copy of the template, made for the call, which is
run with withArgs:executeMethod: (primitive 188); nothing shared is
written, and the method cache is left alone.
"
Extension { #name : 'ContextPart' }

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> pushNewArrayOfSize: arraySize [
	"Simulate 138 without its top bit: a vector of arraySize nils, which
	 is how a temporary shared with a block starts."
	self push: (Array new: arraySize)
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> pushConsArrayWithElements: numElements [
	"Simulate 138 with its top bit: an Array of the top numElements values
	 of the stack, deepest first -- a brace array, {a. b}."
	| array |
	array := Array new: numElements.
	numElements to: 1 by: -1 do: [:i | array at: i put: self pop].
	self push: array
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> pushRemoteTemp: remoteTempIndex inVectorAt: tempVectorIndex [
	"Simulate 140: push the temporary at remoteTempIndex (zero-based) of
	 the vector in my temporary tempVectorIndex (zero-based)."
	self push: ((self tempAt: tempVectorIndex + 1) at: remoteTempIndex + 1)
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> storeIntoRemoteTemp: remoteTempIndex inVectorAt: tempVectorIndex [
	"Simulate 141: store the top of the stack into a temporary held in a
	 vector, leaving it on the stack."
	(self tempAt: tempVectorIndex + 1) at: remoteTempIndex + 1 put: self top
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> popIntoRemoteTemp: remoteTempIndex inVectorAt: tempVectorIndex [
	"Simulate 142: pop the top of the stack into a temporary held in a
	 vector."
	(self tempAt: tempVectorIndex + 1) at: remoteTempIndex + 1 put: self pop
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> pushClosureCopyNumCopiedValues: numCopied numArgs: numArgs blockSize: blockSize [
	"Simulate 143: push a BlockClosure whose body is the next blockSize
	 bytes, and go on after the body.  The copied values are the top
	 numCopied of the stack, deepest first.  startpc is one-relative, as
	 pc is, and is where pc stands now -- just past the instruction."
	| closure |
	closure := BlockClosure basicNew: numCopied.
	closure instVarAt: 1 put: self.
	closure instVarAt: 2 put: pc.
	closure instVarAt: 3 put: numArgs.
	numCopied to: 1 by: -1 do: [:i | closure at: i put: self pop].
	self push: closure.
	self jump: blockSize
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> activateClosure: aClosure withArgs: arguments [
	"Answer the context aClosure runs in when I send it value with
	 arguments, built as the interpreter's ST_activate_closure builds it;
	 or #primitiveFail where that primitive fails."
	| outer numCopied context |
	(aClosure isKindOf: BlockClosure) ifFalse: [^#primitiveFail].
	arguments size = aClosure numArgs ifFalse: [^#primitiveFail].
	outer := aClosure outerContext.
	(outer isKindOf: MethodContext) ifFalse: [^#primitiveFail].
	numCopied := aClosure basicSize.
	context := MethodContext new: outer method frameSize.
	context
		setSender: self
		receiver: outer receiver
		method: outer method
		arguments: arguments.
	1 to: numCopied do: [:i |
		context at: arguments size + i put: (aClosure at: i)].
	"pc, stackp and the closure: fields 2, 3 and 5."
	context instVarAt: 2 put: aClosure startpc.
	context instVarAt: 3 put: arguments size + numCopied.
	context instVarAt: 5 put: aClosure.
	^context
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> doPrimitive: primitiveIndex receiver: receiver args: arguments [
	"Simulate a primitive method whose index is primitiveIndex.  The
	 simulated receiver and arguments are given as arguments to this
	 message.  If successful, push result and return resuming context,
	 else ^#primitiveFail.

	 1983's, with the closure primitives and the two that move between
	 contexts done within the simulation rather than for real; see the
	 comment above."

	| numberArguments primitiveMethod value |
	primitiveIndex = 251 ifTrue: [^self simulateRestartOf: receiver].
	((primitiveIndex between: 201 and: 205)
		or: [primitiveIndex = 221 or: [primitiveIndex = 222]])
			ifTrue: [^self activateClosure: receiver withArgs: arguments].
	primitiveIndex = 206 ifTrue: [
		(arguments size = 1 and: [arguments first class == Array])
			ifFalse: [^#primitiveFail].
		^self activateClosure: receiver withArgs: arguments first].
	(primitiveIndex = 246 or: [primitiveIndex = 247]) ifTrue: [
		^self simulateJumpTo: receiver answering: arguments first
			resuming: primitiveIndex = 247].
	(primitiveIndex = 80 and: [receiver isKindOf: ContextPart])
		ifTrue: [^self push:
					((BlockContext new: receiver size)
						home: receiver home
						startpc: pc + 2
						nargs: arguments first)].
	(primitiveIndex = 81 and: [receiver isMemberOf: BlockContext])
		ifTrue: [^receiver pushArgs: arguments from: self].
	primitiveIndex = 83
		ifTrue: [^self
					send: arguments first
					to: receiver
					with: (arguments copyFrom: 2 to: arguments size)
					super: false].
	numberArguments := arguments size.
	numberArguments > 4 ifTrue: [^#primitiveFail].
	"A template of this call's own; see the comment above."
	primitiveMethod := (TryPrimitiveMethods at: numberArguments + 1) copy.
	primitiveMethod
		literalAt: 2
		put: ((primitiveMethod literalAt: 2) bitAnd: -256) + primitiveIndex.
	value := receiver
		withArgs: arguments asArray
		executeMethod: primitiveMethod.
	value == #primitiveFail
		ifTrue: [^value]
		ifFalse: [^self push: value]
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> tryPrimitiveFor: method receiver: receiver args: arguments [
	"Simulate a primitive method, method for the receiver and arguments
	 given as arguments to this message.  Answer resuming the context if
	 successful, else answer the symbol, #primitiveFail.  1983's without
	 the Alto's sign test; see the comment above."
	| flag primIndex |
	(flag := method flags) < 5 ifTrue: [^#primitiveFail].
	flag = 5 ifTrue: [^self push: receiver].
	flag = 6 ifTrue: [^self push: (receiver instVarAt: method numTempsField + 1)].
	(primIndex := method primitive) = 0 ifTrue: [^#primitiveFail].
	^self doPrimitive: primIndex receiver: receiver args: arguments
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> simulateJumpTo: aContext answering: value resuming: resuming [
	"Primitives 246 and 247, sent from me to aContext: return value from
	 aContext to its sender, or (resuming) carry on in aContext as though
	 the send it was waiting on had answered value.  Everything above goes,
	 as it does from a real stack.  Fails, as they do, unless aContext is
	 on my stack -- and, for a return, unless it has a sender to return to,
	 because a simulation cannot end a process."
	| target |
	(aContext isKindOf: ContextPart) ifFalse: [^#primitiveFail].
	(self == aContext or: [self hasSender: aContext]) ifFalse: [^#primitiveFail].
	resuming ifTrue: [
		self releaseTo: aContext.
		^aContext push: value].
	target := aContext sender.
	target isNil ifTrue: [^#primitiveFail].
	^self return: value to: target
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> methodReturnConstant: value [
	"Simulate a 'return constant' bytecode (^0).  See returnFromMethod:."
	^self returnFromMethod: value
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> methodReturnReceiver [
	"Simulate a 'return receiver' bytecode (^self).  See returnFromMethod:."
	^self returnFromMethod: self receiver
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> methodReturnTop [
	"Simulate a 'return top of stack' bytecode (^something).  See
	 returnFromMethod:."
	^self returnFromMethod: self pop
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> returnFromMethod: value [
	"A ^ in me returns value from my method.  For a method's own
	 activation, or a Blue Book block, that is 1983's `return: value to:
	 self home sender'.  For a closure's activation it is a return from the
	 home method out past every frame between, as the interpreter's
	 return_value does it: cannotReturn: if the home has already returned,
	 aboutToReturn:through: if an ensure: frame lies between, both sent
	 within the simulation."
	| home ctx unwind |
	(self closure isKindOf: BlockClosure)
		ifFalse: [^self return: value to: self home sender].
	home := self.
	[home notNil and: [home closure isKindOf: BlockClosure]] whileTrue: [
		ctx := home closure outerContext.
		home := (ctx isKindOf: MethodContext) ifTrue: [ctx] ifFalse: [nil]].
	ctx := home isNil ifTrue: [nil] ifFalse: [self sender].
	[ctx isNil or: [ctx == home]] whileFalse: [ctx := ctx sender].
	ctx isNil ifTrue: [
		^self send: #cannotReturn: to: self with: (Array with: value) super: false].
	unwind := self findNextUnwindUpTo: home.
	unwind isNil ifFalse: [
		^self
			send: #aboutToReturn:through:
			to: self
			with: (Array with: value with: unwind)
			super: false].
	^self return: value to: home sender
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> runSimulated: aBlock contextAtEachStep: block2 [
	"Simulate the execution of aBlock until it ends.  aBlock MUST NOT
	 contain an ^.  Evaluate block2 with current context prior each
	 instruction executed.  Answer with the simulated value of aBlock.

	 1983's, for a BlockClosure as well as a BlockContext: a closure is
	 not itself a context, so the simulation starts in the activation
	 its value would make.  Its sender is a frame of the simulation's
	 own, not me; see the comment above."
	| bottom current |
	aBlock hasMethodReturn
		ifTrue: [self error: 'simulation of blocks with ^ can run loose'].
	self class initPrimitives.
	"The frame the simulation returns to, standing in for me: mine is
	 running, and the frames below it are not the simulation's."
	bottom := MethodContext
		sender: nil
		receiver: self
		method: thisContext method
		arguments: (Array with: aBlock with: block2).
	(aBlock isKindOf: BlockClosure)
		ifTrue: [
			current := bottom activateClosure: aBlock withArgs: #().
			current == #primitiveFail ifTrue: [
				^self error: 'runSimulated: takes a block of no arguments']]
		ifFalse: [
			current := aBlock.
			current pushArgs: Array new from: bottom].
	[current == bottom]
		whileFalse:
			[current isNil ifTrue: [
				^self error: 'the simulation returned past where it began'].
			block2 value: current.
			current := current step].
	^bottom pop
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> simulateRestartOf: aContext [
	"Primitive 251, restartAndJump, sent from me: aContext starts again
	 from its first bytecode with its temporaries nil, and the simulation
	 goes on in it.  Everything above it goes, as from a real stack.
	 Fails, as it does, unless aContext is a frame on my stack."
	(aContext isKindOf: MethodContext) ifFalse: [^#primitiveFail].
	(self == aContext or: [self hasSender: aContext]) ifFalse: [^#primitiveFail].
	self releaseTo: aContext.
	aContext restart.
	^aContext
]

{ #category : '*Kernel-Methods-Fixes' }
ContextPart >> completeCallee: aContext [
	"Simulate the execution of bytecodes until a return to the receiver,
	 and answer the frame the simulation is in then: the receiver, at its
	 next send or return -- or the frame it unwound to, when what ran
	 returned past the receiver (an exception handled below it, a retry,
	 a ^ from a block whose home lies below), or nil when it returned past
	 the bottom of the stack.  1983's stopped there too, and then stepped
	 the receiver, which was no longer on the stack.  See the comment
	 above."
	| ctxt current |
	self class initPrimitives.
	ctxt := aContext.
	[ctxt notNil and: [ctxt == current or: [ctxt hasSender: self]]]
		whileTrue:
			[current := ctxt.
			ctxt := ctxt step].
	ctxt == self ifFalse: [^ctxt].
	self stepToSendOrReturn.
	^self
]

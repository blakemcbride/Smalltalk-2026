"
What the exception machinery asks of a context.

isHandlerContext and isUnwindContext read the method's primitive number,
which the 1983 CompiledMethod>>primitive already answers correctly for this
VM's header encoding -- BlockClosure>>value reads back as 201.

return: and resume: are the two things Smalltalk cannot say about itself.
"
Extension { #name : 'ContextPart' }

{ #category : '*Kernel-Exceptions' }
ContextPart >> isHandlerContext [
	"Marked by BlockClosure>>on:do:, and not already handling something."
	^self method primitive = 199 and: [self handlerIsActive]
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> handlerIsActive [
	"on:do: keeps this in its third temporary; see the comment there."
	^(self tempAt: 3) == true
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> handlerIsActive: aBoolean [
	self tempAt: 3 put: aBoolean
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> isUnwindContext [
	"Marked by BlockClosure>>ensure: and >>ifCurtailed:."
	^self method primitive = 198
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> return: aValue [
	"Complete the send that made me, answering aValue, and abandon
	 everything I called."
	<primitive: 246>
	^self primitiveFailed
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> resume: aValue [
	"Carry on where I stopped, as though the send I was waiting on had
	 answered aValue.

	 The primitive refuses a context that is no longer on the stack, which
	 is what an exception resumed after its handler returned would be."
	<primitive: 247>
	^Error new signal: 'that context has already returned'
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> restartAndJump [
	"Begin this activation again from its first bytecode, keeping the
	 arguments it already has, and carry on in it.

	 Not called restart: MethodContext>>restart is a 1983 method that
	 resets a frame for the Debugger to resume separately, and this does
	 both at once."
	<primitive: 249>
	^self primitiveFailed
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> cannotReturn: aResult [
	"The VM sends this when a block tries to return from a method that has
	 already returned.  Until now nothing implemented it, so the VM stopped
	 quietly and kept the value."
	^Error new signal: 'return from a method that has already returned'
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> closure [
	"The closure this context is running, or nil for a plain method.
	 InstructionStream declares sender and pc, ContextPart adds stackp, and
	 MethodContext adds method, receiverMap and receiver -- so field five is
	 the one Xerox called receiverMap and never used, which is where the VM
	 keeps closureOrNil."
	^self instVarAt: 5
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> homeContext [
	"The method activation a ^ in me would return from."
	| ctx |
	ctx := self.
	[ctx closure isNil] whileFalse: [ctx := ctx closure outerContext].
	^ctx
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> findNextHandlerContext [
	"The next frame outwards marked by on:do:, or nil.

	 The primitive is Squeak's and is an optimisation only: the body below
	 does the same walk a send at a time, and asks each method for its
	 primitive number through three more sends.  Without the primitive this
	 is slower and not different."
	<primitive: 197>
	| ctx |
	ctx := self sender.
	[ctx isNil] whileFalse: [
		ctx method primitive = 199 ifTrue: [^ctx].
		ctx := ctx sender].
	^nil
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> findNextUnwindUpTo: aContext [
	"The next frame outwards marked by ensure: or ifCurtailed:, stopping at
	 aContext.  Squeak's primitive 195, and an optimisation only."
	<primitive: 195>
	| ctx |
	ctx := self sender.
	[ctx isNil or: [ctx == aContext]] whileFalse: [
		ctx isUnwindContext ifTrue: [^ctx].
		ctx := ctx sender].
	^nil
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> aboutToReturn: aResult through: firstUnwindContext [
	"The VM found an ensure: between a returning block and its home.
	 Run every unwind action between here and the home -- there may be
	 several nested -- and only then let the return finish."
	| home unwind |
	home := self homeContext.
	unwind := firstUnwindContext.
	[unwind isNil] whileFalse: [
		unwind runUnwindBlock.
		unwind := unwind findNextUnwindUpTo: home].
	^home return: aResult
]

{ #category : '*Kernel-Exceptions' }
ContextPart >> runUnwindBlock [
	"An ensure: frame keeps its unwind block in its FIRST temporary: it is
	 the method's only argument, and a method's arguments come first.

	 It disarms itself, because more than one path can pass through the
	 same frame on the way out -- an exception unwinding to a handler and
	 then a non-local return through what is left -- and an ensure: block
	 that ran twice would close the file twice.  Nilling the temporary is
	 what the isNil guard above was always for."
	| block |
	block := self tempAt: 1.
	block isNil ifTrue: [^nil].
	self tempAt: 1 put: nil.
	^block value
]

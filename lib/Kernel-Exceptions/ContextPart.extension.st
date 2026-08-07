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
	"Marked by BlockClosure>>on:do:."
	^self method primitive = 199
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
	 answered aValue."
	<primitive: 247>
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
ContextPart >> findNextUnwindUpTo: aContext [
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
	 the method's only argument, and a method's arguments come first."
	| block |
	block := self tempAt: 1.
	block isNil ifTrue: [^nil].
	^block value
]

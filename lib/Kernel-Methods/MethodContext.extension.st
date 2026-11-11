"
Restarting a frame counted its arguments twice.

MethodContext>>restart resets a frame so the Debugger can run it again --
it is what the debugger's 'restart' button does, and what
restartWith: does after a method under debug is recompiled.  It set

	stackp := method numArgs + method numTemps

and numTemps ALREADY counts the arguments.  BlockClosure>>on:do: takes two
arguments and declares one temporary, and answers 3 for numTemps, not 1.
So a restarted frame came back believing it had two more values below its
stack than it did, and the method then read whatever was in those slots.

The class contradicts itself about this, which is what makes it findable:
setSender:receiver:method:arguments: -- twenty lines further down, and the
method that CREATES a context -- says plainly

	stackp := method numTemps

and that one agrees with the interpreter, which sets a new frame's stack
pointer to the header's temporary count and nothing else.

MethodContext>>blockReturnTop is also here, for the simulator (see
ContextPart.extension.st beside this file).  The end of a block is
bytecode 125, and 1983 simulated it only in BlockContext, the one place a
block ran.  A closure runs in a MethodContext, so stepping off the end of
any block in the Debugger sent a message no MethodContext understood
(Bugs07 GUI-5).  A block's end returns its value to whoever sent it value
-- the sender, not the home method's sender -- as the interpreter's 125
does.

And restarting a BLOCK's frame ran the method instead (Bugs07 GUI-6).
With closures a block runs in a MethodContext of its own: method is the
home method, because the block's bytecodes are inline in it, and field
five -- receiverMap to 1983, closureOrNil to the VM -- holds the
BlockClosure.  restart read only method, so it put pc at the METHOD's first
bytecode and the stack pointer at the method's temporary count, and the
block's frame, resumed, ran the whole method from the top with the block's
arguments sitting where the method's temporaries belong: `log add:
#method' sent to the 1 that was the block's argument.  A closure's frame
starts where the interpreter's ST_activate_closure starts it -- pc at the
closure's startpc, the arguments and then the copied values as the first
temporaries, the stack pointer just above them -- so restart puts it back
there, re-storing the copied values from the closure (a block never writes
them; a temporary it writes lives in a vector instead).  Everything above
that the block's own prologue makes again: its temp vector and a nil for
each local.  Pharo's privRefresh is the same.

restartWith: a new method cannot be done to a block's frame at all: the
new method's blocks need not start where the old one's did, or exist.  It
refuses; the Debugger, which is what sends it after an accept, reverts to
the home method's frame first (Debugger.extension.st beside this file).

printOn: says `[] in' before a closure's frame, as 1983's BlockContext
printed itself: otherwise a block's frame and its method's print the same,
and a stack in the Debugger or a walkback shows the method twice with no
way to choose the block.
"
Extension { #name : 'MethodContext' }

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> restart [
	"Reinitialize the receiver so that it is in the state it was at its
	 creation: at its method's first bytecode with the arguments in
	 place or, for a closure's frame, at the block's first bytecode with
	 the block's arguments and copied values in place.  See the comment
	 above."
	| closure numArgs |
	closure := self closure.
	closure isNil ifTrue: [
		pc := method initialPC.
		stackp := method numTemps.
		^self].
	numArgs := closure numArgs.
	pc := closure startpc.
	stackp := numArgs + closure basicSize.
	1 to: closure basicSize do: [:i |
		self at: numArgs + i put: (closure at: i)]
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> restartWith: aCompiledMethod [
	"Reinitialize the receiver as though it had been for a different
	 method.  Used by a Debugger when one of the methods to which it
	 refers is recompiled.  Refused for a closure's frame, whose block the
	 new method need not have; restart its home instead."
	self closure isNil ifFalse: [
		^self error: 'a block''s frame cannot be restarted with a new method; restart the method it was written in'].
	method := aCompiledMethod.
	^self restart
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> printOn: aStream [
	"A closure's frame prints as 1983's BlockContext printed: `[] in'
	 and then its method."
	self closure isNil ifFalse: [aStream nextPutAll: '[] in '].
	super printOn: aStream
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> blockReturnTop [
	"Simulate the end of a closure's body: return the top of the stack to
	 my sender, which is whoever sent the closure value."
	^self return: self pop to: sender
]

"
The Debugger, told a block's frame from its method's.

1983's Debugger knew a block by its class: a frame that was not a
MethodContext was a BlockContext, and before `restart' or an accepted edit
it offered to revert to the block's home, because a BlockContext could not
be started over and a new method could not be put into one.  With closures
a block runs in a MethodContext of its own, so that test let every block's
frame through as a method's, and both operations then ran the HOME
METHOD's bytecodes inside the block's frame (Bugs07 GUI-6): `restart' on
the block's frame in a do-it of `#(1 2 3) collect: [:x | x zork]' ran the
whole do-it again inside itself, and in a real method the method's first
statements ran with the block's arguments where its temporaries belong.

A closure's frame is now told by the closure it holds, and the two
operations part company.

`restart' restarts the selected frame, block or method:
MethodContext>>restart (beside this file) starts a closure's frame again
at the block's first bytecode, so there is nothing to revert.  Only a
BlockContext -- code from 1983's own code generator -- still goes back to
its home.

An accepted edit still has to go to the home, for the reason 1983 gave:
the new method's blocks need not lie where the old one's did, so the frame
that takes the new method is the method's, and the frames above it go.  A
closure's home is found by following outerContext out to the frame with
no closure, and has to be on the stack below the block: a block that has
outlived its method -- a sort block, the bottom of a forked process -- has
no frame of the method left to restart, and the edit is refused with a
notifier rather than put into some other process's frame or a returned
one.
"
Extension { #name : 'Debugger' }

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> restart [
	"Proceed from the initial state of the currently selected context.
	 The argument is a controller on a view of the receiver.  That view
	 is closed.

	 1983's, except that only a BlockContext is reverted to its home: a
	 closure's frame restarts at its block.  See the comment above."
	self checkContextSelection.
	(context isKindOf: MethodContext)
		ifFalse: [self revertBlock ifFalse: [^self]].
	context restart.
	self resumeProcess
]

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> revertBlock [
	"If the selected context is a block, then revert to its home: the
	 home of a BlockContext, or the frame of the method a closure was
	 written in, which must still be on the stack below it.  Answer
	 whether the selected context is now a method's.  See the comment
	 above."
	| home |
	(context isKindOf: MethodContext)
		ifTrue: [
			context closure isNil ifTrue: [^true].
			home := context homeContext.
			(context hasSender: home) ifFalse: [
				self notify: 'The method this block was written in has already returned,
so there is no frame of it to restart with the new code.
Accept the change in a browser instead.'.
				^false]]
		ifFalse: [home := context home].
	(self confirm:
'I will have to revert to the method from
which this block originated.  Is that OK?')
		ifTrue: [self context: home. ^true]
		ifFalse: [^false]
]

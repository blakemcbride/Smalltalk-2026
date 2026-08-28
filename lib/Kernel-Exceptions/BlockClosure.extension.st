"
Catching, and running something whatever happens.

on:do: and ensure: are marked methods rather than ordinary ones.  Neither
<primitive: 199> nor <primitive: 198> is implemented anywhere -- an
unimplemented primitive fails, so the body below runs -- and the number is
left on the frame as a label a walk can see.  199 says 'a handler lives
here' and 198 says 'run my second argument on the way out'.
"
Extension { #name : 'BlockClosure' }

{ #category : '*Kernel-Exceptions' }
BlockClosure >> on: anExceptionClass do: aHandlerBlock [
	"Evaluate the receiver.  If it signals something anExceptionClass
	 handles, evaluate aHandlerBlock with the exception instead.

	 handlerActive is the third temporary, and it is what stops a handler
	 catching its own signals.  Without it, raising anything inside a
	 handler finds this same frame again and the search that was supposed
	 to move outwards goes round for ever -- so the exception a handler
	 could not deal with hangs the image instead of reaching the handler
	 outside it.  Exception>>runHandler clears it for the duration and puts
	 it back, because a resumed exception leaves this frame in place and
	 still wanting to catch things."
	| handlerActive |
	<primitive: 199>
	handlerActive := true.
	^self value
]

{ #category : '*Kernel-Exceptions' }
BlockClosure >> ensure: aBlock [
	"Evaluate the receiver, and aBlock afterwards however the receiver
	 finishes -- normally, or by a return out through it.

	 The normal path runs aBlock through thisContext runUnwindBlock and
	 not as `aBlock value', and the difference is whether aBlock can run
	 TWICE.  runUnwindBlock nils this frame's first temporary before it
	 runs the block; `aBlock value' leaves it armed.  While the block runs
	 this frame is still on the stack, still marked 198, and if the block
	 raises and a handler outside returns, or the block itself does a ^,
	 the unwind walks out through this very frame, finds the block still
	 in its temporary, and runs it again -- the second time with the
	 handler out of service, so the second signal escapes the on:do: that
	 caught the first.  `[[1] ensure: [Error signal: 'e']] fork' reported
	 'e' twice, and an ensure: that closes a file closed it twice (Bugs3
	 B18).  Disarming first is the contract every other path already
	 keeps, and this was the one that did not."
	| result |
	<primitive: 198>
	result := self value.
	thisContext runUnwindBlock.
	^result
]

{ #category : '*Kernel-Exceptions' }
BlockClosure >> ifCurtailed: aBlock [
	"Evaluate aBlock only if the receiver does NOT finish normally."
	| result |
	<primitive: 198>
	result := self value.
	^result
]

{ #category : '*Kernel-Exceptions' }
BlockClosure >> unwindBlock [
	"The block an ensure: frame runs on the way out is its argument."
	^nil
]

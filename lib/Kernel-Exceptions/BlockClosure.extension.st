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
	 handles, evaluate aHandlerBlock with the exception instead."
	<primitive: 199>
	^self value
]

{ #category : '*Kernel-Exceptions' }
BlockClosure >> ensure: aBlock [
	"Evaluate the receiver, and aBlock afterwards however the receiver
	 finishes -- normally, or by a return out through it."
	| result |
	<primitive: 198>
	result := self value.
	aBlock value.
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

"
The same three entry points, for a Blue Book block.

They are word-for-word what BlockClosure has, and they can be, because
nothing in them is about closures: the whole mechanism is a walk up the
sender chain looking for a frame whose method declares primitive 199 or
198, and a frame is a frame whichever kind of block made it.

Without these, the exception system was reachable only from code compiled
in the closure dialect -- so the 4,500 methods of the 1983 library, every
one of which is Blue Book, could signal an Error (Object>>error: does) and
could not catch one.  That is a strange half-system to leave behind, and
the fix is a copy.
"
Extension { #name : 'BlockContext' }

{ #category : '*Kernel-Exceptions' }
BlockContext >> on: anExceptionClass do: aHandlerBlock [
	"Evaluate the receiver.  If it signals something anExceptionClass
	 handles, evaluate aHandlerBlock with the exception instead."
	| handlerActive |
	<primitive: 199>
	handlerActive := true.
	^self value
]

{ #category : '*Kernel-Exceptions' }
BlockContext >> ensure: aBlock [
	"Evaluate the receiver, and aBlock afterwards however the receiver
	 finishes -- normally, or by a return out through it.

	 Through runUnwindBlock on the normal path, for the reason given at
	 BlockClosure>>ensure: above: run as `aBlock value' the block stays
	 armed in this frame while it runs, and an unwind through the frame --
	 a handler's return: after the block raised, or a ^ inside it -- runs
	 it a second time (Bugs3 B18)."
	| result |
	<primitive: 198>
	result := self value.
	thisContext runUnwindBlock.
	^result
]

{ #category : '*Kernel-Exceptions' }
BlockContext >> ifCurtailed: aBlock [
	"Evaluate aBlock only if the receiver does NOT finish normally."
	| result |
	<primitive: 198>
	result := self value.
	^result
]

{ #category : '*Kernel-Exceptions' }
BlockContext >> unwindBlock [
	"The block an ensure: frame runs on the way out is its argument."
	^nil
]

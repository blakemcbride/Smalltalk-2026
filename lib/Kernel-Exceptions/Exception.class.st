"
The root of the exception hierarchy.

Smalltalk-80 has no exceptions.  It has Object>>error:, which opens a
debugger, and the convention of passing a block to run when something is
not found -- Collection>>remove:ifAbsent: and friends.  Neither can be
caught, and neither lets a caller decide what to do.

What makes this implementable at all is that a marked frame can be found
by walking senders.  BlockClosure>>on:do: declares <primitive: 199>, which
is not a primitive: nothing implements it, an unimplemented primitive
fails, so the Smalltalk body runs and the number is left behind as a LABEL
on the frame.  Squeak does exactly this, and doc/PLAN-PHARO.md's phase D0
asked the 1983 image whether it uses 198 or 199 for anything real.  Its
highest primitive is 135.

So signalling is: walk out from here looking for a frame whose method says
199 and whose exception class matches; run its handler block; and answer
its value from that frame.  The last step is the only thing Smalltalk
cannot say, and it is primitive 246.
"
Class {
	#name : 'Exception',
	#superclass : 'Object',
	#instVars : [ 'messageText', 'signalContext', 'handlerContext' ],
	#category : 'Kernel-Exceptions'
}

{ #category : 'signalling' }
Exception class >> signal [
	^self new signal
]

{ #category : 'signalling' }
Exception class >> signal: aMessage [
	^self new signal: aMessage
]

{ #category : 'testing' }
Exception class >> handles: anException [
	^anException isKindOf: self
]

{ #category : 'accessing' }
Exception >> messageText [
	^messageText isNil ifTrue: [self description] ifFalse: [messageText]
]

{ #category : 'accessing' }
Exception >> messageText: aString [
	messageText := aString
]

{ #category : 'accessing' }
Exception >> description [
	^'An exception has occurred'
]

{ #category : 'accessing' }
Exception >> signalContext [
	^signalContext
]

{ #category : 'accessing' }
Exception >> handlerContext [
	^handlerContext
]

{ #category : 'testing' }
Exception >> isResumable [
	^false
]

{ #category : 'signalling' }
Exception >> signal [
	"thisContext is this activation of #signal, so its sender is where the
	 signal came from -- and the search starts there rather than here."
	signalContext := thisContext sender.
	^self signalFrom: signalContext
]

{ #category : 'signalling' }
Exception >> signal: aMessage [
	messageText := aMessage.
	signalContext := thisContext sender.
	^self signalFrom: signalContext
]

{ #category : 'private' }
Exception >> signalFrom: aContext [
	| found |
	found := self findHandlerFrom: aContext.
	found isNil ifTrue: [^self defaultAction].
	handlerContext := found.
	^self runHandler
]

{ #category : 'private' }
Exception >> findHandlerFrom: aContext [
	"The innermost frame marked by on:do: that is willing to take me.

	 The walk itself is one primitive per hop rather than a send per frame;
	 which handler HANDLES this exception stays a Smalltalk question,
	 because it is one -- it sends #handles: -- and so does whether the
	 handler is currently running and therefore disabled."
	| ctx |
	ctx := aContext findNextHandlerContext.
	[ctx isNil] whileFalse: [
		(ctx handlerIsActive and: [(ctx tempAt: 1) handles: self])
			ifTrue: [^ctx].
		ctx := ctx findNextHandlerContext].
	^nil
]

{ #category : 'private' }
Exception >> runHandler [
	"Run the handler with itself turned off, so that anything IT signals
	 goes to the next handler out rather than back to this one."
	| block result |
	block := handlerContext tempAt: 2.
	handlerContext handlerIsActive: false.
	result := block numArgs = 0
		ifTrue: [block value]
		ifFalse: [block value: self].
	handlerContext handlerIsActive: true.
	"Falling off the end of a handler answers its value, like Pharo."
	^self return: result
]

{ #category : 'handling' }
Exception >> return: aValue [
	"Answer aValue from the on:do: that caught me."
	handlerContext isNil ifTrue: [^aValue].
	^handlerContext return: aValue
]

{ #category : 'handling' }
Exception >> return [
	^self return: nil
]

{ #category : 'handling' }
Exception >> retry [
	"Run the protected block again, with the same handler in place.

	 By RESTARTING the on:do: frame rather than sending it again.  Sending
	 it again works once and nests one frame deeper every time, so a retry
	 that keeps failing exhausts memory instead of looping -- which it did,
	 with a core dump.  Restarting reuses the frame and can go round for
	 ever, which is what a retry that never succeeds should do."
	handlerContext isNil ifTrue: [^self return: nil].
	handlerContext handlerIsActive: true.
	^handlerContext restartAndJump
]

{ #category : 'handling' }
Exception >> retryUsing: aBlock [
	handlerContext isNil ifTrue: [^self return: nil].
	^handlerContext return: aBlock value
]

{ #category : 'handling' }
Exception >> pass [
	"Give the next handler out a chance at me."
	| outer |
	outer := handlerContext isNil
		ifTrue: [nil]
		ifFalse: [self findHandlerFrom: handlerContext sender].
	outer isNil ifTrue: [^self return: self defaultAction].
	handlerContext := outer.
	^self return: self runHandler
]

{ #category : 'handling' }
Exception >> resume: aValue [
	"Carry on from where signal was sent, as though it had answered aValue.

	 Two things have to hold.  The exception must be resumable at all --
	 an Error has nowhere sensible to go back to.  And the context that
	 signalled must still BE there: a returned context has had its
	 instruction pointer nilled, and resuming one would carry on inside a
	 frame nothing refers to any more.  That happens when the exception
	 outlives its handler -- stored in a variable and resumed later -- and
	 it is a mistake worth naming rather than a crash worth having."
	self isResumable ifFalse: [
		^Error new signal: 'this exception is not resumable'].
	signalContext isNil ifTrue: [
		^Error new signal: 'this exception has no context to resume'].
	"The frame stays and may catch again, so put it back in service."
	handlerContext isNil ifFalse: [handlerContext handlerIsActive: true].
	"Primitive 247 refuses a context that has already returned, and there
	 is nothing sensible to do about that here but say so."
	^signalContext resume: aValue
]

{ #category : 'handling' }
Exception >> resume [
	^self resume: nil
]

{ #category : 'private' }
Exception >> primitiveFailed: aString [
	^Error new signal: aString
]

{ #category : 'handling' }
Exception >> defaultAction [
	"Nobody was listening.  Subclasses decide what that means."
	^nil
]

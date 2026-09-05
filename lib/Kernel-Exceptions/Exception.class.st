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
	| ctx guard willing |
	ctx := aContext findNextHandlerContext.
	[ctx isNil] whileFalse: [
		guard := ctx tempAt: 1.
		"A nil guard cannot handle anything, and asking it whether it does
		 is how the system HANGS.

		 `on: NotFound do: [...]' where NotFound is undeclared leaves nil
		 as the guard.  Sending #handles: to nil is a doesNotUnderstand,
		 which signals, which searches for a handler, which finds this same
		 frame and sends #handles: to nil again -- forever, and as a loop
		 rather than a stack overflow, because the search walks the sender
		 chain fresh each time.  Nothing reports and nothing stops.

		 Found by Pharo's own AnnouncerTest, which signals NotFound: a class
		 we have not imported, so the global is nil, so this.  Every
		 unimported exception class in the port will arrive the same way.

		 The nil test is kept because it is free, but it is not the fix: it
		 guards one value, and `on: 3 do:' -- or #foo, or a String, or any
		 object that is not an exception class -- went round the same loop
		 for ever (Bugs3 B4).  What actually stops the loop is that the
		 frame is taken OUT OF SERVICE while its guard is being asked.  A
		 fault inside #handles: then searches outward from a frame that is
		 not listening, reaches the next handler out or the default action,
		 and reports; it cannot come back here.  runHandler does the same
		 thing for the same reason while the handler block runs.  The
		 wrong-guard error itself is Object>>handles:, in this package's
		 Object extension, which says what was wrong in one sentence."
		(guard notNil and: [ctx handlerIsActive]) ifTrue: [
			ctx handlerIsActive: false.
			willing := guard handles: self.
			ctx handlerIsActive: true.
			willing ifTrue: [^ctx]].
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
	"Answer aValue from the on:do: that caught me.

	 The unwind first.  Jumping to the handler's frame throws away every
	 frame between it and the signal, and some of those may be ensure: or
	 ifCurtailed: frames whose whole purpose is to run something on the way
	 out.  Without this line

	     [[Error signal] ensure: [flag := true]] on: Error do: [:e | 'caught']

	 answers 'caught' and never sets the flag -- the file is left open, the
	 lock is left held, and nothing says so.  That is the failure mode
	 ensure: exists to prevent, arriving through the one path nobody
	 tested."
	handlerContext isNil ifTrue: [^aValue].
	self unwindTo: handlerContext.
	^handlerContext return: aValue
]

{ #category : 'private' }
Exception >> unwindTo: aContext [
	"Run every unwind action between the signal and aContext, innermost
	 first -- there may be several nested, and each one's ensure: block
	 has to run before the frames outside it are discarded."
	| unwind |
	signalContext isNil ifTrue: [^self].
	unwind := signalContext findNextUnwindUpTo: aContext.
	[unwind isNil] whileFalse: [
		unwind runUnwindBlock.
		unwind := unwind findNextUnwindUpTo: aContext]
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
	"Same reason as return:: restarting discards the frames in between."
	self unwindTo: handlerContext.
	handlerContext handlerIsActive: true.
	^handlerContext restartAndJump
]

{ #category : 'handling' }
Exception >> retryUsing: aBlock [
	"Run aBlock in place of the protected block, with this handler still
	 armed for whatever aBlock raises.

	 That last clause is the whole of it, and it is what ANSI asks for:
	 retryUsing: is retry with a substitution, not a return with a value
	 computed on the way.  The body used to be

	     self unwindTo: handlerContext.
	     ^handlerContext return: aBlock value

	 which evaluates aBlock in the frame of whoever sent retryUsing: -- the
	 handler block -- and does it AFTER unwindTo: has run the unwind actions
	 between the signal and the handler and while runHandler still has the
	 frame switched off.  So the replacement ran outside the very handler it
	 was meant to be protected by, and

	     [1/0] on: ZeroDivide do: [:e | e retryUsing: [1/0]]

	 reported ZeroDivide out of the top of the image instead of looping
	 (Bugs4 PROC-2).  A replacement that does not raise never noticed.

	 An on:do: activation's receiver IS its protected block, so the
	 substitution is one field, and the rest is retry: unwind to the handler,
	 put it back in service, and restart the frame."
	handlerContext isNil ifTrue: [^self return: nil].
	"Same reason as return: and retry: restarting discards the frames in
	 between, and their ensure: blocks have to run before they go."
	self unwindTo: handlerContext.
	handlerContext handlerIsActive: true.
	^handlerContext restartWithReceiver: aBlock
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
Exception >> outer [
	"Run the handler enclosing the one now running, and come back here if it
	 resumes.

	 outer and pass find the same handler and run it the same way; they
	 differ in one thing, which is where a resumption lands.  pass hands the
	 exception over for good -- if the outer handler resumes, execution
	 carries on from where the exception was SIGNALLED and this handler is
	 finished with.  outer keeps this handler's place: the resumption value
	 comes back as the value of this send, and the handler that sent outer
	 goes on to decide what to do with it.  That is what makes outer the one
	 to use for 'let whoever is above me have a say, then I will act on the
	 answer'.

	 Which is one field.  A resumption goes to signalContext -- see resume:
	 -- so outer points signalContext at its own activation for the
	 duration and puts back what was there if it gets control again.  A
	 handler that RETURNS rather than resumes unwinds to its own on:do: and
	 never comes back here, which is exactly what pass does, and why the two
	 restores below are on the resumption path only.

	 A non-resumable exception has nowhere to come back to, so it is passed
	 outright.  Named in doc/PLAN-PHARO.md and missing until Bugs4 PROC-7."
	| savedSignal savedHandler found result |
	self isResumable ifFalse: [^self pass].
	found := handlerContext isNil
		ifTrue: [nil]
		ifFalse: [self findHandlerFrom: handlerContext sender].
	found isNil ifTrue: [^self defaultAction].
	savedSignal := signalContext.
	savedHandler := handlerContext.
	signalContext := thisContext.
	handlerContext := found.
	result := self runHandler.
	signalContext := savedSignal.
	handlerContext := savedHandler.
	^result
]

{ #category : 'handling' }
Exception >> resignalAs: replacementException [
	"Raise replacementException in place of me, searching for its handler
	 from where I was signalled rather than from here.

	 That start point is the whole of it, and it is what neither pass nor
	 signal can say.  A handler that declined ME -- its on:do: names a class
	 mine is not a kind of -- sits between the signal and this handler, and
	 the replacement may be exactly what it was written for; searching from
	 here would walk straight past it.  So a driver that catches a low-level
	 fault and re-raises it as the failure its callers actually understand
	 uses this, and the caller's own on:do:, two frames below, sees it.

	 The handler now running stays out of service, which is ANSI's rule that
	 a handler is disabled while it is executing and is also what keeps
	 `e resignalAs: e class new' from going round for ever.  If the
	 replacement's handler resumes, the resumption goes where the ORIGINAL
	 signal was sent, since that is the send still waiting for an answer.

	 Named in doc/PLAN-PHARO.md and missing until Bugs4 PROC-7."
	| ctx |
	ctx := signalContext.
	ctx isNil ifTrue: [ctx := thisContext sender].
	replacementException signalContext: ctx.
	^replacementException signalFrom: ctx
]

{ #category : 'private' }
Exception >> signalContext: aContext [
	"Where the search for my handler starts, and where a resumption goes.
	 Written by resignalAs:, which raises one exception from another's
	 signal point; signal and signal: set it for the ordinary case."
	signalContext := aContext
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

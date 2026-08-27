"
Errors become signals.

Object>>error: opened a NotifierView directly, which is why nothing in a
1983 image can catch anything: the window IS the error handling.  It now
signals an Error, and the window moves down into Error>>defaultAction --
which runs only when nobody was listening.  So every existing caller of
error: is unchanged, an unhandled error still opens the same debugger on
the same context, and [...] on: Error do: [...] now works.

doesNotUnderstand: keeps its tryCopyingCodeFor: step first, because that
step succeeds sometimes and a send that can be repaired should be.  Only
when it cannot does the message become a MessageNotUnderstood.
"
Extension { #name : 'Object' }

{ #category : '*Kernel-Exceptions' }
Object >> reportOnStandardError: aString [
	"Say something without drawing it.

	 Transcript is a TextCollector: it draws, and a headless image has no
	 screen -- so the only way it could report was the one way it did not
	 have.  One line to stderr fixes that."
	<primitive: 248>
	^self
]

{ #category : '*Kernel-Exceptions' }
Object >> error: aString [
	^Error new signal: aString
]

{ #category : '*Kernel-Exceptions' }
Object >> doesNotUnderstand: aMessage [
	| status |
	status := self class tryCopyingCodeFor: aMessage selector.
	status == #OK ifTrue: [
		^self perform: aMessage selector withArguments: aMessage arguments].
	^MessageNotUnderstood new
		message: aMessage;
		receiver: self;
		signal
]


{ #category : '*Kernel-Exceptions' }
Object >> errorSubscriptBounds: index [
	"Raise the index error as its own class.

	 Every indexed access in the image funnels through this one method, so
	 overriding it here gives the whole system a catchable index error
	 without touching the frozen sources or any of the places that detect
	 the condition.

	 Deliberately the same shape as Object>>error: just above, differing
	 only in the class raised.  A first version carried the index in a slot
	 and built its text in a messageText override, and the bootstrap hung:
	 an exception raised out of this method must not do anything that could
	 itself land back in this method, and the plainest possible signal: is
	 the only version that cannot."
	^SubscriptOutOfBounds new signal:
		'subscript is out of bounds: ', index printString
]

{ #category : '*Kernel-Exceptions' }
Object >> at: index [
	"An indexed field of me.  Fail if index is not an Integer or is out of
	 bounds.

	 REWRITTEN BECAUSE THE ERROR RETURNS.  Error>>defaultAction reports,
	 unwinds and then ANSWERS -- see the argument in its comment, which is
	 about not terminating a process that a test runner may be -- and the
	 1983 fallback here was written on the opposite assumption, that the
	 error method never comes back.  It read

		index isInteger ifTrue: [self errorSubscriptBounds: index].
		(index isKindOf: Number)
			ifTrue: [^self at: index truncated]
			ifFalse: [self errorNonIntegerIndex]

	 so `#(1 2 3) at: 99' reported, returned, fell through to `99 truncated'
	 -- which is 99 -- and sent `self at: 99' again.  Forever.  A headless
	 image printed 28,529 identical lines in 25 seconds and never stopped,
	 which in the field is a hang and a growing log rather than a fault.
	 at:put:, basicAt: and basicAt:put: had the same shape.

	 So the error is RETURNED rather than fallen past, and the Number arm is
	 gone with it.  Truncating a Float subscript was the other half of the
	 loop -- `#(1 2 3) at: 1.5' answered 1 -- and Squeak and Pharo both
	 raise there, because an index that is not an integer is a caller's
	 mistake and answering an approximation of it hides that.

	 Nothing about this makes an unhandled error stop the process; anything
	 with a handler behaves exactly as before."

	<primitive: 60>
	index isInteger ifFalse: [^self errorNonIntegerIndex].
	^self errorSubscriptBounds: index
]

{ #category : '*Kernel-Exceptions' }
Object >> at: index put: value [
	"Store value in one of my indexed fields, and answer it.

	 See Object>>at: for why the error is returned rather than fallen past.
	 The extra case here is 1983's: an index that IS in bounds means the
	 primitive refused the value, not the subscript -- a Character into a
	 ByteArray, say -- and that is a different sentence."

	<primitive: 61>
	index isInteger ifFalse: [^self errorNonIntegerIndex].
	(index >= 1 and: [index <= self size]) ifTrue: [^self errorImproperStore].
	^self errorSubscriptBounds: index
]

{ #category : '*Kernel-Exceptions' }
Object >> basicAt: index [
	"An indexed field of me, not to be overridden.  See Object>>at:."

	<primitive: 60>
	index isInteger ifFalse: [^self errorNonIntegerIndex].
	^self errorSubscriptBounds: index
]

{ #category : '*Kernel-Exceptions' }
Object >> basicAt: index put: value [
	"Store into an indexed field of me, not to be overridden.
	 See Object>>at:put:."

	<primitive: 61>
	index isInteger ifFalse: [^self errorNonIntegerIndex].
	(index >= 1 and: [index <= self basicSize]) ifTrue: [^self errorImproperStore].
	^self errorSubscriptBounds: index
]

{ #category : '*Kernel-Exceptions' }
Object >> mustBeBoolean [
	"A conditional jump was given me, and I am neither true nor false.

	 Sent by the interpreter, which rewinds to the jump before sending, so
	 WHAT THIS ANSWERS IS RETESTED by the same bytecode.  1983's version
	 ends `^true' and calls that 'proceed for truth', and that is kept: an
	 unhandled error reports, unwinds and returns here, true goes back, and
	 the conditional takes its true arm rather than losing the process.

	 The two-way answer at the end is not decoration.  Since the value is
	 retested, an answer that is itself not a Boolean would send this again
	 and again -- exactly the shape of loop that made an unhandled
	 `#(1 2 3) at: 99' print 28,529 lines and never stop.  A handler may
	 return anything at all, so whatever comes back is reduced here to one
	 of exactly two objects and the retest is guaranteed to terminate.

	 The interpreter halts only when no mustBeBoolean can be found at all,
	 which happens during the bootstrap before Object has methods."
	| answer |
	answer := NonBooleanReceiver new
		object: self;
		signal: self printString , ' is not a Boolean, so it cannot decide a '
			, 'conditional -- proceed for truth'.
	^answer == false ifTrue: [false] ifFalse: [true]
]

{ #category : '*Kernel-Exceptions' }
Object >> outOfMemory [
	"The interpreter could not allocate the context for a send to me.

	 Sent by the VM in place of the send that failed, so what this answers
	 is what that send answers.  See OutOfMemory for why raising is possible
	 here at all, and for what happens to a program that does not handle it.

	 The message text is deliberately a constant String rather than a
	 report built from the numbers: building one means allocating one, and
	 there is a reserve of exactly enough room to raise this and no more.
	 The numbers go to stderr from the VM if the reserve is spent and the
	 process stops."
	^OutOfMemory new signal: 'out of room: no object table entry for a method''s context'
]

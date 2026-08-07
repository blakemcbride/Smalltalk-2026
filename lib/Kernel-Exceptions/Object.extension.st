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

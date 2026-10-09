Extension { #name : 'MessageTally' }

{ #category : '*System' }
MessageTally >> into: aCollection [
	"Fold my tally into the one that collection already holds for my method,
	 adding a fresh tally if it holds none.

	 1983 writes this as `aCollection find: self ifAbsent: [...]' followed by
	 `(aCollection basicAt: index) bump: tally' -- it asks a Set for the
	 INDEX of an element and then reads the element back out of the Set's
	 indexed part by hand.  Both halves are 1983 Set internals: Pharo's Set
	 keeps its elements in an `array' instance variable, so it has no
	 find:ifAbsent: and its indexed part is not where the elements are.
	 Restoring find:ifAbsent: alone would have answered an index into a
	 place basicAt: does not read.

	 So this asks the question directly instead.  detect:ifNone: uses = ,
	 which is what the Set was using through findElementOrNil: anyway --
	 MessageTally>>= compares methods by identity -- so the answer is the
	 same one, arrived at without reaching inside anybody."
	| existing |
	existing := aCollection detect: [:each | each = self] ifNone: [nil].
	existing isNil ifTrue: [
		existing := aCollection add:
			(MessageTally new class: class method: method)].
	existing bump: tally
]

{ #category : '*System' }
MessageTally >> spyEvery: millisecs on: aBlock [
	"Create a spy on the given block at the specified rate.  Don't run it
	 yet.

	 1983 asked for a BlockContext exactly, and every block in every
	 profile but bluebook is a BlockClosure, so the profiler's one entry
	 point refused every block; the error returned, continueSpying went
	 on with no block, and the method died on nil (Bugs6 KERNB-8).  Both
	 kinds of block answer isBlock, receiver and method.  The four class
	 variables are 1983's and stay: one spy at a time per image."
	aBlock isBlock ifFalse: [^self error: 'spy needs a block here'].
	self class: aBlock receiver class method: aBlock method.
	ObservedProcess := Processor activeProcess.
	HowOften := millisecs.
	TargetBlock := aBlock
]

{ #category : '*System' }
MessageTally class >> spyOn: aBlock [
	"An execution profile of aBlock: the block is run under the spy, the
	 report goes to the Transcript, and the block's value is answered.

	 1983 opened the report in a workspace and answered nothing; a
	 headless image has no screen for the workspace, and StringHolderView
	 died in a view that was never shown (Bugs6 KERNB-8).  Pharo writes
	 to the Transcript and answers the value, and so does this."
	| spy value stream |
	spy := self new.
	spy spyEvery: spy defaultInterval on: aBlock.
	value := spy continueSpying.
	stream := WriteStream on: (String new: 5000).
	spy reportOn: stream; close.
	Transcript show: stream contents; cr.
	^value
]

{ #category : '*System' }
MessageTally >> continueSpying [
	"Run the block under the timer and answer its value.

	 1983's timer loop read a temporary of this method -- `active' -- to
	 know when to stop, and read it once more after the method had
	 returned; here a returned context's temporaries are nil, so the
	 timer's last look was `nil is not a Boolean' (Bugs6 KERNB-8).  The
	 timer is terminated when the block ends instead, and it samples
	 only a process that is parked -- under -serve the observed process
	 may be running on another worker, and has no suspendedContext to
	 tally."
	| myDelay |
	myDelay := Delay forMilliseconds: HowOften.
	Timer := [[true] whileTrue: [ | context |
			myDelay wait.
			(context := ObservedProcess suspendedContext) isNil
				ifFalse: [self tally: context]]] newProcess.
	Timer priority: Processor userInterruptPriority.
	Timer resume.
	^[TargetBlock value] ensure: [
		Timer notNil ifTrue: [Timer terminate].
		Timer := nil]
]

{ #category : '*System' }
MessageTally >> defaultInterval [
	"The number of milliseconds between samples.  1983 asked `Smalltalk
	 frills', a flag this image never sets, and nil is not a Boolean: the
	 spy died before it had started (Bugs6 KERNB-8).  Sixteen, the
	 answer the flag gave when it was on."
	^16
]

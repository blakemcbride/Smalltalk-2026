"
The exception system, asserted from what on:do: and ensure: promise rather
than from what they happened to do.
"
Class {
	#name : 'St80ExceptionTest',
	#superclass : 'TestCase',
	#category : 'Library-Tests'
}

{ #category : 'guards' }
St80ExceptionTest >> testAGuardThatIsNotAnExceptionClassIsRefused [
	"`on: 3 do:' hung the process for ever: asking 3 whether it handles
	 the exception raised a doesNotUnderstand whose own search came back
	 to the same frame and asked again (Bugs3 B4).  The frame is out of
	 service while its guard is asked, and Object>>handles: says what was
	 wrong; the error reaches the handler outside, and the code after it
	 runs."
	| outer |
	#(3 #foo 'str') do: [:guard |
		outer := [[Error new signal: 'x'] on: guard do: [:e | #inner]]
			on: Error do: [:e | e messageText].
		self assert: (outer indexOfSubCollection: 'cannot be the guard' startingAt: 1) > 0].
	self assert: ([[Error new signal: 'x'] on: 3 do: [:e | 0]] on: Error do: [:e | 41]) + 1
		equals: 42.
	self assert: ([Error new signal: 'x'] on: Error do: [:e | #caught]) equals: #caught.
	self assert: ([[Error new signal: 'x'] on: nil do: [:e | #inner]] on: Error do: [:e | #outer])
		equals: #outer
]

{ #category : 'unwinding' }
St80ExceptionTest >> testEnsureBlockRunsOnce [
	"An ensure: block that raised, or returned non-locally, ran twice --
	 the second time on the unwind through the frame that had just run it
	 -- and the second signal escaped the handler that caught the first
	 (Bugs3 B18)."
	| log r n |
	log := OrderedCollection new.
	r := [[log add: #body. 1] ensure: [log add: #ensure. Error new signal: 'in ensure']]
		on: Error do: [:e | log add: e messageText. e return: 9].
	self assert: log asArray equals: #(#body #ensure 'in ensure').
	self assert: r equals: 9.
	log := OrderedCollection new.
	self assert: (self ensureReturningThrough: log) equals: #nlr.
	self assert: log asArray equals: #(1 2).
	n := 0.
	[1] ensure: [n := n + 1].
	self assert: n equals: 1.
	log := OrderedCollection new.
	[[[Error new signal: 'x'] ensure: [log add: 2]] ensure: [log add: 3]]
		on: Error do: [:e | e return: nil].
	self assert: log asArray equals: #(2 3).
	log := OrderedCollection new.
	[[log add: #body. Error new signal: 'boom'] ifCurtailed: [log add: #curtailed]]
		on: Error do: [:e | e return: nil].
	self assert: log asArray equals: #(#body #curtailed)
]

{ #category : 'private' }
St80ExceptionTest >> ensureReturningThrough: log [
	"A ^ from inside an ensure: block, which unwinds through the frame
	 that is running it."
	[log add: 1. 1] ensure: [log add: 2. ^#nlr].
	^#fell
]

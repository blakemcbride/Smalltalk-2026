"
An error: something went wrong and there is no sensible value to carry on
with.  Not resumable.
"
Class {
	#name : 'Error',
	#superclass : 'Exception',
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
Error >> description [
	^'An error has occurred'
]

{ #category : 'handling' }
Error >> defaultAction [
	"Nobody was listening.

	 The text goes out FIRST and the window second, deliberately.  An
	 unhandled error still reaches the same 1983 NotifierView on the same
	 context -- that has to keep working, it is the debugger.  But opening
	 a window is useless in a headless build and worse than useless in a
	 test, and an error that can only be seen by drawing cannot be seen at
	 all when there is nothing to draw on.  Saying it in words costs one
	 line and means the report survives either way.

	 And when there is nothing to draw on, the CLEANUP still runs.  Opening
	 a notifier headless parks the process in a window nothing can dismiss,
	 holding every lock it held: one worker raising an error inside
	 `aMutex critical:' left every other worker blocked on a mutex that
	 would never be released, and the run hung rather than failed.  So the
	 unwind actions between the signal and the bottom are run here --
	 every ensure: and ifCurtailed: block, innermost first -- and the lock
	 is released on the way past.

	 Unwinding and then ANSWERING is deliberate, and terminating the
	 process is not what to do instead.  A test runner catching its own
	 failures is a process like any other, and terminating it took the
	 whole pharo-time suite down with the first test that signalled outside
	 a handler -- `did not report at all', which is the shape of failure
	 the ratchet exists to catch.  Answering is safe because
	 runUnwindBlock DISARMS each frame it runs: when those frames later
	 return normally their ensure: blocks are already nil and do nothing.
	 So the cleanup happens once, here, and execution carries on exactly as
	 it did before -- which for an unhandled error is not ideal and is a
	 separate argument from this one.

	 THE REPORT IS ITSELF PROTECTED, because reporting runs image code and
	 image code can fail.  reportAsText builds its line with `,', which is
	 SequenceableCollection>>copyEmpty:, which sends #species -- so
	 `Object removeSelector: #species' made the report of the resulting
	 doesNotUnderstand raise another doesNotUnderstand, whose report raised
	 another, for ever: one line of ordinary reflection and the image hung
	 with a worker spinning in the handler search, no output and nothing to
	 kill but the process (Bugs4 REFLECTION-7).  Every other core removal
	 in that sweep came back as a clean MessageNotUnderstood, and this one
	 differed only because #species is on the path the reporter itself
	 takes.

	 The fallback line is a literal and sends nothing but
	 reportOnStandardError:, which is a primitive: whatever has been
	 removed, that much can still be said.  Catching Error rather than the
	 narrower class is deliberate -- the point is that NOTHING raised while
	 describing an unhandled error may start the cycle again, not that
	 #species in particular is missing."
	self noteUnhandled.
	[self reportAsText]
		on: Error
		do: [:inner | self reportOnStandardError:
				'an error was raised that could not describe itself'].
	self canOpenNotifier ifTrue: [^self openNotifier].
	self unwindTo: nil.
	^nil
]

{ #category : 'private' }
Error >> noteUnhandled [
	"Tell the VM that an Error went unhandled on this stack (Bugs3 B58).

	 Sent from defaultAction and nowhere else, so what the VM counts is
	 exactly the errors nobody caught: a handled Error never reaches
	 defaultAction, and a Warning has its own.  -eval reads the count to
	 decide its exit code -- the answer to `3 zork' is nil, which a build
	 script could not tell from an expression that answered nil on purpose.
	 Primitive 239, this system's own; a build without it answers self."
	<primitive: 239>
	^self
]

{ #category : 'private' }
Error >> canOpenNotifier [
	"Whether there is a user interface that could run a debugger.

	 Sensor alone does not answer it: an InputSensor exists in every image
	 including a headless one, so `Sensor isNil' -- which is what this used
	 to ask -- was false in exactly the builds that cannot draw.  What is
	 actually needed is a controller driving the screen, and
	 ScheduledControllers has an active one only while the desktop is
	 pumping.  A bootstrap, a test run and a doIt all answer nil.

	 The test itself moved to Object>>canOpenUserInterface when halt,
	 halt:, notify:, confirm: and inspect were given the same guard.
	 Several places asking the same question is fine; several places each
	 deciding what the question means is how they drift apart."
	^self canOpenUserInterface
]

{ #category : 'private' }
Error >> reportAsText [
	^self reportOnStandardError:
		self class name , ': ' , self messageText printString
]

{ #category : 'private' }
Error >> openNotifier [
	Sensor isNil ifTrue: [^nil].
	^NotifierView
		openContext: signalContext
		label: self messageText
		contents: signalContext shortStack
]

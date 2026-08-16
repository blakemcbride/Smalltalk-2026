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
	 separate argument from this one."
	self reportAsText.
	self canOpenNotifier ifTrue: [^self openNotifier].
	self unwindTo: nil.
	^nil
]

{ #category : 'private' }
Error >> canOpenNotifier [
	"Whether there is a user interface that could run a debugger.

	 Sensor alone does not answer it: an InputSensor exists in every image
	 including a headless one, so `Sensor isNil' -- which is what this used
	 to ask -- was false in exactly the builds that cannot draw.  What is
	 actually needed is a controller driving the screen, and
	 ScheduledControllers has an active one only while the desktop is
	 pumping.  A bootstrap, a test run and a doIt all answer nil."
	^Sensor notNil
		and: [ScheduledControllers notNil
		and: [ScheduledControllers activeController notNil]]
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

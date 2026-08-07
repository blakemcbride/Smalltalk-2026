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
	 line and means the report survives either way."
	self reportAsText.
	^self openNotifier
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

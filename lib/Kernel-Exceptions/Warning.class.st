"A warning: worth saying, and resumable."
Class {
	#name : 'Warning',
	#superclass : 'Exception',
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
Warning >> description [
	^'An exception has occurred'
]

{ #category : 'testing' }
Warning >> isResumable [
	^true
]

{ #category : 'handling' }
Warning >> defaultAction [
	"Unhandled warnings say so on standard error and carry on.

	 Carrying on is what makes a Warning a Warning; saying so is what makes
	 one worth signalling.  It answered nil silently until halt, halt:,
	 notify:, confirm: and inspect started signalling one where 1983 opened
	 a window nobody could dismiss -- and a `self halt' left in a method by
	 accident would then have gone from hanging the worker to doing
	 absolutely nothing, which is a different way of being invisible.

	 Standard error, and in the same words Error>>reportAsText uses, so the
	 two kinds of report read alike."
	self reportOnStandardError:
		self class name , ': ' , self messageText printString.
	^nil
]

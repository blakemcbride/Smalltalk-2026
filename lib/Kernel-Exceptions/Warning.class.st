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
	"Unhandled warnings carry on."
	^nil
]

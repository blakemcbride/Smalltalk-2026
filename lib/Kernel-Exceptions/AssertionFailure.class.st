"
What assert: signals.

An Error rather than a Warning: a failed assertion means the program's own
statement about itself was wrong, and carrying on from there is not a
recovery, it is a guess.
"
Class {
	#name : 'AssertionFailure',
	#superclass : 'Error',
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
AssertionFailure >> description [
	^'Assertion failed'
]

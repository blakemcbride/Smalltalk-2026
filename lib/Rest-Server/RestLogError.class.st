"
Worth a line in the log, and no more.
"
Class {
	#name : 'RestLogError',
	#superclass : 'RestError',
	#category : 'Rest-Server'
}

{ #category : 'logging' }
RestLogError >> logLevel [
	^#warn
]

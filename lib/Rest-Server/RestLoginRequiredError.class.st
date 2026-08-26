"
The request needs a logged-in user and has none: _ErrorCode 2, which Kiss's
front end reads as `go to the login page'.
"
Class {
	#name : 'RestLoginRequiredError',
	#superclass : 'RestError',
	#category : 'Rest-Server'
}

{ #category : 'accessing' }
RestLoginRequiredError >> errorCode [
	^2
]

{ #category : 'logging' }
RestLoginRequiredError >> logLevel [
	^#none
]

"
What a service raises to answer a request with a failure, and how the failure
is logged.

Kiss has three exception classes for the three things a server operator wants
to know, and they are kept here under one root:

    RestUserError     the user did something wrong -- a bad password, an
                      order for a product that is not there.  Not logged at
                      all: it is the user's news, not the operator's.
    RestLogError      worth a line in the log, without a stack: a condition
                      the service saw coming.
    RestServerError   a fault: logged with everything known about it.

Any other Error inside a service is treated as a RestServerError.  Every one
carries an errorCode for the response's _ErrorCode -- -1 unless the service
said otherwise -- and RestLoginRequiredError is the one the front end tests
for, code 2, which sends the user back to the login page.
"
Class {
	#name : 'RestError',
	#superclass : 'Error',
	#instVars : [ 'errorCode' ],
	#category : 'Rest-Server'
}

{ #category : 'instance creation' }
RestError class >> code: anInteger signal: aString [
	^self new errorCode: anInteger; signal: aString
]

{ #category : 'accessing' }
RestError >> errorCode [
	^errorCode ifNil: [-1]
]

{ #category : 'accessing' }
RestError >> errorCode: anInteger [
	errorCode := anInteger
]

{ #category : 'accessing' }
RestError >> description [
	^'A REST service could not answer'
]

{ #category : 'logging' }
RestError >> logLevel [
	"#none, #warn or #error: what the dispatcher does with this."
	^#error
]

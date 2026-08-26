"
A request that cannot be served, as a status and a reason.

Raised anywhere between reading the request and writing the response, and
turned into that response by HttpConnection: a body too large is 413, a
request line that is not one is 400, a path that climbs out of the document
root is 403.  Any other Error inside a handler is a 500 that names nothing
about the fault to the client and everything about it on standard error.
"
Class {
	#name : 'HttpError',
	#superclass : 'Error',
	#instVars : [ 'status' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'instance creation' }
HttpError class >> status: anInteger reason: aString [
	^self new status: anInteger; signal: aString
]

{ #category : 'accessing' }
HttpError >> status [
	^status ifNil: [500]
]

{ #category : 'accessing' }
HttpError >> status: anInteger [
	status := anInteger
]

{ #category : 'accessing' }
HttpError >> description [
	^'An HTTP request could not be served'
]

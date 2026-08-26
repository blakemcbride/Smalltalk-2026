"
Something the network refused, or something this package asked of it wrongly.

One class and not a hierarchy, for the reason DbError is one class: the useful
distinction is not between kinds of network failure but between the message the
OPERATING SYSTEM wrote and anything this image might invent instead.  So the
system's own text -- `connect: Connection refused', `recv: Connection reset by
peer' -- is carried through untouched, and errno answers the number behind it
for a caller that wants to tell one refusal from another.
"
Class {
	#name : 'NetError',
	#superclass : 'Error',
	#instVars : [ 'errno' ],
	#category : 'Network'
}

{ #category : 'accessing' }
NetError >> description [
	^'A network error has occurred'
]

{ #category : 'accessing' }
NetError >> errno [
	"The operating system's number for the failure, or nil when this image
	 raised without asking it -- a closed socket, a bad argument."
	^errno
]

{ #category : 'accessing' }
NetError >> errno: anInteger [
	errno := anInteger
]

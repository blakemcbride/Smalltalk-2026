"
Raised in a process that has run out of the time somebody gave it.

The tag says WHO ran out.  BlockClosure>>valueWithin:onTimeout: nests --
an outer block with a long budget can contain an inner one with a short
budget -- so a process may have more than one watchdog aimed at it, and a
handler must be able to tell whether the timeout that arrived is the one it
is responsible for or one belonging to a frame further out.  Without the
tag the innermost handler catches every timeout, including the outer one it
should have passed along, and the outer budget silently stops meaning
anything.
"
Class {
	#name : 'TimedOut',
	#superclass : 'Error',
	#instVars : [ 'tag' ],
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
TimedOut >> tag [
	^tag
]

{ #category : 'accessing' }
TimedOut >> tag: anObject [
	tag := anObject
]

{ #category : 'printing' }
TimedOut >> messageText [
	^messageText ifNil: ['timed out']
]

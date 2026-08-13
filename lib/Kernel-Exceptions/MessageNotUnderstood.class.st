"
A message nobody implements.

doesNotUnderstand: signals one of these, so a send that goes nowhere is a
catchable event rather than only a window.
"
Class {
	#name : 'MessageNotUnderstood',
	#superclass : 'Error',
	#instVars : [ 'message', 'receiver' ],
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
MessageNotUnderstood >> message [
	^message
]

{ #category : 'accessing' }
MessageNotUnderstood >> message: aMessage [
	message := aMessage
]

{ #category : 'accessing' }
MessageNotUnderstood >> receiver [
	^receiver
]

{ #category : 'accessing' }
MessageNotUnderstood >> receiver: anObject [
	receiver := anObject
]

{ #category : 'testing' }
MessageNotUnderstood >> isResumable [
	"Answering something from the handler is a perfectly good outcome."
	^true
]

{ #category : 'accessing' }
MessageNotUnderstood >> description [
	"Name the RECEIVER's class as well as the selector.

	 A bare `Message not understood: at:ifAbsent:' says which method is
	 missing and not which class should have had it, and those are different
	 questions -- the second is the one that leads anywhere.  Finding the
	 receiver of a doesNotUnderstand: without this means adding a handler,
	 rebuilding, and running again, and I have done that twice: once chasing
	 `second' through the Chronology suites and once chasing this very
	 selector through Collections-Unordered.

	 Guarded, because this runs on the error path and must not raise a
	 second error while reporting the first: a receiver whose class chain is
	 broken is exactly the kind of object that gets here."
	| where |
	message isNil ifTrue: [^'Message not understood'].
	where := [receiver class name] on: Error do: [:e | e return: nil].
	^where isNil
		ifTrue: ['Message not understood: ', message selector]
		ifFalse: ['Message not understood: ', message selector,
			' (sent to a ', where, ')']
]

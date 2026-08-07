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
	^message isNil
		ifTrue: ['Message not understood']
		ifFalse: ['Message not understood: ', message selector]
]

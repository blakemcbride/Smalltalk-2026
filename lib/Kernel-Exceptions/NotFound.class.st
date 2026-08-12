"
Something was looked for in a collection and was not there.

Pharo raises this from at:, remove: and their relatives, and code that
catches it is common enough that an unimported NotFound is a real gap
rather than a curiosity: `[...] on: NotFound do: [...]' with the global
undeclared leaves nil as the guard, and until Exception>>findHandlerFrom:
was taught to skip a nil guard, that HUNG the image rather than failing.

Ours rather than Pharo's file, because Pharo's keeps the collection in an
inherited `signaler' slot that our Exception does not have.  The protocol
that callers actually use -- signal, signalFor:, signalFor:in:, object,
collection -- is the same.
"
Class {
	#name : 'NotFound',
	#superclass : 'Error',
	#instVars : [ 'object', 'collection' ],
	#category : 'Kernel-Exceptions'
}

{ #category : 'instance creation' }
NotFound class >> signalFor: anObject [
	^self new object: anObject; signal
]

{ #category : 'instance creation' }
NotFound class >> signalFor: anObject in: aCollection [
	^self new object: anObject; collection: aCollection; signal
]

{ #category : 'accessing' }
NotFound >> object [ ^object ]

{ #category : 'accessing' }
NotFound >> object: anObject [ object := anObject ]

{ #category : 'accessing' }
NotFound >> collection [ ^collection ]

{ #category : 'accessing' }
NotFound >> collection: aCollection [ collection := aCollection ]

{ #category : 'accessing' }
NotFound >> messageText [
	"A standard text when nobody set one, which is the usual case for an
	 exception raised by a collection rather than by a person."
	messageText isNil ifFalse: [^messageText].
	object isNil ifTrue: [^'object not found'].
	^'object not found: ' , object printString
]

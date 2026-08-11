"
A queue that many native threads may use at once.

The 1983 SharedQueue exists and is not this one.  It guards an
OrderedCollection with a Semaphore counting items and another for mutual
exclusion, which is correct when ''concurrent'' means green processes
interleaved by one interpreter and wrong when eight of them are inside it
at the same instant.

Built on Monitor instead, so there is exactly one lock and one place where
waiting happens.  next blocks until there is something; nextPut: never
blocks and wakes whoever is waiting.

The re-test in the waiting loop is not defensive coding, it is load
bearing: two consumers can be woken by one arrival, and the loser must go
back to sleep rather than remove from an empty collection.
"
Class {
	#name : 'SharedQueue',
	#superclass : 'Object',
	#instVars : [ 'monitor', 'items' ],
	#category : 'Concurrency'
}

{ #category : 'instance creation' }
SharedQueue class >> new: anInteger [
	"Answer a new queue, ignoring the capacity hint.

	 This exists because the 1983 InputState says `SharedQueue new: 50',
	 and this class replaces the 1983 one rather than sitting beside it.
	 The size was a hint to a fixed-size circular buffer; an
	 OrderedCollection grows, so there is nothing to pre-arrange and
	 nothing to overflow."
	^self new
]

{ #category : 'initialization' }
SharedQueue >> initialize [
	monitor := Monitor new.
	items := OrderedCollection new
]

{ #category : 'accessing' }
SharedQueue >> nextPut: anObject [
	"Add anObject and wake anything waiting.  Never blocks."
	monitor critical: [
		items addLast: anObject.
		monitor signalAll].
	^anObject
]

{ #category : 'accessing' }
SharedQueue >> next [
	"Answer the next item, waiting until there is one."
	^monitor critical: [
		monitor waitUntil: [items isEmpty not].
		items removeFirst]
]

{ #category : 'accessing' }
SharedQueue >> nextOrNil [
	"Answer the next item, or nil at once if there is none."
	^monitor critical: [
		items isEmpty ifTrue: [nil] ifFalse: [items removeFirst]]
]

{ #category : 'testing' }
SharedQueue >> isEmpty [
	"True if empty at the moment of asking, which is all that can be
	 promised while others are running."
	^monitor critical: [items isEmpty]
]

{ #category : 'accessing' }
SharedQueue >> size [
	^monitor critical: [items size]
]

{ #category : 'printing' }
SharedQueue >> printOn: aStream [
	aStream nextPutAll: 'a SharedQueue('.
	self size printOn: aStream.
	aStream nextPut: $)
]

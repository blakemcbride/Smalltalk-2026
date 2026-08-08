"
A fixture for ensure: and ifCurtailed:.

Every case here needs a real method, because in a doIt a ^ targets the doIt
itself and there is nothing left to observe afterwards.  Trace records the
order things happened in, which is the whole question with unwinding.
"
Class {
	#name : 'Unwind',
	#superclass : 'Object',
	#classVars : [ 'Trace' ],
	#category : 'Probe-Core'
}

{ #category : 'tracing' }
Unwind class >> reset [
	Trace := OrderedCollection new
]

{ #category : 'tracing' }
Unwind class >> trace [
	^Trace isNil ifTrue: [OrderedCollection new] ifFalse: [Trace]
]

{ #category : 'tracing' }
Unwind class >> note: aSymbol [
	self trace add: aSymbol.
	Trace := self trace
]

{ #category : 'cases' }
Unwind class >> normal [
	"No early exit: the protected block finishes and the ensure block runs
	 after it."
	[self note: #body] ensure: [self note: #unwound].
	self note: #after.
	^#normal
]

{ #category : 'cases' }
Unwind class >> earlyReturn [
	"A ^ out through an ensure:.  The unwind block must run, and #after
	 must not."
	[self note: #body. ^#early] ensure: [self note: #unwound].
	self note: #after.
	^#late
]

{ #category : 'cases' }
Unwind class >> nested [
	"Two ensure: frames between the ^ and its home.  Both run, innermost
	 first."
	[[self note: #body. ^#deep] ensure: [self note: #inner]]
		ensure: [self note: #outer].
	^#late
]

{ #category : 'cases' }
Unwind class >> escapingBlock [
	"Answers a block whose ^ targets THIS method -- which has returned by
	 the time anybody evaluates it.  That is what cannotReturn: is for, and
	 until something implemented it the VM stopped quietly and kept the
	 value, which looks exactly like success."
	^[ ^#escaped ]
]

{ #category : 'cases' }
Unwind class >> contextTakingTwoArgs: a and: b [
	"Two arguments and one temporary, so numArgs is 2 and numTemps is 3.
	 Answers its own frame, which is safe to restart afterwards because
	 restart only assigns fields."
	| t |
	t := a + b.
	^thisContext
]

{ #category : 'cases' }
Unwind class >> curtailedNormally [
	"ifCurtailed: runs its block only when the receiver does NOT finish."
	[self note: #body] ifCurtailed: [self note: #curtailed].
	^#normal
]

{ #category : 'cases' }
Unwind class >> curtailedEarly [
	[self note: #body. ^#early] ifCurtailed: [self note: #curtailed].
	^#late
]

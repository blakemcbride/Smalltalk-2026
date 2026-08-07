"
A block that is a closure rather than a context.

A Blue Book BlockContext is the closure and the activation record at once,
which is why blocks there are not re-entrant: activating one writes the
instruction pointer, the stack pointer and the caller into the very object
somebody is holding, and its arguments live in the home method's frame
rather than its own.  A BlockClosure holds only what makes it that block --
where its code starts, how many arguments it takes, and the values it
captured -- and every activation gets a context of its own.

The shape is Squeak's, deliberately, because the primitive numbers below
are the ones Pharo source declares and ported methods have to keep working
unedited:

	outerContext	the context this closure was made in
	startpc		the first bytecode of the body, one-relative
	numArgs		how many arguments value: expects
	(indexed)	the copied values, one per captured variable
"
Class {
	#name : 'BlockClosure',
	#superclass : 'Object',
	#type : 'variable',
	#instVars : [ 'outerContext', 'startpc', 'numArgs' ],
	#category : 'Kernel-Closures'
}

{ #category : 'accessing' }
BlockClosure >> numArgs [
	^numArgs
]

{ #category : 'accessing' }
BlockClosure >> outerContext [
	^outerContext
]

{ #category : 'accessing' }
BlockClosure >> startpc [
	^startpc
]

{ #category : 'evaluating' }
BlockClosure >> value [
	<primitive: 201>
	^self numArgsError: 0
]

{ #category : 'evaluating' }
BlockClosure >> value: a [
	<primitive: 202>
	^self numArgsError: 1
]

{ #category : 'evaluating' }
BlockClosure >> value: a value: b [
	<primitive: 203>
	^self numArgsError: 2
]

{ #category : 'evaluating' }
BlockClosure >> value: a value: b value: c [
	<primitive: 204>
	^self numArgsError: 3
]

{ #category : 'evaluating' }
BlockClosure >> value: a value: b value: c value: d [
	<primitive: 205>
	^self numArgsError: 4
]

{ #category : 'evaluating' }
BlockClosure >> valueWithArguments: anArray [
	<primitive: 206>
	^self numArgsError: anArray size
]

{ #category : 'evaluating' }
BlockClosure >> valueNoContextSwitch [
	"In this VM a process switch is decided once per bytecode rather than
	at a send, so there is nothing to suppress and this is value."
	<primitive: 221>
	^self numArgsError: 0
]

{ #category : 'evaluating' }
BlockClosure >> valueNoContextSwitch: a [
	<primitive: 222>
	^self numArgsError: 1
]

{ #category : 'private' }
BlockClosure >> numArgsError: n [
	^self error: 'This block accepts a different number of arguments'
]

{ #category : 'controlling' }
BlockClosure >> whileTrue: aBlock [
	^[self value] whileTrue: [aBlock value]
]

{ #category : 'controlling' }
BlockClosure >> whileFalse: aBlock [
	^[self value] whileFalse: [aBlock value]
]

{ #category : 'controlling' }
BlockClosure >> repeat [
	[true] whileTrue: [self value]
]

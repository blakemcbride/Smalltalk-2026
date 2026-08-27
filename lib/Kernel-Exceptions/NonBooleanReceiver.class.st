"
Something that is not true or false was asked to be one.

`x ifTrue: [...] ifFalse: [...]' compiles to a conditional jump, and a
conditional jump has exactly two values it knows what to do with.  Given a
third, 1983's interpreter stopped: it printed a line, dumped a backtrace and
cleared st_vm.running.  There was no send, so there was nothing to catch,
and one bad expression on one worker took every other worker's work with it.

That is worth a class rather than a sentence because of how it ARRIVES.  It
is almost never a deliberate test of a non-Boolean; it is an instance
variable that `new' never set, so the receiver is nil and the fault is a
missing initialisation somewhere else entirely.  `FileDirectory new
printString' reached it from library code.  A handler that wants to catch
that case and nothing else -- a REST worker isolating one request, a test
runner -- needs to be able to name it.

The offending object is kept, because the message text says what it was and
a handler usually wants the object rather than its printString.
"
Class {
	#name : 'NonBooleanReceiver',
	#superclass : 'Error',
	#instVars : [ 'object' ],
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
NonBooleanReceiver >> object [
	"The value the conditional was given."
	^object
]

{ #category : 'accessing' }
NonBooleanReceiver >> object: anObject [
	object := anObject
]

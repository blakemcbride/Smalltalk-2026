"
There was no room to enter a method.

Sent by the interpreter, which could not allocate the context for a method
it had already found.  Before this class the VM printed a line on stderr and
cleared its running flag: no send, so nothing for a handler to catch, and one
runaway recursion took every other worker's work down with it.  Two ordinary
mistakes reach it -- a recursion with no base case, and printString of a
structure that contains itself.

WHAT MAKES IT POSSIBLE TO RAISE AT ALL is worth stating, because raising an
error costs objects and this is raised at the moment there are none.  The
object memory keeps an emergency reserve of table entries and releases it
once, here, so that the Exception, its text and the contexts a handler search
walks through all have somewhere to live.  A handler that unwinds gives the
frames back and the collector re-arms the reserve; a program that ignores the
error and allocates on reaches the raised ceiling with the reserve already
spent, and THAT is where the process stops.  The rope is finite on purpose.

So a handler is not decoration.  A REST worker isolating one request, or a
test runner, can catch this and go on serving; without one the report is the
last thing the image says.
"
Class {
	#name : 'OutOfMemory',
	#superclass : 'Error',
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
OutOfMemory >> description [
	^'there was no room to enter a method'
]

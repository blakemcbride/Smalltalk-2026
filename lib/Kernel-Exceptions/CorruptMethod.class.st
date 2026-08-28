"
A method the interpreter cannot execute: a bytecode that names a literal
past the method's literal frame, an instruction pointer past the method's
end, or a context resumed with an instruction or stack pointer outside its
bounds (Bugs3 B11).

The interpreter abandons the activation and sends #corruptMethod to its
receiver in place of the answer; this is what that raises.  Before it
existed each of those three was a segfault -- the literal index was added
to the frame's base and fetched, and a context's registers were believed as
they stood -- because the Blue Book trusts bytecodes a compiler wrote, and
CompiledMethod>>at:put: and ContextPart's instance variables let anything
write them.

Its own class rather than a plain Error for the reason RecursionDepthExceeded
is: a handler that retries on an Error it does not recognise must not retry
this one, since the same bytes will do the same thing.  Not resumable: the
activation is already gone.
"
Class {
	#name : 'CorruptMethod',
	#superclass : 'Error',
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
CorruptMethod >> description [
	^'The running method cannot be executed'
]

"
A stack that went too deep: a program calling itself with no way out.

The interpreter stops a stack at ST_MAX_CALL_DEPTH frames -- 200,000 by
default -- and sends #recursionDepthExceeded to the receiver of the send it
would not make.  This is what that raises.

Before it existed, a runaway recursion was the end of the process.  It ran
to about five million frames, 4.4 GB resident after five seconds and 12.3
after fifteen, and then printed `out of memory activating a method' and
exited -- taking every worker, every open connection and every request in
flight with it.  Nothing was signalled on the way, so `on: Error do:' around
the call could not help: there was nothing to catch.

An Error and not an OutOfMemory, though the immediate symptom is the same
shape.  The two want opposite responses: running out of room is a program
that needs more of it, and might well succeed on a retry or with a bigger
table; going too deep is a program with a bug in it, and retrying it does
the same thing again.  A handler that treats them alike is wrong about one
of them.

Not resumable, for the same reason: resuming means making the send that was
refused, from the frame that was already too deep.
"
Class {
	#name : 'RecursionDepthExceeded',
	#superclass : 'Error',
	#category : 'Kernel-Exceptions'
}

{ #category : 'accessing' }
RecursionDepthExceeded >> description [
	^'The call stack went deeper than this system allows'
]

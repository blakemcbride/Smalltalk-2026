"
A process is not copied deeply.
"
Extension { #name : 'Process' }

{ #category : '*Kernel-Protocol' }
Process >> deepCopy [
	"Myself.  A process is its suspended context, which is a stack, which
	 reaches the image (Bugs3 B47, and ContextPart>>deepCopy) -- and a
	 second Process holding the same frames would be two schedulable
	 things sharing one stack, which is worse than the recursion.  The
	 scheduler holds processes; a deep copy of anything that holds one
	 must keep holding the same one."
	^self
]

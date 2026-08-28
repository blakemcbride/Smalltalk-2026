"
A context is not copied deeply.
"
Extension { #name : 'ContextPart' }

{ #category : '*Kernel-Protocol' }
ContextPart >> deepCopy [
	"Myself.  A context's sender is a context, whose sender is a context,
	 and the chain ends at the process's first frame after passing every
	 receiver and every method on the stack -- which is how deepCopy of a
	 SortedCollection, whose sortBlock's outerContext is one of these,
	 reached every object in the image (Bugs3 B47).  A copy of an
	 activation that shared no frame with the stack would not be on the
	 stack, so it would not be a copy of anything that could run."
	^self
]

"
A semaphore is not copied deeply.
"
Extension { #name : 'Semaphore' }

{ #category : '*Kernel-Protocol' }
Semaphore >> deepCopy [
	"Myself.  A Semaphore is a LinkedList of the processes waiting on it,
	 and a process reaches the image (Bugs3 B47, and Process>>deepCopy).
	 More to the point, a semaphore is a meeting place: a copy that the
	 signaller holds and the waiter does not is a wait that never ends.
	 Every object that owns one -- Mutex, Monitor, SharedQueue, Delay --
	 keeps the same one in its deep copy."
	^self
]

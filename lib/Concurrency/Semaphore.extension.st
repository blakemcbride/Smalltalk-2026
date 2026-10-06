"
Ending the process that waits longest on a Semaphore.

Delay>>disable sends this to take the waiter off a Delay that is being
cancelled.  1983's version was `self removeFirst terminate': LinkedList's
removeFirst, walking the links from Smalltalk with no lock, while a signal
from another worker takes from the same list under the VM's stripe lock.
Process>>terminate now takes a waiting process off its Semaphore itself,
under that lock (see Process.extension.st), so this has only to say which
process, and the list is never written from here.

And critical:, which in 1983 had no ensure: at all -- `self wait. blockValue
:= aBlock value. self signal' -- so any way out of the block but falling off
its end, a ^, an exception's return:, a terminate, kept the semaphore for
ever (Bugs5 KERN-2).  It has Mutex>>critical:'s shape now; see there.
"
Extension { #name : 'Semaphore' }

{ #category : '*Concurrency' }
Semaphore >> terminateProcess [
	"Terminate the process that has waited longest on me, if any."
	self isEmpty ifFalse: [self first terminate]
]

{ #category : '*Concurrency' }
Semaphore >> critical: mutuallyExcludedBlock [
	"Evaluate mutuallyExcludedBlock holding me, and signal me afterwards
	 however it ends -- but only if this process got me.

	 The wait is inside the protected block, in waitAndMark:, which notes
	 in a cell of this activation's own that the wait completed; the
	 ensure: signals only then.  A process terminated or interrupted while
	 still waiting never got me and must not signal, and one stopped just
	 after its wait did and must: Process>>noteMutexOwnerIn:waiting: tells
	 the two apart, as for Mutex>>waitFor: (Bugs5 OM-5)."
	| got |
	got := Array new: 1.
	^[self waitAndMark: got.
	  mutuallyExcludedBlock value]
		ensure: [(got at: 1) == true ifTrue: [self signal]]
]

{ #category : '*Concurrency' }
Semaphore >> waitAndMark: aCell [
	"Wait for me and record in aCell that I was had -- nothing else, so
	 that a process stopped between the two is recognisable from its top
	 context alone."
	self wait.
	aCell at: 1 put: true
]

"
Ending the process that waits longest on a Semaphore.

Delay>>disable sends this to take the waiter off a Delay that is being
cancelled.  1983's version was `self removeFirst terminate': LinkedList's
removeFirst, walking the links from Smalltalk with no lock, while a signal
from another worker takes from the same list under the VM's stripe lock.
Process>>terminate now takes a waiting process off its Semaphore itself,
under that lock (see Process.extension.st), so this has only to say which
process, and the list is never written from here.
"
Extension { #name : 'Semaphore' }

{ #category : '*Concurrency' }
Semaphore >> terminateProcess [
	"Terminate the process that has waited longest on me, if any."
	self isEmpty ifFalse: [self first terminate]
]

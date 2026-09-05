"
The process that tells an ephemeron its key has died.

An ephemeron -- a class declared `#type : 'ephemeron'' in a Tonel header --
holds its first named field weakly and everything else strongly for exactly
as long as that first field is reachable some other way.  The collector
works that out; the fixed point is in om_mt.c and it has been right for a
while.  What was missing is the sentence at the end of it.

Pharo's WeakKeyDictionary keeps a WeakKeyAssociation per entry, and the
association remembers the dictionary.  When the key goes, the ONLY thing
that can take the entry out of the dictionary is the association, in
Smalltalk, sending `container removeKey: key'.  A collector cannot do that:
it runs with every worker parked, and there is no process to run a send in.
So a WeakKeyDictionary kept every entry it had ever been given, for ever --
the keys were correctly reclaimed and the dictionary was never told, which
is a leak sitting on top of a correct collector (Bugs4 MEM-2).

So the collector queues each such ephemeron in C and signals MournSemaphore,
and this process wakes at userInterruptPriority, takes them one at a time
and sends each one #mourn.  The queue holds what is in it alive -- the
ephemeron AND its key, which is the reason the message can say which entry
to remove -- until the send has been made.  The collection AFTER that finds
nothing holding either and reclaims both, so an entry costs one extra cycle
and not two.

Why a process rather than a drain at the end of garbageCollect: #mourn is
arbitrary Smalltalk written by whoever declared the ephemeron.  It allocates
(removeKey: rehashes), it can raise, and it can take as long as it likes.
None of that can happen inside a collection, and none of it should happen on
whichever process happened to allocate the object that triggered one.

Where the semaphore lives is worth one paragraph, because it is the one
thing about this that an image file does not carry.  The collector holds the
Semaphore as a bare oop in C, told to it by primNextMournedObjectSignalling:,
and the image format stores objects and not the VM's variables -- so an image
written by -bootstrap comes back with the process still on the ready list,
which runs a drain and re-arms it before anything else happens, while an image
snapshotted from a LIVE run comes back with the process asleep and the VM with
nowhere to signal.  The debt is kept rather than dropped, so the first drain
from anywhere re-arms it and delivers whatever piled up; `Finalizer restart' is
that drain.  The delay timer has the same shape of hole for the same reason --
primitive 136's outstanding request is C state too -- and it is what
SystemDictionary>>install was for in 1983.

WeakArray, WeakSet and WeakValueDictionary need nothing here and are the
reason it took an audit to notice this was missing.  They are `#type :
'weak'': the collector nils the dead slot in place and the collection is
correct the instant the sweep ends, with nobody to tell.  An ephemeron is
the case where nilling a slot is not enough, because the object that has to
change is not the one holding the reference.
"
Class {
	#name : 'Finalizer',
	#superclass : 'Object',
	#classVars : [ 'MournSemaphore', 'Worker' ],
	#category : 'Kernel-Finalization'
}

{ #category : 'instance creation' }
Finalizer class >> new [
	"There is nothing to make.  One queue, one process, both class-side.

	 Written out rather than inherited because the loader synthesizes
	 `new ^super new initialize' for any class that inherits #initialize
	 and defines no new of its own, and a class whose only initialize is
	 the class initializer below would get one that means something else."
	^self error: 'Finalizer is not instantiated; the finalization process is class-side'
]

{ #category : 'class initialization' }
Finalizer class >> initialize [
	"Called by the bootstrap, and safe to call again from a Workspace."
	MournSemaphore isNil ifTrue: [MournSemaphore := Semaphore new].
	self restart
]

{ #category : 'accessing' }
Finalizer class >> mournSemaphore [
	"The Semaphore the collector signals.  For tests, which have to be able
	 to run a drain themselves rather than wait on a process."
	^MournSemaphore
]

{ #category : 'accessing' }
Finalizer class >> process [
	"The finalization process, or nil before initialize has run."
	^Worker
]

{ #category : 'private' }
Finalizer class >> restart [
	"Fork the process, replacing any earlier one.

	 At userInterruptPriority, above anything a user writes and below the
	 timer and the I/O processes, which is Pharo's choice and for the same
	 reason: mourning is the tail of a collection and every process in the
	 image is waiting on the memory it releases, so it should not queue
	 behind ordinary work -- and it is Smalltalk of unknown cost, so it
	 must not sit above the clock."
	Worker isNil ifFalse: [Worker terminate].
	Worker := [[true] whileTrue: [
			self drain.
			MournSemaphore wait]]
		forkAt: Processor userInterruptPriority
]

{ #category : 'private' }
Finalizer class >> drain [
	"Send #mourn to everything the collector has queued, and answer how
	 many there were.

	 The semaphore goes with every request, because the two facts belong to
	 the same process: the VM cannot signal before it has been told where,
	 and this is the only place that knows.  Answering nil is what ends the
	 loop; a signal that arrives between that nil and the wait is not lost,
	 since a Semaphore counts its excess signals."
	^self drainSignalling: MournSemaphore
]

{ #category : 'private' }
Finalizer class >> drainSignalling: aSemaphoreOrNil [
	"As drain, but arming the collector to signal aSemaphoreOrNil rather
	 than the one this class waits on.

	 nil disarms it: the collector queues as before and wakes nobody, so
	 the only thing that will ever mourn is whoever calls this again.  That
	 is what a caller driving the queue itself wants -- tests/unit's image
	 harness runs its doIts on a bare context with no Process behind it, so
	 a preemption there has nowhere to park what it interrupted, and a
	 finalization process waking in the middle of one takes the run with
	 it.  The process above never passes nil."
	| each count |
	count := 0.
	[each := self primNextMournedObjectSignalling: aSemaphoreOrNil.
	 each isNil] whileFalse: [
		count := count + 1.
		"An error in one #mourn must not end the process.  It would stop
		 finalization for the rest of the run, and the next symptom would
		 be a dictionary that grows without bound -- a very long way from
		 the method that raised.  Said on stderr, because a headless image
		 has no Transcript to draw on."
		[each mourn]
			on: Error
			do: [:e |
				self reportOnStandardError:
					'st80: ', e class name, ' mourning ',
					each class name, ': ',
					(e messageText ifNil: ['<no message>'])]].
	^count
]

{ #category : 'private' }
Finalizer class >> primNextMournedObjectSignalling: aSemaphore [
	"Answer the oldest ephemeron the collector has queued for #mourn, or
	 nil when there is none, and remember aSemaphore as the one to signal
	 when it queues another.

	 Answers nil when the primitive is absent, which is a VM with no
	 ephemerons in it and therefore nothing that could ever be queued."
	<primitive: 236>
	^nil
]

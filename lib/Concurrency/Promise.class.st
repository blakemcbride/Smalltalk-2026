"
A value that is not there yet.

Fork something, hold on to this, and ask for the value when it is needed;
whoever asks first waits, and everyone who asks afterwards gets the answer
without waiting.

signalError: is here from the beginning, deliberately.  A promise with only
fulfill: has one failure mode and it is the worst one available: the
producer raises, nobody fulfils anything, and every consumer waits for ever
with no indication of why.  A promise that can be broken turns a hang into
an exception raised in the consumer, which is a bug report rather than a
mystery.  Retrofitting it later means auditing every producer, so it is not
retrofitted later.

The state is settled once.  A second fulfil or break is refused rather than
silently believed, because a value that changes after being read is not a
promise.  breakIfUnsettled: is the one exception, and it does not break that
rule: it settles only what nothing has settled, and says so.

value waits without a deadline, and that is a decision rather than an
omission.  A promise cannot know how long the computation behind it should
take, so any timeout it picked would be wrong in one direction or the other
-- the same argument the scheduler's idle loop makes about calling a wait a
deadlock.  What made an unsettled promise a HANG was a producer that could
end without settling, and forkParallel: no longer has one.  A caller that
genuinely has a deadline wants a watchdog process signalling TimedOut into
its own process, which is BlockClosure>>valueWithinMilliseconds:onTimeout:
and belongs there rather than here.
"
Class {
	#name : 'Promise',
	#superclass : 'Object',
	#instVars : [ 'monitor', 'value', 'error', 'settled' ],
	#category : 'Concurrency'
}

{ #category : 'initialization' }
Promise >> initialize [
	monitor := Monitor new.
	settled := false.
	value := nil.
	error := nil
]

{ #category : 'settling' }
Promise >> fulfill: anObject [
	"Settle with a value and wake everyone waiting."
	monitor critical: [
		settled ifTrue: [^Error new signal: 'Promise is already settled'].
		value := anObject.
		settled := true.
		monitor signalAll].
	^anObject
]

{ #category : 'settling' }
Promise >> signalError: anException [
	"Settle with a failure.  Everyone waiting, and everyone who asks
	 later, gets it raised in their own process."
	monitor critical: [
		settled ifTrue: [^Error new signal: 'Promise is already settled'].
		error := anException.
		settled := true.
		monitor signalAll].
	^anException
]

{ #category : 'settling' }
Promise >> breakIfUnsettled: anException [
	"Settle with a failure unless something has already settled me, and
	 answer whether this call was the one that did it.

	 fulfill: and signalError: refuse a second settling on purpose, and that
	 refusal is exactly wrong for the last-resort settling a producer does on
	 its way out: an ensure: block cannot know whether the block it guarded
	 finished, so asking and settling have to be one act under the monitor.
	 Asking first and calling signalError: afterwards is the same test-then-act
	 race Semaphore>>wait had, one Promise wide.

	 Bugs4 PROC-3 is why it exists.  ProcessorScheduler>>forkParallel: guarded
	 its body with `on: Error do:' alone, so a task that was TERMINATED --
	 which raises nothing -- left its promise pending for ever, and
	 Promise>>value, and every parallelCollect: that asked for it, waited on a
	 producer that no longer existed."
	^monitor critical: [
		settled
			ifTrue: [false]
			ifFalse: [
				error := anException.
				settled := true.
				monitor signalAll.
				true]]
]

{ #category : 'accessing' }
Promise >> value [
	"The value, waiting for it if necessary.  If the promise was broken,
	 raise that here -- in the process that asked, which is the one able
	 to do something about it."
	| e |
	e := monitor critical: [
		monitor waitUntil: [settled].
		error].
	e isNil ifFalse: [^e signal].
	^value
]

{ #category : 'accessing' }
Promise >> valueOrNil [
	"The value if it has been settled, else nil, without waiting.  Answers
	 nil for a broken promise too; use isBroken to tell them apart."
	^monitor critical: [
		(settled and: [error isNil]) ifTrue: [value] ifFalse: [nil]]
]

{ #category : 'testing' }
Promise >> isSettled [
	^monitor critical: [settled]
]

{ #category : 'testing' }
Promise >> isBroken [
	^monitor critical: [settled and: [error notNil]]
]

{ #category : 'printing' }
Promise >> printOn: aStream [
	aStream nextPutAll: 'a Promise('.
	aStream nextPutAll: (self isSettled
		ifTrue: [self isBroken ifTrue: ['broken'] ifFalse: ['settled']]
		ifFalse: ['pending']).
	aStream nextPut: $)
]

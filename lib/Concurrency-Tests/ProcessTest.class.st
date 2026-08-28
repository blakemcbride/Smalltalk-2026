"
Stopping a process from outside it, single-threaded: what terminate,
suspend, resume and signalException: do to a process that is waiting or
ready.  Bugs3 B3, B13 and B16 are the faults; the half of B16 that needs a
process running on another core is tests/unit/test_parallel_shared.c,
because a process spinning on the one worker a test run has would never
give this test the processor back.

Processor yield is how a forked process is run up to its first wait here:
the primitive parks this process at the end of its ready list and takes
the forked one, which runs until it waits and hands the worker back.
"
Class {
	#name : 'ProcessTest',
	#superclass : 'TestCase',
	#category : 'Concurrency-Tests'
}

{ #category : 'tests' }
ProcessTest >> testTerminateRunsTheEnsureBlocks [
	| log sem p |
	log := OrderedCollection new.
	sem := Semaphore new.
	p := [[[sem wait. log add: #after] ensure: [log add: #inner]]
			ensure: [log add: #outer]] fork.
	Processor yield.
	p terminate.
	self assert: log asArray equals: #(#inner #outer).
	self assert: p suspendedContext isNil.
	"A second terminate has nothing left to run."
	p terminate.
	self assert: log asArray equals: #(#inner #outer)
]

{ #category : 'tests' }
ProcessTest >> testTerminateReleasesAMutex [
	| m sem p |
	m := Mutex new.
	sem := Semaphore new.
	p := [m critical: [sem wait]] fork.
	Processor yield.
	self assert: m isHeld.
	p terminate.
	self deny: m isHeld.
	self assert: (m critical: [#acquired]) equals: #acquired
]

{ #category : 'tests' }
ProcessTest >> testTerminateAReadyProcess [
	"One that has never run: on its ready list, with the context it was
	 made with."
	| ran p |
	ran := false.
	p := [ran := true] fork.
	p terminate.
	Processor yield.
	self deny: ran.
	self assert: p suspendedContext isNil
]

{ #category : 'tests' }
ProcessTest >> testAProcessThatEndsIsTerminated [
	| p |
	p := [3 + 4] fork.
	Processor yield.
	self assert: p suspendedContext isNil.
	self should: [p resume] raise: Error
]

{ #category : 'tests' }
ProcessTest >> testResumingATerminatedProcessRaises [
	| p |
	p := [Semaphore new wait] fork.
	Processor yield.
	p terminate.
	self should: [p resume] raise: Error.
	self assert: ([p resume. nil] on: Error do: [:e | e messageText])
		equals: 'cannot resume a process that has terminated'
]

{ #category : 'tests' }
ProcessTest >> testResumingTheActiveProcessRaises [
	self assert: ([Processor activeProcess resume. nil] on: Error do: [:e | e messageText])
		equals: 'cannot resume a process that is running'
]

{ #category : 'tests' }
ProcessTest >> testResumingAWaitingProcessRaises [
	| p |
	p := [Semaphore new wait] fork.
	self assert: ([p resume. nil] on: Error do: [:e | e messageText])
		equals: 'cannot resume a process that is already waiting'.
	p terminate
]

{ #category : 'tests' }
ProcessTest >> testSuspendAndResumeAReadyProcess [
	| log p |
	log := OrderedCollection new.
	p := [log add: #ran] fork.
	p suspend.
	Processor yield.
	log add: #between.
	p resume.
	Processor yield.
	self assert: log asArray equals: #(#between #ran)
]

{ #category : 'tests' }
ProcessTest >> testSuspendRefusesASemaphoreWaiter [
	"As 1983 refused it: taken out of its wait and resumed, the process
	 would go on past the wait as though the signal had come."
	| sem p |
	sem := Semaphore new.
	p := [sem wait] fork.
	Processor yield.
	self should: [p suspend] raise: Error.
	p terminate
]

{ #category : 'tests' }
ProcessTest >> testSignalExceptionReachesAWaitingProcess [
	| sem p log |
	sem := Semaphore new.
	log := OrderedCollection new.
	p := [[sem wait. log add: #after] on: Error do: [:e | log add: e messageText]] fork.
	Processor yield.
	p signalException: (Error new messageText: 'cut short').
	Processor yield.
	self assert: log asArray equals: #('cut short')
]

{ #category : 'tests' }
ProcessTest >> testTerminateAProcessInsideACriticalSectionOfAnotherSemaphore [
	"The waiter is taken off its Semaphore under the Semaphore's lock;
	 a signal afterwards is an excess for the next waiter, not a signal
	 lost to a dead process."
	| sem p got |
	sem := Semaphore new.
	p := [sem wait] fork.
	Processor yield.
	p terminate.
	sem signal.
	got := false.
	[sem wait. got := true] fork.
	Processor yield.
	self assert: got
]

"
Monitor, single-threaded: the re-entry it promises, and the depth a
waiter must give up.  Bugs3 B60 found the promise unkept -- critical:
was a Mutex's, and a Mutex refuses its holder -- and these hold the
shape of the fix where any profile can run them.
"
Class {
	#name : 'MonitorTest',
	#superclass : 'TestCase',
	#category : 'Concurrency-Tests'
}

{ #category : 'tests' }
MonitorTest >> testCriticalIsReentrant [
	| m |
	m := Monitor new.
	self assert: (m critical: [m critical: [m critical: [#deep]]]) equals: #deep
]

{ #category : 'tests' }
MonitorTest >> testTheOutermostExitReleases [
	| m |
	m := Monitor new.
	m critical: [m critical: [self assert: m isHeldByActiveProcess]].
	self deny: m isHeldByActiveProcess.
	"And through an exception out of the inner block."
	[m critical: [m critical: [Error new signal: 'out']]] on: Error do: [:e | nil].
	self deny: m isHeldByActiveProcess.
	self assert: (m critical: [#free]) equals: #free
]

{ #category : 'tests' }
MonitorTest >> testWaitForChangeGivesUpTheWholeDepth [
	"A waiter two levels in must let the signaller into the monitor, or
	 nothing could ever change; and it must come back two levels in."
	| m log p |
	m := Monitor new.
	log := OrderedCollection new.
	p := [m critical: [m critical: [
		m waitUntil: [log includes: #go].
		log add: (m isHeldByActiveProcess ifTrue: [#wokenHolding] ifFalse: [#wokenFree])]]] fork.
	Processor yield.
	m critical: [log add: #go. m signalAll].
	Processor yield.
	self assert: log asArray equals: #(#go #wokenHolding).
	self deny: m isHeldByActiveProcess
]

{ #category : 'tests' }
MonitorTest >> testAnotherProcessIsKeptOut [
	| m log p |
	m := Monitor new.
	log := OrderedCollection new.
	m critical: [
		p := [m critical: [log add: #in]] fork.
		Processor yield.
		log add: #stillOut].
	Processor yield.
	self assert: log asArray equals: #(#stillOut #in)
]

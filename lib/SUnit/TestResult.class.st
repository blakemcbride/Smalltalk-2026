"
What happened when a suite ran: how many tests, how many passed, and which
of the rest failed as against which broke.
"
Class {
	#name : 'TestResult',
	#superclass : 'Object',
	#instVars : [ 'runCount', 'passedCount', 'failures', 'errors' ],
	#category : 'SUnit'
}

{ #category : 'initialization' }
TestResult >> initialize [
	runCount := 0.
	passedCount := 0.
	failures := OrderedCollection new.
	errors := OrderedCollection new
]

{ #category : 'accessing' }
TestResult >> runCount [ ^runCount ]

{ #category : 'accessing' }
TestResult >> passedCount [ ^passedCount ]

{ #category : 'accessing' }
TestResult >> failureCount [ ^failures size ]

{ #category : 'accessing' }
TestResult >> errorCount [ ^errors size ]

{ #category : 'accessing' }
TestResult >> failures [ ^failures ]

{ #category : 'accessing' }
TestResult >> errors [ ^errors ]

{ #category : 'testing' }
TestResult >> hasPassed [
	^failures isEmpty and: [errors isEmpty]
]

{ #category : 'running' }
TestResult >> runCase: aTestCase [
	"Run one test, and put it in exactly one of three buckets.

	 The two handlers are nested rather than side by side because
	 TestFailure IS an Error: the inner, more specific one has to be the
	 nearer of the two up the sender chain, or every failure would be
	 counted as an error and the distinction the class exists for would
	 quietly stop working.

	 The outcome is recorded in a temporary and the bucket chosen once,
	 after both handlers: a failure's handler used to add to failures and
	 return, the return unwound through runCase's ensure:, tearDown
	 raised, and the outer handler added the same test to errors -- two
	 buckets for one test (Bugs6 KERNB-10).  An error from tearDown after
	 a failure is the failure's; one after a pass is an error."
	| outcome text |
	outcome := #passed.
	[[aTestCase runCase]
		on: TestFailure
		do: [:e |
			outcome := #failed.
			text := e messageText ifNil: ['failed'].
			e return]]
		on: Error
		do: [:e |
			outcome == #passed ifTrue: [
				outcome := #error.
				text := e messageText ifNil: [e class name]].
			e return].
	runCount := runCount + 1.
	outcome == #passed ifTrue: [passedCount := passedCount + 1].
	outcome == #failed ifTrue: [failures add: aTestCase -> text].
	outcome == #error ifTrue: [errors add: aTestCase -> text]
]

{ #category : 'printing' }
TestResult >> summary [
	^String streamContents: [:s |
		s << runCount printString << ' run, '
		  << passedCount printString << ' passed, '
		  << self failureCount printString << ' failed, '
		  << self errorCount printString << ' errors']
]

{ #category : 'printing' }
TestResult >> report [
	"The summary, and then a line naming each test that did not pass.

	 On standard error rather than the Transcript, so that it works in a
	 headless image -- which is where a test suite is usually run."
	self reportOnStandardError: self summary.
	failures do: [:each |
		self reportOnStandardError:
			'  FAIL ', each key printString, ': ', each value].
	errors do: [:each |
		self reportOnStandardError:
			'  ERROR ', each key printString, ': ', each value]
]

{ #category : 'printing' }
TestResult >> printOn: aStream [
	aStream nextPutAll: self summary
]

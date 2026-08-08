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
	 quietly stop working."
	| passed |
	passed := false.
	[[aTestCase runCase. passed := true]
		on: TestFailure
		do: [:e | failures add: aTestCase -> (e messageText ifNil: ['failed'])]]
		on: Error
		do: [:e | errors add: aTestCase -> (e messageText ifNil: [e class name])].
	runCount := runCount + 1.
	passed ifTrue: [passedCount := passedCount + 1]
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

"
Does the runner count what actually happened?

This is the test that would catch SUnit lying.  It runs a class of tests
one of which passes, one of which fails an assertion, and one of which
raises something nobody expected, and then reads the TestResult.
"
Class {
	#name : 'SUnitReportingTest',
	#superclass : 'TestCase',
	#category : 'SUnit-Tests'
}

{ #category : 'tests' }
SUnitReportingTest >> testThreeBuckets [
	| result |
	result := SUnitBrokenTest suite run.
	self assert: result runCount equals: 3.
	self assert: result passedCount equals: 1.
	self assert: result failureCount equals: 1.
	self assert: result errorCount equals: 1.
	self deny: result hasPassed
]

{ #category : 'tests' }
SUnitReportingTest >> testFailureIsNotAnError [
	"A failed assertion is a failure, never an error.  TestFailure is a
	 subclass of Error, so getting this right depends entirely on the
	 order the two handlers are nested in TestResult>>runCase:."
	| result |
	result := (SUnitBrokenTest selector: #testThatFails) run.
	self assert: result failureCount equals: 1.
	self assert: result errorCount equals: 0
]

{ #category : 'tests' }
SUnitReportingTest >> testErrorIsNotAFailure [
	| result |
	result := (SUnitBrokenTest selector: #testThatErrors) run.
	self assert: result errorCount equals: 1.
	self assert: result failureCount equals: 0
]

{ #category : 'tests' }
SUnitReportingTest >> testTearDownRunsAfterAFailure [
	"The point of ensure: in runCase.  A fixture left standing by a failed
	 test is what makes the NEXT test fail, and then the report names the
	 wrong one."
	| test |
	test := SUnitBrokenTest selector: #testThatFails.
	test run.
	self assert: test tornDown
]

{ #category : 'tests' }
SUnitReportingTest >> testASuiteRunsEveryTest [
	| suite |
	suite := TestSuite new.
	suite addTestCase: SUnitTest.
	self assert: suite tests size equals: SUnitTest testSelectors size.
	self assert: suite run hasPassed
]

{ #category : 'tests' }
SUnitReportingTest >> testSummaryReadsCorrectly [
	self assert: SUnitBrokenTest suite run summary
		equals: '3 run, 1 passed, 1 failed, 1 errors'
]

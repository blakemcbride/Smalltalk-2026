"
SUnit testing itself.

The tests that pass are the easy half.  The half that matters is
SUnitReportingTest below, which runs tests that DELIBERATELY fail and blow
up, and then checks that the result counted them in the right buckets --
because a runner that quietly reports every failure as a pass is worse than
no runner at all, and nothing but a deliberate failure can catch that.
"
Class {
	#name : 'SUnitTest',
	#superclass : 'TestCase',
	#instVars : [ 'log' ],
	#category : 'SUnit-Tests'
}

{ #category : 'running' }
SUnitTest >> setUp [
	log := OrderedCollection new.
	log add: #setUp
]

{ #category : 'tests' }
SUnitTest >> testAssert [
	self assert: true.
	self assert: 1 + 1 = 2.
	self assert: [1 < 2]
]

{ #category : 'tests' }
SUnitTest >> testDeny [
	self deny: false.
	self deny: [1 > 2]
]

{ #category : 'tests' }
SUnitTest >> testAssertEquals [
	self assert: 3 + 4 equals: 7.
	self assert: 'ab' , 'c' equals: 'abc'.
	self deny: 1 equals: 2
]

{ #category : 'tests' }
SUnitTest >> testShouldRaise [
	self should: [1/0] raise: ZeroDivide.
	self should: [self signalFailure: 'x'] raise: TestFailure.
	self shouldnt: [1 + 1] raise: Error
]

{ #category : 'tests' }
SUnitTest >> testSetUpRan [
	"setUp runs before every test method, not once for the class."
	self assert: log size equals: 1.
	self assert: log first equals: #setUp
]

{ #category : 'tests' }
SUnitTest >> testSelectorsAreFound [
	self assert: (SUnitTest testSelectors includes: #testAssert).
	self deny: (SUnitTest testSelectors includes: #setUp)
]

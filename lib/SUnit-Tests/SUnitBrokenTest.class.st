"
Tests that are supposed to go wrong.

Not run as part of any suite anyone reads: SUnitReportingTest runs them on
purpose, into a TestResult of its own, and checks the counts.
"
Class {
	#name : 'SUnitBrokenTest',
	#superclass : 'TestCase',
	#instVars : [ 'tornDown' ],
	#category : 'SUnit-Tests'
}

{ #category : 'testing' }
SUnitBrokenTest class >> shouldRunInAllTests [
	"These are supposed to go wrong, so a whole-image run leaves them
	 out.  SUnitReportingTest runs them on purpose and reads the counts."
	^false
]

{ #category : 'running' }
SUnitBrokenTest >> tearDown [
	tornDown := true
]

{ #category : 'accessing' }
SUnitBrokenTest >> tornDown [
	^tornDown == true
]

{ #category : 'tests' }
SUnitBrokenTest >> testThatPasses [
	self assert: true
]

{ #category : 'tests' }
SUnitBrokenTest >> testThatFails [
	self assert: 1 equals: 2
]

{ #category : 'tests' }
SUnitBrokenTest >> testThatErrors [
	nil thisIsNotAMessageAnyoneImplements
]

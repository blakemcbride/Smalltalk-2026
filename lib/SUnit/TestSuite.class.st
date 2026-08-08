"
A collection of tests that run together and report as one.

It defines initialize and no class-side new, so the loader writes
'new ^super new initialize' for it -- which makes this the first place in
the system where that mechanism is load-bearing rather than demonstrated.
"
Class {
	#name : 'TestSuite',
	#superclass : 'Object',
	#instVars : [ 'tests', 'name' ],
	#category : 'SUnit'
}

{ #category : 'initialization' }
TestSuite >> initialize [
	tests := OrderedCollection new
]

{ #category : 'accessing' }
TestSuite >> name [ ^name ]

{ #category : 'accessing' }
TestSuite >> name: aString [ name := aString ]

{ #category : 'accessing' }
TestSuite >> tests [ ^tests ]

{ #category : 'building' }
TestSuite >> addTest: aTest [
	tests add: aTest.
	^aTest
]

{ #category : 'building' }
TestSuite >> addTests: aCollection [
	aCollection do: [:each | self addTest: each]
]

{ #category : 'building' }
TestSuite >> addTestCase: aTestCaseClass [
	"Every test method of a TestCase class, as its own test."
	self addTests: aTestCaseClass suite tests
]

{ #category : 'running' }
TestSuite >> run [
	| result |
	result := TestResult new.
	self run: result.
	^result
]

{ #category : 'running' }
TestSuite >> run: aResult [
	tests do: [:each | each run: aResult].
	^aResult
]

{ #category : 'printing' }
TestSuite >> printOn: aStream [
	aStream nextPutAll: (name ifNil: ['a TestSuite'])
]

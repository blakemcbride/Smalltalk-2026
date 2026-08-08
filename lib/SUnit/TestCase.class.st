"
One test.

A TestCase instance is one test METHOD, not one test class: the class is a
family of tests that share a setUp, and running it makes one instance per
test so that no test can see what another left behind.  That isolation is
the design's whole claim, and it is why setUp runs again for every one of
them rather than once for the class.

Subclass it, write methods whose names begin with test, and send suite to
the class:

	MyTests suite run report

assert: here is NOT Object>>assert:.  Object's signals an AssertionFailure,
which a test runner would count as an error -- something unforeseen broke.
This one signals a TestFailure, which is the runner's other bucket: the test
worked and the answer was no.
"
Class {
	#name : 'TestCase',
	#superclass : 'Object',
	#instVars : [ 'testSelector' ],
	#category : 'SUnit'
}

{ #category : 'instance creation' }
TestCase class >> selector: aSymbol [
	^self new setTestSelector: aSymbol
]

{ #category : 'accessing' }
TestCase class >> testSelectors [
	"Every selector beginning with test, in a fixed order.

	 Sorted, so that a suite runs the same way twice.  A method dictionary
	 is a hash table and iterating one gives whatever order the hashes
	 happened to produce, which is not something a test report should
	 depend on."
	^(self selectors select: [:each | each beginsWith: 'test']) asSortedCollection asArray
]

{ #category : 'instance creation' }
TestCase class >> suite [
	| suite |
	suite := TestSuite new.
	suite name: self name.
	self testSelectors do: [:each | suite addTest: (self selector: each)].
	^suite
]

{ #category : 'running' }
TestCase class >> run [
	^self suite run
]

{ #category : 'private' }
TestCase >> setTestSelector: aSymbol [
	testSelector := aSymbol
]

{ #category : 'accessing' }
TestCase >> testSelector [
	^testSelector
]

{ #category : 'running' }
TestCase >> setUp [
	"Whatever this test needs in place.  Runs before every test method."
]

{ #category : 'running' }
TestCase >> tearDown [
	"Undo setUp.  Runs after every test method, however it finished --
	 which is what ensure: is for below."
]

{ #category : 'running' }
TestCase >> performTest [
	^self perform: testSelector
]

{ #category : 'running' }
TestCase >> runCase [
	"tearDown runs whether the test passed, failed or blew up.  A test
	 that leaves a fixture standing because it failed is a test that makes
	 the NEXT one fail, and then the report names the wrong one."
	[self setUp. self performTest] ensure: [self tearDown]
]

{ #category : 'running' }
TestCase >> run [
	| result |
	result := TestResult new.
	self run: result.
	^result
]

{ #category : 'running' }
TestCase >> run: aResult [
	aResult runCase: self
]

{ #category : 'asserting' }
TestCase >> signalFailure: aString [
	^TestFailure new signal: aString
]

{ #category : 'asserting' }
TestCase >> assert: aBooleanOrBlock [
	(aBooleanOrBlock cull: nil) == true
		ifFalse: [^self signalFailure: 'assertion failed']
]

{ #category : 'asserting' }
TestCase >> assert: aBooleanOrBlock description: aString [
	(aBooleanOrBlock cull: nil) == true
		ifFalse: [^self signalFailure: aString]
]

{ #category : 'asserting' }
TestCase >> deny: aBooleanOrBlock [
	(aBooleanOrBlock cull: nil) == true
		ifTrue: [^self signalFailure: 'denial failed']
]

{ #category : 'asserting' }
TestCase >> deny: aBooleanOrBlock description: aString [
	(aBooleanOrBlock cull: nil) == true
		ifTrue: [^self signalFailure: aString]
]

{ #category : 'asserting' }
TestCase >> assert: actual equals: expected [
	"Reports both values, which is the only reason to prefer it over
	 assert: actual = expected -- that one can only say 'false'."
	actual = expected ifFalse: [
		^self signalFailure:
			'expected ', expected printString,
			' but got ', actual printString]
]

{ #category : 'asserting' }
TestCase >> deny: actual equals: expected [
	actual = expected ifTrue: [
		^self signalFailure:
			'expected anything but ', expected printString]
]

{ #category : 'asserting' }
TestCase >> should: aBlock raise: anExceptionClass [
	"Fails if the block finishes without signalling what was expected.

	 The handler returns rather than falling out of the block, so that an
	 exception raised deep inside is caught here and nowhere else."
	[aBlock value]
		on: anExceptionClass
		do: [:e | ^self].
	^self signalFailure:
		'expected ', anExceptionClass name, ' and nothing was signalled'
]

{ #category : 'asserting' }
TestCase >> shouldnt: aBlock raise: anExceptionClass [
	[aBlock value]
		on: anExceptionClass
		do: [:e | ^self signalFailure:
			'did not expect ', anExceptionClass name].
	^self
]

{ #category : 'printing' }
TestCase >> printOn: aStream [
	aStream nextPutAll: self class name;
		nextPutAll: '>>';
		nextPutAll: (testSelector ifNil: ['(no test)']) asString
]

{ #category : 'testing' }
TestCase class >> shouldRunInAllTests [
	"Whether a run of every test in the image should include this class.

	 Almost always yes.  It is no for a class whose tests are MEANT to go
	 wrong -- SUnit needs a few of those to check that it reports failures
	 as failures, and a whole-image run that reported them would be crying
	 wolf at every build."
	^true
]

{ #category : 'instance creation' }
TestCase class >> allTests [
	"Every test in the image, in a fixed order.

	 This is what makes the port's progress a number rather than a
	 judgement: load a package, run this, read the bar.  Sorted by class
	 name so that two runs of the same image report the same way."
	| suite |
	suite := TestSuite new.
	suite name: 'all tests'.
	((self allSubclasses select: [:each | each shouldRunInAllTests])
		asSortedCollection: [:a :b | a name <= b name])
			do: [:each | suite addTestCase: each].
	^suite
]

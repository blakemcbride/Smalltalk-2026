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
	 assert: actual = expected -- that one can only say 'false'.

	 The EXPECTED value is the receiver of the comparison, and that is not
	 arbitrary tidiness: = is not symmetric across Symbol and String.  Our
	 String>>= answers true for a Symbol spelling the same characters, and
	 Symbol>>= is identity and answers false -- so `#Tuesday = 'Tuesday''
	 and `'Tuesday' = #Tuesday' disagree.  Pharo's Chronology answers
	 Symbols from its name tables (DayNames is a literal array of them) and
	 its tests compare against String literals, which works there and did
	 not here, in nine tests, purely on which side the comparison started
	 from.  Putting the expected value first is what Pharo's own SUnit does
	 and is the thing that makes a ported suite mean what it says."
	expected = actual ifFalse: [
		^self signalFailure:
			'expected ', expected printString,
			' but got ', actual printString]
]

{ #category : 'asserting' }
TestCase >> deny: actual equals: expected [
	"Expected first, for the reason given in assert:equals:."
	expected = actual ifTrue: [
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
	 wolf at every build.

	 And no for an ABSTRACT test class.  Pharo builds a suite once and
	 shares it down a hierarchy -- CollectionRootTest defines the tests
	 every collection must pass and leaves `collection' as a
	 subclassResponsibility for each concrete subclass to answer -- so
	 running the parent itself asks a question nobody was meant to answer.
	 It produced 52 of `My subclass should have overridden one of my
	 messages' in one run, every one of them a test doing exactly what it
	 was written to do."
	^self isAbstract not
]

{ #category : 'testing' }
TestCase class >> isAbstract [
	"Whether I only define tests for my subclasses to run.

	 False here, and overridden by the classes that mean it -- which is
	 Pharo's arrangement, and the reason the override reads
	 `^self name = #CollectionRootTest': a class is abstract, its
	 subclasses are not, and inheriting the answer would silence them too."
	^false
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

{ #category : 'asserting' }
TestCase >> assertEmpty: aCollection [
	self assert: aCollection isEmpty
		description: 'expected an empty collection'
]

{ #category : 'asserting' }
TestCase >> denyEmpty: aCollection [
	self deny: aCollection isEmpty
		description: 'expected a collection with something in it'
]

{ #category : 'asserting' }
TestCase >> deny: actual identicalTo: expected [
	self deny: actual == expected
		description: 'expected two different objects'
]

{ #category : 'asserting' }
TestCase >> assert: actual identicalTo: expected [
	self assert: actual == expected
		description: 'expected the very same object'
]

{ #category : 'accessing' }
TestCase >> defaultTestError [
	"The exception class a test means when it says only 'this should fail'.

	 Pharo answers its own TestFailure hierarchy's root for this; here it is
	 Error, because a test written as `should: [...] raise: self
	 defaultTestError' is asking whether the code refuses bad input at all,
	 not which particular refusal it chose."
	^Error
]

{ #category : 'asserting' }
TestCase >> assert: actual closeTo: expected [
	"For values that arithmetic cannot be expected to reproduce exactly.

	 Not a weaker assert:equals: to reach for when one fails -- reaching for
	 it there hides the bug it is complaining about.  It is for quantities
	 that genuinely have no exact representation, which in Chronology means
	 a duration expressed in days: 1.08546296296 is a repeating fraction and
	 no float will ever be equal to it."
	self assert: (actual closeTo: expected)
		description: 'expected something close to ', expected printString,
			' but got ', actual printString
]

{ #category : 'asserting' }
TestCase >> assert: actual closeTo: expected precision: aPrecision [
	self assert: (actual closeTo: expected precision: aPrecision)
		description: 'expected something within ', aPrecision printString,
			' of ', expected printString, ' but got ', actual printString
]

{ #category : 'asserting' }
TestCase >> deny: actual closeTo: expected [
	self deny: (actual closeTo: expected)
		description: 'expected something not close to ', expected printString
]

"
A TestCase that knows which class it is testing.

Ten of System-Time-Tests' twenty-four files subclass this.  Pharo's version
adds coverage checking -- it can ask which of the tested class's selectors
no test exercises -- and the tests themselves do not use any of that; they
subclass it because that is the house style for a test of one class.

So this is the part they actually need: somewhere to stand.  The coverage
protocol is absent until something asks for it rather than transcribed on
the chance that it might.
"
Class {
	#name : 'ClassTestCase',
	#superclass : 'TestCase',
	#category : 'SUnit'
}

{ #category : 'accessing' }
ClassTestCase >> classToBeTested [
	"Pharo derives this from the test's own name -- FooTest tests Foo."
	| name |
	name := self class name.
	(name endsWith: 'Test') ifFalse: [^nil].
	^Smalltalk at: (name copyFrom: 1 to: name size - 4) asSymbol
			  ifAbsent: [nil]
]

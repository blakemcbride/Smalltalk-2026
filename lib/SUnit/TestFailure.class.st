"
What a failed assertion signals.

The distinction SUnit rests on is between a FAILURE and an ERROR.  A failure
is the test saying 'the thing I checked was not so' -- the test worked, and
the answer was no.  An error is anything else: the code under test, or the
test itself, broke in a way nobody predicted.  Both stop the test; they mean
very different things to whoever reads the bar, and a runner that reported
them as one number would be throwing away the more useful half.

That is the whole reason TestFailure is its own class and TestResult catches
it separately, more specifically, and first.
"
Class {
	#name : 'TestFailure',
	#superclass : 'Error',
	#category : 'SUnit'
}

{ #category : 'accessing' }
TestFailure >> description [
	^'Test failed'
]

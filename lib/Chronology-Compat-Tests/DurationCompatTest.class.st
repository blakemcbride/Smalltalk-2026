"
What lib/Chronology-Compat changes in Pharo's own classes, checked where
Pharo's suite cannot reach: a fault Pharo's version does not have is one
Pharo's tests never had a reason to cover.  Loaded by the pharo-time
profile beside pharo/System-Time-Tests, and scored with it in
tests/profiles.expected.
"
Class {
	#name : 'DurationCompatTest',
	#superclass : 'TestCase',
	#category : 'Chronology-Compat-Tests'
}

{ #category : 'tests' }
DurationCompatTest >> testNanosecondsCarryIntoSeconds [
	"A nanosecond count of a second or more is carried into the seconds
	 (Bugs6 KERNA-4); it used to print as 0:00:00:01.15."
	| d |
	d := Duration seconds: 1 nanoSeconds: 1500000000.
	self assert: d seconds equals: 2.
	self assert: d nanoSeconds equals: 500000000.
	self assert: d printString equals: '0:00:00:02.5'.
	self assert: d asMilliSeconds equals: 2500.
	d := Duration days: 0 hours: 0 minutes: 0 seconds: 0 nanoSeconds: 2500000000.
	self assert: d seconds equals: 2.
	self assert: d printString equals: '0:00:00:02.5'.
	d := Duration seconds: 0 nanoSeconds: 3000000000.
	self assert: d seconds equals: 3.
	self assert: d nanoSeconds equals: 0.
	self assert: d printString equals: '0:00:00:03'.
	self assert: d equals: (Duration seconds: 3)
]

{ #category : 'tests' }
DurationCompatTest >> testEqualDurationsHashAlike [
	"= compares the total and hash the fields, so the fields have to be
	 one way of writing the total."
	| a b |
	a := Duration seconds: 1 nanoSeconds: 1500000000.
	b := Duration seconds: 2 nanoSeconds: 500000000.
	self assert: a equals: b.
	self assert: a hash equals: b hash.
	self assert: ((Set with: a) includes: b)
]

{ #category : 'tests' }
DurationCompatTest >> testNegativeNanosecondsCarry [
	"The carry keeps the seconds and the nanoseconds of one sign, and
	 Pharo's sign repair still runs on what is left."
	| d |
	d := Duration seconds: -1 nanoSeconds: -1500000000.
	self assert: d printString equals: '-0:00:00:02.5'.
	self assert: d asNanoSeconds equals: -2500000000.
	d := Duration seconds: 1 nanoSeconds: -1500000000.
	self assert: d printString equals: '-0:00:00:00.5'.
	self assert: d asNanoSeconds equals: -500000000.
	d := Duration seconds: -1 nanoSeconds: 1500000000.
	self assert: d printString equals: '0:00:00:00.5'.
	self assert: d asNanoSeconds equals: 500000000
]

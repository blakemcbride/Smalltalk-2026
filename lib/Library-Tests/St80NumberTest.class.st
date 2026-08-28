"
The 1983 numeric tower, asserted from first principles rather than from
what it currently answers.

The distinction matters.  A test written by reading the implementation
confirms the implementation; these were written by asking what Smalltalk-80
is supposed to do -- exact integers of any size, exact rationals, the four
division operators keeping their separate promises, and coercion across all
of it -- and then run to see.
"
Class {
	#name : 'St80NumberTest',
	#superclass : 'TestCase',
	#category : 'Library-Tests'
}

{ #category : 'integers' }
St80NumberTest >> testIntegersAreExactAtAnySize [
	self assert: 100 factorial numberOfDigits equals: 158.
	self assert: (2 raisedTo: 100) equals: 1267650600228229401496703205376.
	self assert: (2 raisedTo: 100) class equals: LargePositiveInteger.
	self assert: 1000000 * 1000000 equals: 1000000000000.
	self assert: (2 raisedTo: 64) - (2 raisedTo: 64) equals: 0.
	self assert: ((2 raisedTo: 100) // (2 raisedTo: 90)) equals: 1024
]

{ #category : 'integers' }
St80NumberTest >> testLargeIntegersRoundTrip [
	| n |
	n := 12345678901234567890.
	self assert: n printString equals: '12345678901234567890'.
	self assert: n negated negated equals: n.
	self assert: n negated class equals: LargeNegativeInteger.
	self assert: (n + 1) - 1 equals: n.
	self assert: (n * n) // n equals: n
]

{ #category : 'division' }
St80NumberTest >> testTheFourDivisions [
	self assert: 7 / 2 equals: 7/2.
	self assert: 7 // 2 equals: 3.
	self assert: 7 \\ 2 equals: 1.
	self assert: (7 rem: 2) equals: 1.
	self assert: (7 quo: 2) equals: 3
]

{ #category : 'division' }
St80NumberTest >> testDivisionOfNegatives [
	"// and \\ follow the divisor's sign; quo: and rem: truncate toward zero."
	self assert: -7 // 2 equals: -4.
	self assert: -7 \\ 2 equals: 1.
	self assert: (-7 quo: 2) equals: -3.
	self assert: (-7 rem: 2) equals: -1.
	self assert: 7 // -2 equals: -4.
	self assert: 7 \\ -2 equals: -1
]

{ #category : 'division' }
St80NumberTest >> testDivisionIdentity [
	"The identity every pair has to keep, over a spread of signs."
	#(17 -17 100 -100 3 -3) do: [:a |
		#(5 -5 7 -7) do: [:b |
			self assert: (a // b) * b + (a \\ b) equals: a.
			self assert: (a quo: b) * b + (a rem: b) equals: a]]
]

{ #category : 'division' }
St80NumberTest >> testDivisionByZeroRaises [
	self should: [1 / 0] raise: ZeroDivide.
	self should: [1 // 0] raise: ZeroDivide.
	self should: [1 \\ 0] raise: ZeroDivide
]

{ #category : 'fractions' }
St80NumberTest >> testFractionsAreExact [
	self assert: (1/3) + (1/6) equals: 1/2.
	self assert: (1/10) + (2/10) equals: 3/10.
	self assert: (1/3) * 3 equals: 1.
	self assert: (2/4) equals: (1/2).
	self assert: (2/4) numerator equals: 1.
	self assert: (1/2) reciprocal equals: 2.
	self assert: (3/4) < (4/5)
]

{ #category : 'fractions' }
St80NumberTest >> testFractionReduces [
	self assert: (6/8) numerator equals: 3.
	self assert: (6/8) denominator equals: 4.
	self assert: (4/2) equals: 2.
	self assert: (4/2) class equals: SmallInteger
]

{ #category : 'floats' }
St80NumberTest >> testFloatBasics [
	self assert: 9 sqrt equals: 3.0.
	self assert: (10 log: 10) equals: 1.0.
	self assert: 3.7 rounded equals: 4.
	self assert: 3.7 truncated equals: 3.
	self assert: 3.7 floor equals: 3.
	self assert: 3.7 ceiling equals: 4.
	self assert: -3.7 floor equals: -4.
	self assert: -3.7 truncated equals: -3.
	self assert: -3.7 rounded equals: -4
]

{ #category : 'coercion' }
St80NumberTest >> testMixedArithmeticCoerces [
	self assert: 1 + 2.5 equals: 3.5.
	self assert: (1/2) + 0.5 equals: 1.0.
	self assert: 1 + (1/2) equals: 3/2.
	self assert: (1/2) < 0.6.
	self assert: 2 = 2.0.
	self assert: ((2 raisedTo: 70) + 1) > (2 raisedTo: 70)
]

{ #category : 'comparing' }
St80NumberTest >> testComparisonAcrossClasses [
	self assert: ((2 raisedTo: 70) > 100).
	self assert: (100 < (2 raisedTo: 70)).
	self assert: ((0 - (2 raisedTo: 70)) < 0).
	self assert: ((1/2) max: 0.25) equals: (1/2)
]

{ #category : 'protocol' }
St80NumberTest >> testNumberProtocol [
	self assert: -5 abs equals: 5.
	self assert: 5 negated equals: -5.
	self assert: 3 sign equals: 1.
	self assert: 0 sign equals: 0.
	self assert: -3 sign equals: -1.
	self assert: (12 gcd: 18) equals: 6.
	self assert: (4 lcm: 6) equals: 12.
	self assert: 5 even equals: false.
	self assert: 5 odd equals: true.
	self assert: (5 between: 1 and: 10)
]

{ #category : 'intervals' }
St80NumberTest >> testIntervals [
	self assert: (1 to: 5) asArray equals: #(1 2 3 4 5).
	self assert: (1 to: 10 by: 3) asArray equals: #(1 4 7 10).
	self assert: (10 to: 1 by: -2) asArray equals: #(10 8 6 4 2).
	self assert: (1 to: 0) asArray equals: #().
	self assert: (1 to: 5) size equals: 5.
	self assert: ((1 to: 10) inject: 0 into: [:a :b | a + b]) equals: 55.
	self assert: ((1 to: 4) collect: [:i | i * i]) asArray equals: #(1 4 9 16)
]

{ #category : 'reading' }
St80NumberTest >> testReadingRadixDigitsInEitherCase [
	"digitValue knew capitals only, so `'16rff' asNumber' was 0; an r with
	 no digit after it is a letter, so `'16r'' is 16 as asInteger already
	 said; and a reader takes a String (Bugs3 B51)."
	self assert: '16rff' asNumber equals: 255.
	self assert: '16rFF' asNumber equals: 255.
	self assert: '16r-ff' asNumber equals: -255.
	self assert: '16r' asNumber equals: 16.
	self assert: '16r' asInteger equals: 16.
	self assert: (Number readFrom: '42') equals: 42.
	self assert: (Integer readFrom: '42') equals: 42.
	self assert: 'abc' asNumber isNil.
	self assert: $f digitValue equals: 15.
	self assert: $? digitValue equals: -1
]

{ #category : 'random' }
St80NumberTest >> testRandom [
	"Two generators are two streams; the same seed is the same stream; and
	 the stream is java.util.Random's, whose first answers for seed 42 are
	 published (Bugs3 B50)."
	| r |
	self assert: ((1 to: 1000) collect: [:i | Random new next]) asSet size equals: 1000.
	self assert: (Random seed: 42) next equals: 0.7275636800328681.
	self assert: (Random seed: 5) next equals: (Random seed: 5) next.
	r := Random seed: 7.
	self assert: ((1 to: 1000) collect: [:i | r between: 3 and: 5]) asSet size equals: 3.
	self assert: ((1 to: 1000) select: [:i | | x | x := r next. x < 0 or: [x >= 1]]) isEmpty
]

{ #category : 'time' }
St80NumberTest >> testTimeReadsNoonAndMidnight [
	"12 pm is noon and 12 am is midnight; 1983 had them the other way
	 round, so storeString of noon evaluated to midnight (Bugs3 B48)."
	self assert: (Time readFromString: '12:00:00 pm') asSeconds equals: 43200.
	self assert: (Time readFromString: '12:00:00 am') asSeconds equals: 0.
	self assert: (Time readFromString: '1:59:30 pm') asSeconds equals: 50370.
	self assert: ((0 to: 86399 by: 61) select: [:s |
		(Time readFromString: (Time fromSeconds: s) printString) asSeconds ~= s]) isEmpty
]

{ #category : 'dates' }
St80NumberTest >> testDateReadsEveryFormAndRefusesTheRest [
	"1983's three forms and the year-first one; month 13, February 31 and
	 `garbage' are one error rather than four different ones (Bugs3 B51)."
	| d |
	d := Date readFromString: '2024-01-02'.
	self assert: d year equals: 2024.
	self assert: d monthIndex equals: 1.
	self assert: d dayOfMonth equals: 2.
	#('5 April 1982' 'April 5, 1982' '4/5/1982' '5APR1982' '5-apr-1982') do: [:text |
		d := Date readFromString: text.
		self assert: d year equals: 1982.
		self assert: d monthIndex equals: 4.
		self assert: d dayOfMonth equals: 5].
	self assert: (Date readFromString: 'February 29, 2024') dayOfMonth equals: 29.
	#('13/13/2024' '31/2/2024' 'garbage' 'February 29, 2023' '2024-02-30' '') do: [:text |
		self should: [Date readFromString: text] raise: Error]
]

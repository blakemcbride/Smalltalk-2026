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

{ #category : 'bugs4' }
St80NumberTest >> testFloorLogRefusesWhatHasNoLogarithm [
	"`Float infinity floorLog: 10' recursed on a radix it squared each
	 level -- 10, 100, 10^4 -- and never came back, because an infinity is
	 never less than the radix; a NaN did the same, and a radix of 1 or 0
	 recursed on itself until the stack ran out.  A negative radix squared
	 to a positive one and answered the logarithm in a different base:
	 `1000 floorLog: -2' was 9, which is log base 4 (Bugs4 NUMBERS-1)."
	self should: [Float infinity floorLog: 10] raise: Error.
	self should: [Float infinity negated floorLog: 10] raise: Error.
	self should: [Float nan floorLog: 10] raise: Error.
	self should: [1000 floorLog: 1] raise: Error.
	self should: [1000 floorLog: 0] raise: Error.
	self should: [1000 floorLog: -2] raise: Error.
	self should: [1000 floorLog: Float nan] raise: Error.
	self should: [1000.0 floorLog: 1] raise: Error
]

{ #category : 'bugs4' }
St80NumberTest >> testFloorLogIsExactAtEverySize [
	"An Integer past the double range reached Float>>floorLog: through
	 asFloat, which is an infinity, so it hung (Bugs4 NUMBERS-1).  It
	 answers its own question now, through highBit for base two and
	 through an estimate CORRECTED against exact powers otherwise, so the
	 boundaries are right rather than nearly right."
	self assert: (1000 floorLog: 10) equals: 3.
	self assert: (999 floorLog: 10) equals: 2.
	self assert: (1 floorLog: 10) equals: 0.
	self assert: ((2 raisedTo: 1100) floorLog: 10) equals: 331.
	self assert: ((2 raisedTo: 1100) floorLog: 2) equals: 1100.
	self assert: ((10 raisedTo: 60) floorLog: 10) equals: 60.
	self assert: ((10 raisedTo: 60) - 1 floorLog: 10) equals: 59.
	self assert: ((10 raisedTo: 60) + 1 floorLog: 10) equals: 60.
	self assert: ((10 raisedTo: 60) floorLog: 10.0) equals: 60.
	self assert: (255 floorLog: 2) equals: 7.
	self assert: (256 floorLog: 2) equals: 8.
	self assert: (1000.0 floorLog: 10) equals: 3
]

{ #category : 'bugs4' }
St80NumberTest >> testLcmIsNeverNegative [
	"The Blue Book formula carries the sign of whichever operand it
	 divided, so `-4 lcm: 6' answered -12 where ANSI, Pharo and Python
	 answer 12 (Bugs4 NUMBERS-4).  A LEAST common multiple can only mean
	 least in magnitude."
	self assert: (-4 lcm: 6) equals: 12.
	self assert: (4 lcm: -6) equals: 12.
	self assert: (-4 lcm: -6) equals: 12.
	self assert: (4 lcm: 6) equals: 12.
	self assert: (0 lcm: 5) equals: 0.
	self assert: (-7 lcm: 0) equals: 0.
	self assert: ((2 raisedTo: 70) negated lcm: 6) equals: (2 raisedTo: 70) * 3
]

{ #category : 'bugs4' }
St80NumberTest >> testFractionConstructorReduces [
	"`Fraction numerator: 2 denominator: 4' stored 2 and 4, and Fraction>>=
	 compares the two fields -- so it was not equal to (1/2), while
	 Fraction>>hash gave both the same answer.  Two objects that hash alike
	 and are not equal are two objects a Set holds both of (Bugs4
	 NUMBERS-5)."
	| a b s |
	a := Fraction numerator: 2 denominator: 4.
	b := 1/2.
	self assert: a equals: b.
	self assert: a hash equals: b hash.
	s := Set new.
	s add: a; add: b.
	self assert: s size equals: 1.
	self assert: (Fraction numerator: 4 denominator: 2) printString equals: '(2/1)'.
	self assert: (Fraction numerator: 1 denominator: -2) printString equals: '(-1/2)'.
	self assert: (Fraction numerator: -1 denominator: -2) equals: (1/2).
	self assert: (Fraction numerator: 0 denominator: 5) numerator equals: 0.
	self assert: (Fraction numerator: 0 denominator: 5) denominator equals: 1.
	self assert: (Fraction numerator: 2 denominator: 4) reduced equals: (1/2).
	self assert: (Fraction numerator: 4 denominator: 2) reduced equals: 2.
	self assert: (3/4) + (1/4) equals: 1.
	self assert: (3/4) * (2/3) equals: (1/2)
]

{ #category : 'bugs4' }
St80NumberTest >> testFractionValidatesTheDenominatorAfterCoercing [
	"The zero test ran on the argument as written and the truncation
	 after it, so `Fraction numerator: 3 denominator: (1/2)' passed the
	 test -- (1/2) is not zero -- and then truncated to zero, leaving a
	 (3/0) that raised somewhere else entirely (Bugs4 NUMBERS-6)."
	self should: [Fraction numerator: 3 denominator: (1/2)] raise: ZeroDivide.
	self should: [Fraction numerator: 3 denominator: 0.5] raise: ZeroDivide.
	self should: [Fraction numerator: 3 denominator: 0] raise: ZeroDivide.
	self should: [Fraction numerator: 3 denominator: -0.5] raise: ZeroDivide.
	self assert: (Fraction numerator: 3 denominator: 2.5) equals: (3/2)
]

{ #category : 'bugs4' }
St80NumberTest >> testIsZero [
	"Documented in the manual's Number tables with no implementor, so
	 `3 isZero' was a doesNotUnderstand (Bugs4 DOCS-4)."
	self deny: 3 isZero.
	self assert: 0 isZero.
	self assert: 0.0 isZero.
	self assert: 0.0 negated isZero.
	self deny: 0.1 isZero.
	self deny: Float nan isZero.
	self deny: Float infinity isZero.
	self deny: (1/2) isZero.
	self assert: (Fraction numerator: 0 denominator: 5) isZero.
	self assert: ((2 raisedTo: 70) - (2 raisedTo: 70)) isZero.
	self deny: (2 raisedTo: 70) isZero
]

{ #category : 'bugs4' }
St80NumberTest >> testLargeIntegersPrintAndReadBack [
	"printOn:base: divided the whole magnitude by the base once per digit
	 and readFrom:radix: multiplied the whole magnitude by the base once
	 per digit, both a byte at a time in Smalltalk: printing sixteen
	 thousand digits took 15 seconds and reading eight thousand took 59
	 (Bugs4 NUMBERS-3).  Both work in an Array of SmallInteger limbs now,
	 a chunk of digits per pass.  What is asserted here is that they
	 answer the same numbers -- the boundaries where a chunk or a limb
	 ends, every base, both signs, and the round trip."
	| digits n |
	self assert: (2 raisedTo: 64) printString equals: '18446744073709551616'.
	self assert: (2 raisedTo: 64) negated printString equals: '-18446744073709551616'.
	self assert: (10 raisedTo: 30) printString equals: '1000000000000000000000000000000'.
	self assert: ((10 raisedTo: 30) + 1) printString equals: '1000000000000000000000000000001'.
	self assert: (SmallInteger maxVal + 1) printString equals: '4611686018427387904'.
	self assert: ((2 raisedTo: 100) printString: 16) equals: '10000000000000000000000000'.
	self assert: ((2 raisedTo: 100) printString: 36) equals: '3EWFDNCA0N6LD1GGVFGG'.
	self assert: ((2 raisedTo: 100) printString: 2) size equals: 101.
	"tests/unit/test_image.c's test_bugs4_collections carries the sizes
	 that show the time -- the suites here run under one bytecode budget
	 for the whole of them and are no place for a benchmark."
	self assert: '123456789012345678901234567890' asNumber
		equals: 123456789012345678901234567890.
	self assert: '-123456789012345678901234567890' asNumber
		equals: 123456789012345678901234567890 negated.
	self assert: (Integer readFrom: 'ff' radix: 16) equals: 255.
	self assert: (Integer readFrom: '12abc') equals: 12.
	self assert: (Integer readFrom: '') equals: 0.
	"Every length from one digit to past two chunks and two limbs."
	1 to: 12 do: [:k |
		digits := String new: k withAll: $7.
		n := digits asNumber.
		self assert: n printString equals: digits.
		self assert: n negated printString equals: '-' , digits].
	n := 40 factorial.
	self assert: n printString size equals: 48.
	self assert: n printString asNumber equals: n.
	self assert: (Integer readFrom: (n printString: 16) radix: 16) equals: n
]

{ #category : 'bugs4' }
St80NumberTest >> testLargeIntegerMultiplyIsSchoolbookAtEverySize [
	"digitMultiply:neg: multiplies three bytes at a time in an Array of
	 SmallIntegers rather than a nibble at a time through digitAt:put:,
	 nine times fewer inner iterations (Bugs4 NUMBERS-3).  It computes
	 exactly what the loop it replaced computed; these are the places a
	 limb or a carry ends, plus an identity that would catch a lost carry
	 anywhere: (10^k - 1) squared is a number whose digits are known."
	| a b |
	self assert: (2 raisedTo: 64) * (2 raisedTo: 64) equals: (2 raisedTo: 128).
	self assert: 0 * (2 raisedTo: 200) equals: 0.
	self assert: (2 raisedTo: 200) * 0 equals: 0.
	self assert: (2 raisedTo: 200) * 1 equals: (2 raisedTo: 200).
	self assert: (2 raisedTo: 200) negated * (2 raisedTo: 200) equals: (2 raisedTo: 400) negated.
	self assert: (2 raisedTo: 200) negated * (2 raisedTo: 200) negated equals: (2 raisedTo: 400).
	1 to: 12 do: [:k |
		a := (10 raisedTo: k) - 1.
		self assert: a * a equals: (10 raisedTo: 2 * k) - (2 * (10 raisedTo: k)) + 1].
	#(23 24 25 47 48 49 71 72 73) do: [:bits |
		a := (1 bitShift: bits) - 1.
		b := (1 bitShift: bits) + 1.
		self assert: a * b equals: (1 bitShift: 2 * bits) - 1.
		self assert: b * b equals: (1 bitShift: 2 * bits) + (1 bitShift: bits + 1) + 1].
	self assert: (40 factorial // 39 factorial) equals: 40.
	self assert: ((2 raisedTo: 200) * (3 raisedTo: 150)) // (3 raisedTo: 150)
		equals: (2 raisedTo: 200)
]

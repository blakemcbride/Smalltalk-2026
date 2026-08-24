"
The boundary between what a driver says and what Smalltalk means.

The exactness tests are the ones worth having.  Everything else here would
be caught by the first person to run a query; a DECIMAL column that has
quietly become a Float will produce answers that are right to six figures
for years, and then be wrong about money.
"
Class {
	#name : 'DbValueTest',
	#superclass : 'TestCase',
	#category : 'Database-Tests'
}

{ #category : 'exactness' }
DbValueTest >> testDecimalIsExact [
	"One tenth is not representable in binary, so a Float round trip loses
	 it.  This is the whole reason DECIMAL columns come up as characters."

	self assert: (Odbc numberFromDigits: '0.10') * 10 equals: 1.
	self assert: (Odbc numberFromDigits: '0.10') * 10 = 1.
	self assert: ((Odbc numberFromDigits: '0.10') isKindOf: Fraction)
]

{ #category : 'exactness' }
DbValueTest >> testDecimalBeatsTheImagesOwnReader [
	"Recorded as a comparison rather than asserted alone, so that the day
	 String>>asNumber becomes exact this test says so instead of quietly
	 duplicating it."

	self assert: ('0.10' asNumber * 10) = 1.0.
	self deny: ('0.10' asNumber isKindOf: Fraction)
]

{ #category : 'exactness' }
DbValueTest >> testDecimalValues [
	self assert: (Odbc numberFromDigits: '123.45') equals: 2469/20.
	self assert: (Odbc numberFromDigits: '-123.45') equals: -2469/20.
	self assert: (Odbc numberFromDigits: '42') equals: 42.
	self assert: (Odbc numberFromDigits: '-42') equals: -42.
	self assert: (Odbc numberFromDigits: '0') equals: 0.
	self assert: (Odbc numberFromDigits: '  12.500  ') equals: 25/2.
	self assert: (Odbc numberFromDigits: '+7') equals: 7
]

{ #category : 'exactness' }
DbValueTest >> testWholeDecimalReducesToAnInteger [
	"3.00 is three, and answering a Fraction with denominator one would make
	 every downstream isKindOf: Integer test fail."

	self assert: ((Odbc numberFromDigits: '3.00') isKindOf: Integer)
]

{ #category : 'types' }
DbValueTest >> testColumnTypePredicates [
	self assert: (self column: 12) isString.        "VARCHAR"
	self assert: (self column: 1) isString.         "CHAR"
	self assert: (self column: -1) isString.        "LONGVARCHAR"
	self assert: (self column: 4) isInteger.        "INTEGER"
	self assert: (self column: -5) isInteger.       "BIGINT"
	self assert: (self column: 8) isFloat.          "DOUBLE"
	self assert: (self column: 3) isExact.          "DECIMAL"
	self assert: (self column: 2) isExact.          "NUMERIC"
	self assert: (self column: -7) isBoolean.       "BIT"
	self assert: (self column: -3) isBinary.        "VARBINARY"
	self assert: (self column: 91) isDate.
	self assert: (self column: 92) isTime.
	self assert: (self column: 93) isTimestamp
]

{ #category : 'types' }
DbValueTest >> testExactIsNotFloat [
	"DECIMAL is a number and is not a float, and the difference is the
	 point.  A predicate that answered true to both would let the decimal
	 path be skipped."

	self assert: (self column: 3) isNumber.
	self deny: (self column: 3) isFloat.
	self deny: (self column: 8) isExact
]

{ #category : 'types' }
DbValueTest >> testOdbcTwoDateCodes [
	"SQL_DATE was 9 before ODBC 3 renumbered it to 91, and a driver
	 reporting version 2 still says nine."

	self assert: (self column: 9) isDate.
	self assert: (self column: 10) isTime
]

{ #category : 'types' }
DbValueTest >> testUnknownNullabilityIsTreatedAsNullable [
	"ODBC answers 2 for `the driver does not know'.  Treating that as
	 not-nullable would let a caller assume a value is always there."

	self assert: (DbColumnInfo fromArray: #('c' 12 10 0 2)) isNullable.
	self assert: (DbColumnInfo fromArray: #('c' 12 10 0 1)) isNullable.
	self deny: (DbColumnInfo fromArray: #('c' 12 10 0 0)) isNullable
]

{ #category : 'private' }
DbValueTest >> column: sqlType [
	^DbColumnInfo fromArray: (Array with: 'c' with: sqlType with: 10
		with: 0 with: 1)
]

{ #category : 'timestamps' }
DbValueTest >> testTimestampFields [
	| t |
	t := DbTimestamp fromArray: #(2026 8 24 13 45 6 123456789).
	self assert: t year equals: 2026.
	self assert: t month equals: 8.
	self assert: t day equals: 24.
	self assert: t hour equals: 13.
	self assert: t nanosecond equals: 123456789.
	self assert: t asInteger equals: 20260824
]

{ #category : 'timestamps' }
DbValueTest >> testTimestampPrinting [
	self assert: (DbTimestamp year: 2026 month: 8 day: 4
			hour: 1 minute: 2 second: 3) printString
		equals: '2026-08-04T01:02:03'.
	self assert: (DbTimestamp fromArray: #(2026 8 4 1 2 3 500000000)) printString
		equals: '2026-08-04T01:02:03.500'
]

{ #category : 'timestamps' }
DbValueTest >> testTimestampsSortChronologically [
	"Field by field, in the order they are written, because converting to a
	 number would have to choose a precision and any choice makes two
	 distinguishable moments compare equal."

	| early late |
	early := DbTimestamp fromArray: #(2026 8 4 1 2 3 100).
	late := DbTimestamp fromArray: #(2026 8 4 1 2 3 200).
	self assert: early < late.
	self deny: late < early.
	self deny: early = late.
	self assert: (DbTimestamp fromArray: #(2025 12 31 23 59 59 0))
		< (DbTimestamp fromArray: #(2026 1 1 0 0 0 0))
]

{ #category : 'timestamps' }
DbValueTest >> testTimestampConversions [
	| t |
	t := DbTimestamp fromArray: #(2026 8 24 13 45 6 0).
	self assert: t asDate year equals: 2026.
	self assert: t asDate dayOfMonth equals: 24.
	self assert: t asTime hours equals: 13.
	self assert: t asTime minutes equals: 45.
	self assert: t asTime seconds equals: 6
]

{ #category : 'availability' }
DbValueTest >> testAvailabilityAnswersWithoutRaising [
	"A build with no ODBC must answer this question rather than fail to
	 understand it -- which is why the package loads either way."

	self assert: (Odbc isAvailable == true or: [Odbc isAvailable == false]).
	self assert: (Odbc lastError isKindOf: String)
]

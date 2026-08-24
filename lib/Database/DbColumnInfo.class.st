"
What a result set says about one of its columns.

The SQL type is a NUMBER and the numbers are ODBC's -- 1 CHAR, 4 INTEGER, 12
VARCHAR, 91 DATE, 93 TIMESTAMP, and the negatives for the long and binary
types.  Those are the same numbers java.sql.Types uses, because JDBC took its
type constants from ODBC and never renumbered them.  That is why the code
this package was ported from carries its type switches over unchanged, and it
is worth knowing when reading either.

The predicates below are what callers actually want, and they are the reason
this class is not just an Array.  A caller asking `is this column a number'
should not have to know that BIGINT is -5 and TINYINT is -6, nor that
java.sql.Types spells NCHAR -15 where ODBC spells SQL_WCHAR -8 -- a genuine
divergence between the two, and the only one, handled here once so that
nothing above has to know it exists.
"
Class {
	#name : 'DbColumnInfo',
	#superclass : 'Object',
	#instVars : [ 'name', 'sqlType', 'size', 'decimalDigits', 'nullable' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbColumnInfo class >> fromArray: anArray [
	"Name, type, size, decimal digits, nullable -- as the primitive describes
	 a column."

	^self new
		setName: (anArray at: 1)
		sqlType: (anArray at: 2)
		size: (anArray at: 3)
		decimalDigits: (anArray at: 4)
		nullable: (anArray at: 5)
]

{ #category : 'private' }
DbColumnInfo >> setName: n sqlType: t size: s decimalDigits: d nullable: nu [
	name := n.
	sqlType := t.
	size := s.
	decimalDigits := d.
	nullable := nu
]

{ #category : 'accessing' }
DbColumnInfo >> name [ ^name ]

{ #category : 'accessing' }
DbColumnInfo >> sqlType [ ^sqlType ]

{ #category : 'accessing' }
DbColumnInfo >> size [ ^size ]

{ #category : 'accessing' }
DbColumnInfo >> decimalDigits [ ^decimalDigits ]

{ #category : 'accessing' }
DbColumnInfo >> isNullable [
	"ODBC answers 1 for a column that may be null, 0 for one that may not,
	 and 2 for `the driver does not know'.  Two is answered as true here:
	 treating an unknown as not-nullable would let a caller assume a value
	 is always there, and the assumption fails on the row where it isn't."

	^nullable ~= 0
]

{ #category : 'testing' }
DbColumnInfo >> isString [
	"CHAR, VARCHAR, LONGVARCHAR, and the national forms in both spellings."

	^#(1 12 -1 -8 -9 -10 -15 -16 2005 2011) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isExact [
	"DECIMAL and NUMERIC: the types whose whole point is that they are not
	 floating point.  Answered as a Fraction, never as a Float."

	^#(2 3) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isInteger [
	"TINYINT, SMALLINT, INTEGER, BIGINT."

	^#(4 5 -5 -6) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isFloat [
	"REAL, FLOAT, DOUBLE."

	^#(6 7 8) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isNumber [
	^self isInteger or: [self isFloat or: [self isExact]]
]

{ #category : 'testing' }
DbColumnInfo >> isBoolean [
	"BIT and java.sql.Types' BOOLEAN."

	^#(-7 16) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isBinary [
	"BINARY, VARBINARY, LONGVARBINARY, BLOB."

	^#(-2 -3 -4 2004) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isDate [
	"DATE, in both the ODBC 2 and ODBC 3 spellings.  Nine was SQL_DATE
	 before ODBC 3 renumbered it to 91, and a driver that reports version 2
	 still says nine."

	^#(91 9) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isTime [
	"TIME, with or without a time zone, and the ODBC 2 spelling."

	^#(92 10 2013) includes: sqlType
]

{ #category : 'testing' }
DbColumnInfo >> isTimestamp [
	^#(93 11 2014) includes: sqlType
]

{ #category : 'printing' }
DbColumnInfo >> printOn: aStream [
	aStream nextPutAll: name; nextPutAll: ' ('; print: sqlType; nextPut: $)
]

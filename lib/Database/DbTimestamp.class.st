"
A moment, to the precision a database keeps one.

This class exists because the 1983 image has no single class for a date and
a time together, and its Time is whole seconds.  A TIMESTAMP column is
neither: it is both halves at once and it carries a fraction, and rounding
that away at the boundary would be a data loss the caller never asked for and
could not detect.

It is NOT a replacement for Pharo's DateAndTime, and asDateAndTime is here so
that an image with pharo/System-Time loaded can leave this class behind at
once.  What this is, is the shape of the value as the database has it, held
exactly, in a package that must work in every profile -- including the plain
Blue Book one, where DateAndTime does not exist.

Comparison is by the seven fields in order, which is the same order they sort
in, so a collection of these sorts chronologically without a block.
"
Class {
	#name : 'DbTimestamp',
	#superclass : 'Magnitude',
	#instVars : [ 'year', 'month', 'day', 'hour', 'minute', 'second', 'nanosecond' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbTimestamp class >> fromArray: anArray [
	"Seven integers, as the primitive answers them."

	^self new
		setYear: (anArray at: 1)
		month: (anArray at: 2)
		day: (anArray at: 3)
		hour: (anArray at: 4)
		minute: (anArray at: 5)
		second: (anArray at: 6)
		nanosecond: (anArray at: 7)
]

{ #category : 'instance creation' }
DbTimestamp class >> year: y month: mo day: d hour: h minute: mi second: s [
	^self new setYear: y month: mo day: d hour: h minute: mi second: s
		nanosecond: 0
]

{ #category : 'instance creation' }
DbTimestamp class >> date: aDate time: aTime [
	^self new
		setYear: aDate year
		month: aDate monthIndex
		day: aDate dayOfMonth
		hour: aTime hours
		minute: aTime minutes
		second: aTime seconds
		nanosecond: 0
]

{ #category : 'private' }
DbTimestamp >> setYear: y month: mo day: d hour: h minute: mi second: s nanosecond: n [
	year := y.
	month := mo.
	day := d.
	hour := h.
	minute := mi.
	second := s.
	nanosecond := n
]

{ #category : 'accessing' }
DbTimestamp >> year [ ^year ]

{ #category : 'accessing' }
DbTimestamp >> month [ ^month ]

{ #category : 'accessing' }
DbTimestamp >> day [ ^day ]

{ #category : 'accessing' }
DbTimestamp >> hour [ ^hour ]

{ #category : 'accessing' }
DbTimestamp >> minute [ ^minute ]

{ #category : 'accessing' }
DbTimestamp >> second [ ^second ]

{ #category : 'accessing' }
DbTimestamp >> nanosecond [ ^nanosecond ]

{ #category : 'converting' }
DbTimestamp >> asArray [
	"The seven fields, in the order the primitive binds them."

	^(Array new: 7)
		at: 1 put: year;
		at: 2 put: month;
		at: 3 put: day;
		at: 4 put: hour;
		at: 5 put: minute;
		at: 6 put: second;
		at: 7 put: nanosecond;
		yourself
]

{ #category : 'converting' }
DbTimestamp >> asDate [
	^Date newDay: day month: (Date nameOfMonth: month) year: year
]

{ #category : 'converting' }
DbTimestamp >> asTime [
	"Whole seconds, because that is all a 1983 Time holds.  The fraction is
	 still here on the receiver; this answers the part Time can carry."

	^Time fromSeconds: hour * 3600 + (minute * 60) + second
]

{ #category : 'converting' }
DbTimestamp >> asDateAndTime [
	"Pharo's DateAndTime, in an image that has one.

	 Deliberately a runtime lookup rather than a direct reference: this
	 package loads into the plain Blue Book profile too, where the global
	 does not exist, and a method that mentions it directly would fail to
	 compile there and take the package with it."

	| class |
	class := Smalltalk at: #DateAndTime ifAbsent: [nil].
	class isNil ifTrue:
		[^DbError signal: 'this image has no DateAndTime -- load pharo/System-Time'].
	^class
		year: year month: month day: day
		hour: hour minute: minute second: second
		nanoSecond: nanosecond
		offset: (Smalltalk at: #Duration) zero
]

{ #category : 'converting' }
DbTimestamp >> asSeconds [
	"Seconds since the Smalltalk epoch, which is what a 1983 image counts
	 from.  The fraction is dropped; asMilliseconds keeps three digits of it."

	^self asDate asSeconds + (hour * 3600) + (minute * 60) + second
]

{ #category : 'converting' }
DbTimestamp >> asMilliseconds [
	^self asSeconds * 1000 + (nanosecond // 1000000)
]

{ #category : 'converting' }
DbTimestamp >> asInteger [
	"yyyymmdd, which is how Kiss's getDateAsInt names a day and how a great
	 deal of application code stores one."

	^year * 10000 + (month * 100) + day
]

{ #category : 'comparing' }
DbTimestamp >> = anObject [
	self class == anObject class ifFalse: [^false].
	^self asComparisonArray = anObject asComparisonArray
]

{ #category : 'comparing' }
DbTimestamp >> hash [
	^self asComparisonArray hash
]

{ #category : 'comparing' }
DbTimestamp >> < aTimestamp [
	| mine theirs |
	mine := self asComparisonArray.
	theirs := aTimestamp asComparisonArray.
	1 to: mine size do:
		[:i |
		(mine at: i) = (theirs at: i) ifFalse: [^(mine at: i) < (theirs at: i)]].
	^false
]

{ #category : 'private' }
DbTimestamp >> asComparisonArray [
	"The seven fields in the order they sort in, which is the order they are
	 written in.  Comparing field by field rather than by a converted
	 number: a conversion would have to choose a precision, and any choice
	 makes two distinguishable moments compare equal."

	^self asArray
]

{ #category : 'printing' }
DbTimestamp >> printOn: aStream [
	"ISO 8601, because it is the one spelling every database and every
	 reader agrees on, and it sorts as text in the order it sorts as time."

	self print: year digits: 4 on: aStream.
	aStream nextPut: $-.
	self print: month digits: 2 on: aStream.
	aStream nextPut: $-.
	self print: day digits: 2 on: aStream.
	aStream nextPut: $T.
	self print: hour digits: 2 on: aStream.
	aStream nextPut: $:.
	self print: minute digits: 2 on: aStream.
	aStream nextPut: $:.
	self print: second digits: 2 on: aStream.
	nanosecond = 0 ifFalse:
		[aStream nextPut: $..
		 self print: nanosecond // 1000000 digits: 3 on: aStream]
]

{ #category : 'private' }
DbTimestamp >> print: anInteger digits: count on: aStream [
	| text |
	text := anInteger printString.
	count - text size timesRepeat: [aStream nextPut: $0].
	aStream nextPutAll: text
]

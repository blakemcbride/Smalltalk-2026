"
What the 1983 image still asks of Date after Pharo's replaced it.
See Time.extension.st in this package for why these exist at all.
"
Extension { #name : 'Date' }

{ #category : '*Chronology-Compat' }
Date class >> leapYear: yearInteger [
	"1 if it is a leap year, 0 if it is not.

	 An INTEGER, not a Boolean, and that is not a mistake: 1983's
	 daysInYear: is written `^365 + (self leapYear: yearInteger)'.  Pharo
	 spells the same question `isLeapYear' and answers a Boolean, which is
	 better and is not what the callers here expect."
	(yearInteger \\ 4 ~= 0
		or: [yearInteger \\ 100 = 0 and: [yearInteger \\ 400 ~= 0]])
			ifTrue: [^0].
	^1
]

{ #category : '*Chronology-Compat' }
Date class >> readFrom: aStream [
	"A day, a month name and a year -- `1 January 2026' -- which is the
	 format 1983 writes and reads."
	| day monthName year |
	[aStream atEnd not and: [aStream peek isDigit not]] whileTrue: [aStream next].
	day := Integer readFrom: aStream.
	[aStream atEnd not and: [aStream peek isLetter not]] whileTrue: [aStream next].
	monthName := WriteStream on: (String new: 12).
	[aStream atEnd not and: [aStream peek isLetter]]
		whileTrue: [monthName nextPut: aStream next].
	[aStream atEnd not and: [aStream peek isDigit not]] whileTrue: [aStream next].
	year := Integer readFrom: aStream.
	^self
		newDay: day
		month: (self indexOfMonth: monthName contents)
		year: year
]

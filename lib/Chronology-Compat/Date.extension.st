"
What the 1983 image still asks of Date after Pharo's replaced it.
See Time.extension.st in this package for why these exist at all.

A method that was here and is not: Date class>>readFrom:.

It was a shim for the one form 1983 writes -- `1 January 2026' -- and it
read only that form: `4/5/82' came out as `4 January 0000', `13/13/2024' as
`13 January 0000', and `2024-01-02' as a DateError about the day.  It also
shadowed a better reader that was already installed on this class.  A
#supersede drops a class's DEFINITION and keeps every extension file on
it, so lib/Kernel-Protocol/Date.extension.st -- loaded before System-Time,
and written against nothing but newDay:month:year: with a month name,
which Pharo's Date takes as 1983's does -- was there under the shim the
whole time, reading all four of 1983's forms and refusing what is not a
date (Bugs3 B51).  The lesson is the one Time.extension.st already states:
check what the built image answers before writing a shim for it.
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

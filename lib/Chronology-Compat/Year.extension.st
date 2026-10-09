"
Leap years as the Julian-day arithmetic counts them.

Every date computation in pharo/System-Time is proleptic Gregorian -- the
year 0 exists and is a leap year, the year -1 is not -- except Year
class>>isLeapYear:, which mapped a year at or below 0 to -(y + 1) on the
ground that there was no year 0.  So February of the year 0 was 29 days by
subtraction while 29 February 0 was refused, 29 February -1 was accepted and
printed as 1 March, and dayOfYear was off by one from March on in every such
year (Bugs6 KERNA-25).  The plain rule on the year as given agrees with the
arithmetic.
"
Extension { #name : 'Year' }

{ #category : '*Chronology-Compat' }
Year class >> isLeapYear: aYearInteger [
	"The proleptic Gregorian rule on the year as given; see the comment
	 above.  \\ is floor modulus, so a negative year is divided as the
	 Julian-day arithmetic divides it."
	^(aYearInteger \\ 4 = 0)
		and: [(aYearInteger \\ 100 ~= 0) or: [aYearInteger \\ 400 = 0]]
]

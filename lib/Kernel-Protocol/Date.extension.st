"
Date>>hash, from what = compares.

1983's is `(year hash bitShift: 3) bitXor: day', which folds a day number of
up to nine bits into a year shifted by three: the low bits of the two
overlap and cancel, so a run of consecutive days hashed to a tenth as many
values -- 510 of 5,000 -- and a Set of five thousand Dates took twenty-four
seconds to build where `Date fromDays:' alone took twenty-two milliseconds
(Bugs3 B33).  Dates are keys: a daily total, a schedule, a log by day.

Time>>hash in this package was given `asSeconds' for the same fault, and
this is the same repair.  asSeconds is a function of the day and the year
alone, so two Dates that = says are equal hash the same, and it is a
different number for every different day, so consecutive days spread
across a table; Set>>findElementOrNil: mixes it before choosing a bucket.

asSeconds rather than `year * 512 + day', which was the first version and
is cheaper: profiles/pharo-time supersedes Date with Pharo's, whose
instance variables are a start and a duration and which has no `year' or
`day' of its own, and an extension that names those compiles there against
nothing -- DateTest>>testComparing found `nil * 512'.  asSeconds is protocol
both Dates answer, and Pharo's Timespan>>= compares the start it is
computed from, so the hash is lawful for either.

A Date read from text, and one refusal for text that is not one.

1983's Date class>>readFrom: reads three fields in any of its three orders
and checks nothing: `13/13/2024' went to nameOfMonth: 13 and died in a
SubscriptOutOfBounds, `2024-01-02' went there with 2024, `31/2/2024' with
31, and `garbage' -- letters to the end, then nil -- was a doesNotUnderstand
of isLetter.  Four different errors for one kind of mistake, none of which
said `that is not a date' (Bugs3 B51).  This reader takes the same three
forms, and the year-first form with four digits that every file written
this century uses, and refuses everything else in one sentence that quotes
the text.

It is written against no more of Date than newDay:month:year: with a month
NAME, which is what 1983 wants and what Pharo's Date also takes, so that the
pharo-time profile -- where Pharo's Date supersedes 1983's and this
extension file is kept -- reads the same forms the same way.  The month
names and lengths are its own for the same reason: Pharo's Date has no
nameOfMonth:, and 1983's tables are class variables the other has not got.
"
Extension { #name : 'Date' }

{ #category : '*Kernel-Protocol' }
Date >> hash [
	"Seconds since the base day, which is a function of exactly what =
	 compares.  See the comment above for why not the fields themselves."
	^self asSeconds hash
]


{ #category : '*Kernel-Protocol' }
Date class >> readFrom: aStream [
	"A Date from text in any of the forms

		<day> <month name> <year>       5 April 1982   5-APR-82   5APR82
		<month name> <day> <year>       April 5, 1982
		<month number> <day> <year>     4/5/82
		<year> <month number> <day>     2024-01-02  (the year has four digits)

	 The three fields are runs of digits or of letters; whatever lies
	 between them -- spaces, slashes, dashes, commas -- is skipped, as 1983
	 skips it.  Which field is which is decided by where the letters are,
	 and, when there are none, by the first number: a number no day or
	 month can be is the year, which is how the year-first form is told
	 from the month-first one without ambiguity.  A two-digit year is
	 passed through as 1983 passes it.

	 Every field is checked -- a month name that names no month, a month
	 past twelve, a day past the end of its month, a February 29th in a
	 year that has none -- and refused with the same error, because a
	 caller with bad text wants one thing to catch."
	| fields field day month year names |
	fields := OrderedCollection new.
	[fields size < 3 and: [aStream atEnd not]] whileTrue: [
		aStream peek isDigit
			ifTrue: [fields add: (Integer readFrom: aStream)]
			ifFalse: [
				aStream peek isLetter
					ifTrue: [
						field := WriteStream on: (String new: 10).
						[aStream atEnd not and: [aStream peek isLetter]]
							whileTrue: [field nextPut: aStream next].
						fields add: field contents]
					ifFalse: [aStream next]]].
	fields size = 3 ifFalse: [^self notADate: aStream].
	(fields at: 3) isString ifTrue: [^self notADate: aStream].
	(fields at: 1) isString
		ifTrue: [month := fields at: 1. day := fields at: 2. year := fields at: 3]
		ifFalse: [
			(fields at: 2) isString
				ifTrue: [day := fields at: 1. month := fields at: 2. year := fields at: 3]
				ifFalse: [
					(fields at: 1) > 31
						ifTrue: [year := fields at: 1. month := fields at: 2. day := fields at: 3]
						ifFalse: [month := fields at: 1. day := fields at: 2. year := fields at: 3]]].
	names := #('January' 'February' 'March' 'April' 'May' 'June'
		'July' 'August' 'September' 'October' 'November' 'December').
	month isString ifTrue: [month := self monthIndexOf: month in: names].
	(month isInteger and: [month between: 1 and: 12])
		ifFalse: [^self notADate: aStream].
	(day isInteger and: [day between: 1 and: (self daysInMonthNumber: month year: year)])
		ifFalse: [^self notADate: aStream].
	^self newDay: day month: (names at: month) year: year
]

{ #category : '*Kernel-Protocol' }
Date class >> monthIndexOf: aString in: names [
	"The index of the month that text names, or nil.

	 A prefix of at least three letters, compared without regard to case,
	 which takes `apr', `APR' and `April' and does not take `A' -- 1983's
	 indexOfMonth: matched any prefix at all and answered the first month
	 that fitted, so a single letter named a month by accident."
	aString size < 3 ifTrue: [^nil].
	1 to: 12 do: [:i |
		(aString size <= (names at: i) size
			and: [((names at: i) copyFrom: 1 to: aString size) sameAs: aString])
				ifTrue: [^i]].
	^nil
]

{ #category : '*Kernel-Protocol' }
Date class >> daysInMonthNumber: monthIndex year: year [
	"How many days that month has in that year, with February's 29th in a
	 leap year: every fourth year, except the centuries that are not
	 fourth centuries."
	| days |
	days := #(31 28 31 30 31 30 31 31 30 31 30 31) at: monthIndex.
	(monthIndex = 2
		and: [year \\ 4 = 0 and: [year \\ 100 ~= 0 or: [year \\ 400 = 0]]])
			ifTrue: [days := 29].
	^days
]

{ #category : '*Kernel-Protocol' }
Date class >> notADate: aStream [
	"Refuse, quoting the text -- the whole of the stream's contents, cut at
	 forty characters so that a stream over a file names its start rather
	 than all of itself."
	| text |
	text := aStream contents.
	text size > 40 ifTrue: [text := (text copyFrom: 1 to: 40) , '...'].
	^self error: 'not a date: ' , text printString
]

"
Date arithmetic outside 1901-2099, and in constant time.

1983 counts the days between two Dates with `year - 1 // 4 - (aDate year //
4)' -- every fourth year a leap year, no century term at all -- and builds
a Date from a day count with a flat 1461-day four-year cycle from 1901.
Neither knows about 1900 or 2100, and the two do not even agree with each
other: over 527 dates in 1600-9999, 386 subtractDate: results, 305
asSeconds and 299 fromDays: differed from the same arithmetic done by
Python's datetime, and `(Date fromDays: 2958098) printString' -- 31
December 9999 -- answered `1 November 9999' (Bugs4 CHRON-4).  Inside
1901-2099 nothing differs, which is why it lasted: 1900 and 2100 are the
two years the shortcut gets wrong, and the Blue Book's own century is
between them.

The five methods below replace the shortcut with a day-of-era count and its
inverse -- Howard Hinnant's formulation, four hundred years to the era and
146,097 days to the four hundred, with March as the first month so that the
leap day falls at the end where it does not shift anything.  daysFrom1901-
ToStartOfYear: is exact for every Gregorian year, proleptic in both
directions, and every other method here is written in terms of it, so the
three that disagreed cannot disagree again: subtractDate: is a difference
of two of them, fromDays: and newDay:year: are its inverse, and asSeconds
-- which 1983 already writes as a subtractDate: from 1 January 1901 -- is
carried along with it unchanged.

They are also O(1), which 1983's newDay:year: was not: it walks one year
per iteration from the reference year, so `aDate addDays: (2 raisedTo: 40)'
-- addDays: is newDay:year: with a bigger day count -- runs for three
billion iterations and never finishes (Bugs4 CHRON-7).  The two whileTrue:
loops in yearForDaysFrom1901: each run at most once: the era formula
answers the year of a March-to-February year, which is the calendar year
for ten months of the twelve, and the loops settle January and February.

In profiles/pharo-time these four are shadowed by Pharo's own Date, whose
arithmetic is on Julian day numbers and passed all 527.  They are written
with `self day' and `self year' rather than the instance variables so that
they still COMPILE there, where Date is Pharo's and has neither.
"

{ #category : '*Kernel-Protocol' }
Date class >> daysFrom1901ToStartOfYear: yearInteger [
	"The number of days from 1 January 1901 to 1 January yearInteger,
	 negative for a year before 1901.

	 The count is of days in a 400-year era beginning on 1 March, which is
	 what makes it exact: with March first, the leap day is the last day of
	 the year and adding it shifts nothing after it.  306 is the days from
	 1 March to 1 January, 146,097 the days in an era, 719,468 the days
	 from the era's zero to 1 January 1970, and 25,202 the days from 1
	 January 1901 to 1 January 1970 -- the same number ST_EPOCH_OFFSET_SEC
	 carries in the VM.

	 // is floor division here, which is what makes the formula work for a
	 year before the era's zero as well as after it."
	| priorYear era yearOfEra |
	priorYear := yearInteger - 1.
	era := priorYear // 400.
	yearOfEra := priorYear - (era * 400).
	^era * 146097
		+ (yearOfEra * 365 + (yearOfEra // 4) - (yearOfEra // 100) + 306)
		- 719468 + 25202
]

{ #category : '*Kernel-Protocol' }
Date class >> yearForDaysFrom1901: dayCount [
	"The calendar year the dayCount'th day after 1 January 1901 falls in.

	 The inverse of daysFrom1901ToStartOfYear:, and read back the same way:
	 the era arithmetic answers the year of a year that begins in March, so
	 it names the calendar year for March to December and the year before
	 for January and February.  The two loops settle that, and each can run
	 at most once -- they are a correction of one, not a search."
	| z era dayOfEra yearOfEra year |
	z := dayCount - 25202 + 719468.
	era := z // 146097.
	dayOfEra := z - (era * 146097).
	yearOfEra := dayOfEra - (dayOfEra // 1460) + (dayOfEra // 36524)
					- (dayOfEra // 146096) // 365.
	year := yearOfEra + (era * 400) + 1.
	[dayCount < (self daysFrom1901ToStartOfYear: year)]
		whileTrue: [year := year - 1].
	[dayCount >= (self daysFrom1901ToStartOfYear: year + 1)]
		whileTrue: [year := year + 1].
	^year
]

{ #category : '*Kernel-Protocol' }
Date class >> newDay: dayCount year: referenceYear [
	"The Date that is dayCount days after the beginning of referenceYear,
	 where day 1 is 1 January.

	 dayCount need not be inside the year and need not be positive: this is
	 what addDays: and subtractDays: are written in terms of, so it is
	 asked for day 400 and for day −5,000 as a matter of course.  1983
	 walked to the right year one year at a time, adding or subtracting a
	 year's days per iteration, which is fine for a week and does not
	 finish for `addDays: (2 raisedTo: 40)' (Bugs4 CHRON-7).  Here the
	 whole count is turned into a day number and turned back."
	| absolute year |
	absolute := (self daysFrom1901ToStartOfYear: referenceYear) + dayCount - 1.
	year := self yearForDaysFrom1901: absolute.
	^self new
		day: absolute - (self daysFrom1901ToStartOfYear: year) + 1
		year: year
]

{ #category : '*Kernel-Protocol' }
Date class >> fromDays: dayCount [
	"The Date that is dayCount days after 1 January 1901.

	 1983 divides by 1461, the days in a four-year cycle, and takes every
	 fourth year in it as a leap year.  That is exactly right from 1901 to
	 2099 and wrong at either end of them: `Date fromDays: 2958098' is 31
	 December 9999 and answered 1 November 9999 (Bugs4 CHRON-4).

	 Written through newDay:year: so that one method decides which year a
	 day number falls in; day 0 is 1 January 1901, which is day 1 of 1901."
	^self newDay: dayCount truncated + 1 year: 1901
]

{ #category : '*Kernel-Protocol' }
Date >> subtractDate: aDate [
	"The number of days between the receiver and aDate.

	 1983 counts the leap years between them as `year - 1 // 4 - (aDate
	 year // 4)', which has no century term, so 1900 and 2100 are leap
	 years to it: `(Date newDay: 1 month: #January year: 2101) subtractDate:
	 (Date newDay: 1 month: #January year: 2099)' answered 731 for 730
	 (Bugs4 CHRON-4).  A difference of two day numbers has nothing left to
	 get wrong, and it carries Date>>asSeconds -- written in 1983 as a
	 subtractDate: from 1 January 1901 -- with it."
	^(Date daysFrom1901ToStartOfYear: self year) + self day
		- ((Date daysFrom1901ToStartOfYear: aDate year) + aDate day)
]

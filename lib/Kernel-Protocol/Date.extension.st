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

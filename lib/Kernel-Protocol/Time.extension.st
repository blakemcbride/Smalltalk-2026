"
A Time is a time of DAY, and now says so at both ends.

`Time fromSeconds:' took any Integer and stored it without reducing, and
`Time>>printOn:' turned hours into a twelve-hour clock by subtracting twelve
rather than by taking the remainder.  Between them:

	(Time fromSeconds: 86400) printString     '12:00:00 pm'   hours = 24
	(Time fromSeconds: 90000) printString     '13:00:00 pm'
	(Time fromSeconds: -1)    printString     '12:59:59 am'

-- an afternoon that is really midnight, an hour that does not exist on any
clock, and a negative second printed as though it were just before one.
Seconds since midnight is exactly the kind of number that arrives from
arithmetic rather than from a literal -- a duration added to a start time,
a timestamp reduced -- so passing 86400 or -1 is ordinary, not perverse.

fromSeconds: now reduces modulo a day, which makes it total: every Integer
names a time.  printOn: takes the remainder as well, so a Time built some
other way -- hours:minutes:seconds: takes what it is given -- still prints
as a clock rather than as arithmetic.

hash was `((hours hash bitShift: 3) bitXor: minutes) bitXor: seconds', which
for times inside one hour reduces to `minutes bitXor: seconds': 200
consecutive seconds produced 60 distinct values.  Not wrong, and weak
exactly where Times are used as keys.  asSeconds is a bijection onto the
day and is what = compares, one field at a time, so hashing it is both
faster and perfect.
"
Extension { #name : 'Time' }

{ #category : '*Kernel-Protocol' }
Time class >> fromSeconds: secondCount [
	"A Time that is secondCount seconds after midnight, reduced into the
	 day it lands in.  \\ and not rem:, so that a negative count counts
	 BACK from midnight: -1 is 23:59:59, not an hour before zero."
	| secondsInHour seconds hours |
	seconds := secondCount truncated \\ 86400.
	hours := seconds // 3600.
	secondsInHour := seconds \\ 3600.
	^self new hours: hours
			  minutes: secondsInHour // 60
			  seconds: secondsInHour \\ 60
]

{ #category : '*Kernel-Protocol' }
Time >> printOn: aStream [
	"Format is h:mm:ss am/pm, on a twelve-hour clock that wraps rather
	 than subtracting: hour 0 and hour 12 both print as 12, and hour 24 --
	 which fromSeconds: no longer produces but hours:minutes:seconds: still
	 accepts -- prints as midnight rather than as noon."
	| clockHours |
	clockHours := hours \\ 12.
	clockHours = 0 ifTrue: [clockHours := 12].
	clockHours printOn: aStream.
	aStream nextPutAll: (minutes < 10 ifTrue: [':0'] ifFalse: [':']).
	minutes printOn: aStream.
	aStream nextPutAll: (seconds < 10 ifTrue: [':0'] ifFalse: [':']).
	seconds printOn: aStream.
	aStream nextPutAll: (hours \\ 24 < 12 ifTrue: [' am'] ifFalse: [' pm'])
]

{ #category : '*Kernel-Protocol' }
Time >> hash [
	"Seconds since midnight, which is exactly what = compares."
	^self asSeconds hash
]

{ #category : '*Kernel-Protocol' }
Time class >> readFrom: aStream [
	"A Time from text in the form 1983 writes and reads --
	 <hour>:<minute>:<second> <am/pm>, where the minute, the second and
	 the suffix may each be left out: `1:59:30 pm', `8AM', `15:30' -- and
	 a refusal for text that is not one.

	 1983's reader added twelve for `pm' and left `am' alone, and on a
	 twelve-hour clock that is wrong at exactly the two hours people most
	 often write down: `12:00:00 pm' became hour 24, which fromSeconds:
	 reduces to midnight, and `12:00:00 am' stayed hour 12, which is noon.
	 The two were swapped, so `(Time fromSeconds: 43200) storeString' --
	 noon, stored as `(Time readFromString: '12:00:00 pm')' -- evaluated
	 to midnight, and 119 of the day's minutes did not survive printString
	 and readFromString: (Bugs3 B48).  On a twelve-hour clock the hour
	 written 12 is hour ZERO of its half, which is what the `hour = 12'
	 line says, and it has to be said before the twelve is added.

	 The same reader answered midnight for `garbage' and one in the morning
	 for `25:00', because Integer class>>readFrom: answers 0 when there
	 are no digits and fromSeconds: folds any count into the day.  A
	 reader that turns every mistake into a valid time hides the mistake,
	 so the hour, the minute and the second are each checked against the
	 clock, a suffix has to be am or pm, and text with no hour at all is
	 refused -- in one error, which quotes the text."
	| hour minute second suffix |
	aStream skipSeparators.
	(aStream atEnd not and: [aStream peek isDigit])
		ifFalse: [^self notATime: aStream].
	hour := Integer readFrom: aStream.
	minute := 0.
	second := 0.
	(aStream peekFor: $:) ifTrue: [
		(aStream atEnd not and: [aStream peek isDigit])
			ifFalse: [^self notATime: aStream].
		minute := Integer readFrom: aStream.
		(aStream peekFor: $:) ifTrue: [
			(aStream atEnd not and: [aStream peek isDigit])
				ifFalse: [^self notATime: aStream].
			second := Integer readFrom: aStream]].
	aStream skipSeparators.
	suffix := nil.
	(aStream atEnd not and: [aStream peek isLetter]) ifTrue: [
		suffix := aStream next asLowercase.
		(suffix = $a or: [suffix = $p]) ifFalse: [^self notATime: aStream].
		(aStream peekFor: $m) ifFalse: [aStream peekFor: $M]].
	suffix notNil ifTrue: [
		"A twelve-hour clock: 12 is the first hour of its half, not the
		 thirteenth, and there is no hour 13 on it at all."
		hour > 12 ifTrue: [^self notATime: aStream].
		hour = 12 ifTrue: [hour := 0].
		suffix = $p ifTrue: [hour := hour + 12]].
	((hour between: 0 and: 23)
		and: [(minute between: 0 and: 59) and: [second between: 0 and: 59]])
			ifFalse: [^self notATime: aStream].
	^self fromSeconds: 60 * (60 * hour + minute) + second
]

{ #category : '*Kernel-Protocol' }
Time class >> notATime: aStream [
	"Refuse, quoting the text -- the whole of the stream's contents,
	 because a time is short and the mistake is as often in what follows
	 the part that was read as in that part; cut at forty characters so
	 that a stream over a file names its start rather than all of itself."
	| text |
	text := aStream contents.
	text size > 40 ifTrue: [text := (text copyFrom: 1 to: 40) , '...'].
	^self error: 'not a time: ' , text printString
]

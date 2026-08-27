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

"
The local time zone, actually asked of the host.

Pharo's LocalTimeZone>>primOffset is a call to primitiveTimezoneOffset in
the LocalePlugin with `^ 0' behind it, and there is no LocalePlugin here,
so it answered zero and this profile kept UTC.  The st2026 profile in the
same build answered California -- 1983's Time class>>currentTime: carries
the Alto's `m570 _ 16505. m571 _ 305' -- so one build had two profiles
seven hours apart, and neither of them was the machine's own time (Bugs4
CHRON-3).

Primitive 219 reads the host's zone database through localtime_r, which is
the real rule for the real place rather than a rule from 1983, and
Time class>>localOffsetSeconds in lib/Kernel-Protocol is where both
profiles ask it.  So `DateAndTime now', `Date today' and `Time now' answer
local time here and in st2026, and they answer the SAME local time.

Minutes rather than seconds, because that is what primOffset's callers
expect: LocalTimeZone>>offset sends `minutes' to it and every zone Pharo
names is a whole number of minutes from UTC.  A zone that is not -- there
were several before 1900, and the tz database still carries them -- rounds
toward zero here, which is the same thing Pharo's own primitive does.
"
Extension { #name : 'LocalTimeZone' }

{ #category : '*Chronology-Compat' }
LocalTimeZone >> primOffset [
	"The host's offset from UTC in minutes, positive towards the east."
	^Time localOffsetSeconds quo: 60
]

{ #category : '*Chronology-Compat' }
LocalTimeZone >> abbreviation [
	"LT, the offset's sign, then its hours and minutes without one:
	 LT-2:30, LT5:30, LT-0:30.

	 The sign belongs to the whole offset, not to either part.  Printing
	 the hours with theirs and the minutes with theirs gave LT-2:-30 for a
	 negative half-hour zone (Bugs6 KERNA-32); printing the minutes
	 without theirs fixed that and lost the sign of every zone less than
	 an hour west of UTC, whose hours are 0 -- UTC-0:30 and UTC+0:30 both
	 printed LT0:30 (Bugs07 KERNA-12).  No zone in today's tz database is
	 one, but a POSIX TZ string can say it."
	| offset |
	offset := self offset.
	^String streamContents: [:s |
		s nextPutAll: 'LT'.
		offset negative ifTrue: [s nextPut: $-].
		s print: offset hours abs; nextPut: $:.
		s nextPutAll: (offset minutes abs printPaddedWith: $0 to: 2)]
]

"
Three answers of Pharo's DateAndTime put right for instants before an epoch.
"
Extension { #name : 'DateAndTime' }

{ #category : '*Chronology-Compat' }
DateAndTime >> dayOfWeek [
	"Sunday=1, ... , Saturday=7 -- for a negative Julian day number too.
	 rem: keeps the sign, so a date before 24 November 4714 BC answered 0
	 or less and dayOfWeekName indexed below the table (Bugs6 KERNA-26);
	 \\ is floor modulus."
	^(self julianDayNumber + 1 \\ 7) + 1
]

{ #category : '*Chronology-Compat' }
DateAndTime >> asUnixTime [
	"The number of seconds since the Unix epoch (midnight 1 January 1970,
	 UTC) -- the FLOOR, which is what Unix time means and what
	 fromUnixTime: inverts.  A negative Duration keeps its seconds toward
	 zero and its nanoseconds negative, so the whole-second count of half
	 a second before the epoch was 0, and every instant before 1970 with a
	 fractional second reported the second after its own (Bugs6
	 KERNA-33)."
	^((self offset: Duration zero) - self class unixEpoch) asNanoSeconds
		// 1000000000
]

{ #category : '*Chronology-Compat' }
DateAndTime >> asSeconds [
	"The number of seconds since the epoch, 1 January 1901: the floor, for
	 the reason asUnixTime gives."
	^(self - self class epoch) asNanoSeconds // 1000000000
]

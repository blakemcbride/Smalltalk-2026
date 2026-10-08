"
What Pharo's Duration gets wrong on its own, kept here because pharo/ is
imported byte for byte and this package loads after it.
"
Extension { #name : 'Duration' }

{ #category : '*Chronology-Compat' }
Duration >> seconds: secondCount nanoSeconds: nanoCount [
	"Set my two fields from a count of seconds and one of nanoseconds,
	 normalised: the nanoseconds are less than a second in magnitude and
	 never of the other sign from the seconds.

	 Pharo's own repairs only the sign.  A nanosecond count of a second or
	 more was stored as it came, and the constructors that do not go
	 through nanoSeconds: -- seconds:nanoSeconds: and days:hours:minutes:
	 seconds:nanoSeconds: -- bring one here.  (Time>>asDuration comes
	 here too, with fields Time has already refused to hold out of range.)
	 `Duration seconds: 1 nanoSeconds: 1500000000' printed as 0:00:00:01.15,
	 answered 1 to seconds and 2500 to asMilliSeconds, and hashed
	 differently from the 2.5 seconds it was = to, since = compares the
	 total and hash the two fields (Bugs6 KERNA-4).  Whole seconds are
	 carried out of the nanoseconds first, by quo: so that the two keep one
	 sign, and Pharo's sign repair runs after on what is left, as it
	 always did.  Pharo's DurationTest>>testNormalizeNanoSeconds covers the
	 sign cases; DurationCompatTest in ../Chronology-Compat-Tests covers
	 the carry."
	| carry |
	seconds := secondCount.
	nanos := nanoCount rounded.
	nanos abs >= NanosInSecond
		ifTrue:
			[carry := nanos quo: NanosInSecond.
			seconds := seconds + carry.
			nanos := nanos - (carry * NanosInSecond)].
	"normalize if signs do not match"
	[nanos < 0 and: [seconds > 0]]
		whileTrue:
			[seconds := seconds - 1.
			nanos := nanos + NanosInSecond].
	[seconds < 0 and: [nanos > 0]]
		whileTrue:
			[seconds := seconds + 1.
			nanos := nanos - NanosInSecond]
]

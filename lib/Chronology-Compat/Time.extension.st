"
What the 1983 image still asks of Time after Pharo's replaced it.

pharo/System-Time supersedes the 1983 Date and Time, because the rest of
Chronology is built on Pharo's.  Supersession is not free: the 1983 library
is still there and still sends what it always sent, and a replaced class
that quietly stops answering is worse than one that is missing, because
nothing announces it.

These were found by enumerating every `Time <selector>' and
`Date <selector>' in sources/ and trying all fourteen against the loaded
image, rather than by waiting for something to break.  Five did not answer;
three of them are here and two on Date.

A method that was here and is not: Time class>>readFrom:.

It was added because the 1983 sources send it and it looked missing, on a
comment written here that said Pharo `does not answer this selector'.  Pharo
answers it perfectly well -- Time.class.st line 233 -- and does so with
nanosecond and am/pm support this version never had.  Because this package
loads AFTER System-Time, the shim did not fill a gap, it SHADOWED the real
one, and every fractional second in the suite parsed as zero.

The lesson is about compatibility shims generally, not about Time: one added
to fill a gap that is not really there is worse than nothing, because it
silently replaces something better.  Check that the selector is actually
unanswered in the built image before writing one.
"
Extension { #name : 'Time' }

{ #category : '*Chronology-Compat' }
Time class >> millisecondsToRun: timedBlock [
	"How many milliseconds timedBlock takes to answer.

	 1983's own words, and its own arithmetic: the clock is read before and
	 after and the difference answered.  MessageTally and every benchmark in
	 the image go through this."
	| started |
	started := self millisecondClockValue.
	timedBlock value.
	^self millisecondClockValue - started
]

{ #category : '*Chronology-Compat' }
Time class >> dateAndTimeNow [
	"An Array of (Date today, Time now), which is what 1983 means by it."
	^Array with: Date today with: Time now
]


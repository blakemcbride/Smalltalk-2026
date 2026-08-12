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

{ #category : '*Chronology-Compat' }
Time class >> readFrom: aStream [
	"Hours, minutes and optional seconds, colon separated -- 1983's format.
	 Pharo parses a richer syntax through DateAndTime and does not answer
	 this selector."
	| hour minute second |
	hour := Integer readFrom: aStream.
	minute := 0.
	second := 0.
	(aStream atEnd not and: [aStream peek = $:])
		ifTrue: [
			aStream next.
			minute := Integer readFrom: aStream.
			(aStream atEnd not and: [aStream peek = $:])
				ifTrue: [
					aStream next.
					second := Integer readFrom: aStream]].
	^self seconds: hour * 3600 + (minute * 60) + second
]

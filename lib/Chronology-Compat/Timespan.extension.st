"
Working days are Monday to Friday.
"
Extension { #name : 'Timespan' }

{ #category : '*Chronology-Compat' }
Timespan >> workDatesDo: aBlock [
	"Exclude Saturday and Sunday.  Pharo's test was `dayOfWeek < 6', which
	 on a Sunday-first week keeps Sunday to Thursday (Bugs6 KERNA-27)."
	self do: aBlock with: start asDate when: [:d | d dayOfWeek between: 2 and: 6]
]

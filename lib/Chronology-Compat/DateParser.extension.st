"
A four-digit year is the year it says.

Pharo's DateParser applied its two-digit shorthand -- 50 means 2050 -- to
the parsed NUMBER, after every pattern, so `0050-01-02' under yyyy-mm-dd
read as the year 2050 and DateAndTime>>printOn:, which writes a four-digit
year, did not round-trip through fromString: for the years 0 to 99 (Bugs6
KERNA-24).  The rule belongs where a year is read from one or two digits,
and nowhere else.
"
Extension { #name : 'DateParser' }

{ #category : '*Chronology-Compat' }
DateParser >> convertTwoDigitsYear [
	"Nothing: the century is added where a year is read from one or two
	 digits -- parseTwoDigitYear and parseVariableDigitYear -- and never
	 after a four-digit one.  See the comment above."
]

{ #category : '*Chronology-Compat' }
DateParser >> parseTwoDigitYear [
	"Two digits under a yy pattern: this century's."
	year := self currentMillenium + (self parse: 'year' expectedSize: 2)
]

{ #category : '*Chronology-Compat' }
DateParser >> parseVariableDigitYear [
	"As many digits as there are under a y pattern: this century's when
	 there were one or two of them, the year as written otherwise."
	| start |
	start := inputStream position.
	year := self parse: 'year'.
	(inputStream position - start <= 2 and: [year between: 0 and: 99])
		ifTrue: [year := self currentMillenium + year]
]

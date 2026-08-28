Extension { #name : 'Character' }

{ #category : '*Kernel-Protocol' }
Character >> displayString [
	^String with: self
]

{ #category : '*Kernel-Protocol' }
Character >> isCharacter [
	^true
]

{ #category : '*Kernel-Protocol' }
Character >> value [
	"My code point.

	 Pharo has this and so does Squeak, and without it a Character inherits
	 Object>>value -- see the note there -- which answers the RECEIVER.  So
	 `$A value' answered $A where every other dialect answers 65, and it
	 answered it silently: ported code that reads a code point this way gets
	 a Character where it expected an Integer and finds out later, in
	 arithmetic or in a comparison, rather than at the send.

	 asInteger and asciiValue answer the same number and always did.  This
	 is the third spelling because it is the one ported source uses."
	^self asInteger
]

{ #category : '*Kernel-Protocol' }
Character >> digitValue [
	"My value as a digit: 0 to 9 for $0 to $9, 10 to 35 for $A to $Z and
	 for $a to $z alike, and -1 for anything else.

	 1983 knew capitals only, so a lowercase hex digit was not a digit:
	 `16rff' read as 16 followed by something that was not a number, and
	 `'16rff' asNumber' answered 0 (Bugs3 B51).  Every reader of a radix
	 number -- Integer class>>readFrom:radix:, Number class>>readFrom: and
	 the image's own Scanner -- asks this one question, so this is the
	 one place to answer it for both cases; Pharo's digitValue answers the
	 same.  The percent-escape and JSON readers in this library already
	 uppercased before asking, and are unchanged by it."
	(value between: 48 and: 57) ifTrue: [^value - 48].
	(value between: 65 and: 90) ifTrue: [^value - 55].
	(value between: 97 and: 122) ifTrue: [^value - 87].
	^-1
]

Extension { #name : 'Character' }

{ #category : '*Streams-Protocol' }
Character >> putOn: aStream [
	aStream nextPut: self
]

{ #category : '*Streams-Protocol' }
Character class >> lf [
	"Line feed, 10.  Character cr is 13 on this system because that is what
	 the Alto meant by it, and it keeps that value."
	^Character value: 10
]

{ #category : '*Streams-Protocol' }
Character class >> nl [
	^Character value: 10
]

{ #category : '*Streams-Protocol' }
Character >> isPunctuation [
	"Whether I am one of the marks that separate words rather than spell
	 them.  The set is Pharo's."
	^'.,;:!?''"`(){}[]<>-_/\|@#$%^&*+=~' includes: self
]

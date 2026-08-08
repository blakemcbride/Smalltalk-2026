Extension { #name : 'String' }

{ #category : '*Streams-Protocol' }
String >> putOn: aStream [
	"Its characters, not its printString: << is for text a person reads."
	aStream nextPutAll: self
]

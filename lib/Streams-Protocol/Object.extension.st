"putOn: -- how an object goes onto a stream under <<."
Extension { #name : 'Object' }

{ #category : '*Streams-Protocol' }
Object >> putOn: aStream [
	"Anything with nothing better to say writes its display form."
	aStream nextPutAll: self displayString
]

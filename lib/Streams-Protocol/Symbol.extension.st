Extension { #name : 'Symbol' }

{ #category : '*Streams-Protocol' }
Symbol >> putOn: aStream [
	aStream nextPutAll: self asString
]

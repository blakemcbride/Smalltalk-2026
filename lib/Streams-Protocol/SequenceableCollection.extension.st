Extension { #name : 'SequenceableCollection' }

{ #category : '*Streams-Protocol' }
SequenceableCollection class >> streamContents: aBlock [
	"Build a collection by writing to a stream.

	 It is on SequenceableCollection class rather than String class so that
	 Array streamContents: works too, which is where the idiom earns its
	 keep for anything that is not text."
	| stream |
	stream := WriteStream on: (self new: 64).
	aBlock value: stream.
	^stream contents
]

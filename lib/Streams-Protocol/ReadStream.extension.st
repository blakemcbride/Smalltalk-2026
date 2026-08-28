"
Reading a run of elements by copying it, not by collecting it.

A ReadStream's collection IS the whole collection -- unlike a FileStream's,
which is one page of a file -- so `the next n elements' is a copyFrom:to:
of it and a move of the position, which is what Squeak's ReadStream does.
The inherited versions in PositionableStream build the answer one nextPut:
at a time into a WriteStream on `collection species new:', and for an
OrderedCollection that is an empty collection that refuses at:put:, so
upToEnd, next:, upTo: and nextLine all failed on a stream over one (Bugs3
B46).  WriteStream>>pastEndPut: now grows an OrderedCollection by addLast:,
which cures every one of those; these two are here because for the common
case they are also the right shape.  copyFrom:to: is index-aware for every
SequenceableCollection, an OrderedCollection's included, and it is one send
where the loop was one per element.
"
Extension { #name : 'ReadStream' }

{ #category : '*Streams-Protocol' }
ReadStream >> upToEnd [
	"Everything from here to the end, leaving me at the end."
	| result |
	result := collection copyFrom: position + 1 to: readLimit.
	position := readLimit.
	^result
]

{ #category : '*Streams-Protocol' }
ReadStream >> next: anInteger [
	"The next anInteger elements, or as many as are left -- short rather
	 than failing, for the reason PositionableStream>>next: gives.  A
	 request for none, or fewer, answers an empty collection of my kind
	 and moves nothing."
	| end result |
	end := (position + (anInteger max: 0)) min: readLimit.
	result := collection copyFrom: position + 1 to: end.
	position := end.
	^result
]

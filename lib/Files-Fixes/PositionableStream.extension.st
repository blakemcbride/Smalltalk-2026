"
Reading a chunk turns every line ending into the one the image uses.

This is the inbound half of what TextFileStream does outbound, and neither
is much use without the other.  Chunk format is how source arrives -- file
in, ClassCategoryReader>>scanFrom:, and every method the changes file
carries -- and the text that comes out of here is the text that is compiled
and, more to the point, the text the browser shows you afterwards.

So a chunk read from a file written with line feeds has to arrive holding
carriage returns.  Without that, filing in a file this system had itself
filed out would store method source with no line breaks in it as far as
Paragraph is concerned, and every method in the browser would display as one
line -- a fault that would look like a browser bug and would in fact be a
file being read in the convention it was written in.

The C chunk reader in src/compiler/chunk.c has done exactly this since it
was written, for the same reason and with the same rule: a carriage return
and a line feed together are one ending and not two.  This is that rule,
where the image can reach it.
"
Extension { #name : 'PositionableStream' }

{ #category : '*Files-Fixes' }
PositionableStream >> nextChunk [
	"Answer the contents of the receiver up to the next terminator, with
	 doubled terminators ignored, and with every line ending translated to
	 the carriage return the text machinery breaks on."

	| aStream char terminator cr lf |
	terminator := $!.
	cr := Character cr.
	lf := Character value: 10.
	aStream := WriteStream on: (String new: 200).
	self skipSeparators.
	[(char := self next) == nil] whileFalse: [
		char == terminator
			ifTrue: [
				(self peekFor: terminator)
					ifTrue: ["doubled terminator" aStream nextPut: char]
					ifFalse: [^aStream contents]]
			ifFalse: [
				char == cr
					ifTrue: [
						"A carriage return and a line feed are one ending."
						(self atEnd not and: [self peek == lf]) ifTrue: [self next].
						aStream nextPut: cr]
					ifFalse: [
						aStream nextPut: (char == lf ifTrue: [cr] ifFalse: [char])]]].
	^aStream contents
]

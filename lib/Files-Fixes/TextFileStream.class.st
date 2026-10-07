"
A FileStream that writes the host's line ending instead of a carriage return.

The image and the file disagree about what a line ends with, and both are
right.  Inside, a line ends with Character cr and nothing else will do:
CompositionScanner stops on carriage return alone, so a Paragraph holding
line feeds has no line breaks in it -- see FileModel, where the same fact is
what made a viewed file arrive as one line thousands of characters long.
Outside, a carriage return on its own is the Alto's convention and no system
anybody now runs reads it: a filed-out class opens in an editor, in git and
in a diff as one enormous line.

So the translation goes at the edge, and this class is the outbound half of
it.  It is a FileStream in every other respect -- timeStamp, shorten and
close all work, and position counts the bytes actually written, which is why
shorten still truncates in the right place when the ending is two bytes
long.

Only nextPut: and nextPutAll: are overridden, and between them they are the
only way a byte reaches the page: FileStream>>nextPutAll: writes Strings
straight into the page buffer without passing through nextPut:, so
overriding one of the two would translate some of the file and not the rest.
Anything that is neither String nor ByteArray goes to super, which loops
back through nextPut: here.

The inbound half is PositionableStream>>nextChunk, which turns every common
ending back into a carriage return as the text is read.  The two have to
exist together: filing out with line feeds and reading back without
translating would put line feeds into method source, and the browser would
show every method as a single line.

The price of the pair is that a line feed the image holds cannot cross it: it
goes out as a line feed and comes back as a carriage return.  For text that
is nobody's business but the writer's.  For a CHUNK -- a method's source, a
class definition, a comment, which fileIn will compile -- it changed a string
literal holding the HTTP line end into one holding a carriage return, and the
filed-in method answered something else (Bugs5 FILES-5).  So nextChunkPut:
refuses a chunk with a line feed in it, rather than writing what will not
read back.
"
Class {
	#name : 'TextFileStream',
	#superclass : 'FileStream',
	#instVars : [ 'lineEnd' ],
	#category : 'Files-Fixes'
}

{ #category : 'accessing' }
TextFileStream >> lineEnd [
	"The ending this stream writes.  Asked of the VM once and kept, because
	 it is a property of the machine and cannot change while we run."

	lineEnd isNil ifTrue: [lineEnd := FileStream nativeLineEnd].
	^lineEnd
]

{ #category : 'accessing' }
TextFileStream >> lineEnd: aString [
	"Write this ending instead of the host's own."

	lineEnd := aString
]

{ #category : 'writing' }
TextFileStream >> nextPut: aCharacter [
	"Write the character, or the host's line ending in place of a carriage
	 return."

	aCharacter == Character cr ifTrue: [
		super nextPutAll: self lineEnd.
		^aCharacter].
	^super nextPut: aCharacter
]

{ #category : 'writing' }
TextFileStream >> nextPutAll: aCollection [
	"Write the collection with every carriage return replaced by the host's
	 line ending.

	 Written as runs between the returns rather than a character at a time,
	 because FileStream>>nextPutAll: copies a String into the page in a
	 block and going through nextPut: for every character of a 46K file-out
	 is the difference between one operation and forty-six thousand."

	| cr size start index ending |
	(aCollection isKindOf: String) ifFalse: [^super nextPutAll: aCollection].
	cr := Character cr.
	ending := self lineEnd.
	(ending size = 1 and: [ending first == cr])
		ifTrue: [^super nextPutAll: aCollection].
	size := aCollection size.
	start := 1.
	index := 1.
	[index <= size] whileTrue: [
		(aCollection at: index) == cr
			ifTrue: [
				index > start ifTrue: [
					super nextPutAll: (aCollection copyFrom: start to: index - 1)].
				super nextPutAll: ending.
				start := index + 1].
		index := index + 1].
	start <= size ifTrue: [
		super nextPutAll: (aCollection copyFrom: start to: size)].
	^aCollection
]

{ #category : 'writing' }
TextFileStream >> nextChunkPut: aString [
	"Write aString as a chunk, as WriteStream does -- or refuse, writing
	 nothing, if it holds a line feed: nextChunk reads every line feed
	 back as a carriage return, so the chunk filed in would not be the
	 chunk filed out (Bugs5 FILES-5; see this class's comment).  A line
	 feed in a method's source can only be inside a literal or a comment,
	 and `Character value: 10', built into the String when the method
	 runs, says the same thing in a form that survives."

	(aString asString includes: (Character value: 10)) ifTrue: [
		^Error new signal: 'this chunk holds a line feed, which fileIn would',
			' read back as a carriage return: ',
			(aString asString copyFrom: 1 to: (aString size min: 40)) printString,
			'.  Build the String when the method runs -- String with:',
			' Character cr with: (Character value: 10) -- instead of writing',
			' the character inside a literal.'].
	^super nextChunkPut: aString
]

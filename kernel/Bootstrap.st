"Smalltalk-2026 bootstrap additions.
Copyright (c) 2026 Blake McBride.  All rights reserved.

A handful of methods an image built from sources needs and a 1983 image
never did, because a 1983 image was built from another 1983 image and
inherited the answers.

This is our own code, kept apart from the vendored MIT sources in sources/
so that neither is confused for the other.  It is filed in last."!

"----------  Sources without files  ----------"!

"Smalltalk-80 keeps no source in the image: a CompiledMethod carries a
position into a sources file and the Browser reads the chunk there.  This
image has no file system, so its sources are a String and SourceFiles holds
streams on it -- which works because RemoteString asks only for position:
and nextChunk.  What such a stream does not have is FileStream's mode
protocol, and putSource: sets a mode before writing."!

!PositionableStream methodsFor: 'bootstrap support'!
readWriteShorten
	"On a file this asks that the file be truncated where writing stops.
	These sources are a String; there is no file and nothing to truncate."!
readWrite
	"As readWriteShorten: nothing to do for a stream on a collection."!
readOnly
	"As readWriteShorten: nothing to do for a stream on a collection."! !

"----------  A method the 1983 sources send and never define  ----------"!

"CharacterBlock class>>stringIndex:character:boundingRectangle: sends
newStringIndex:Character:BoundingRectangle: to the instance it has just made.
No such method exists.  Searching the whole of sources/ for
'BoundingRectangle:' finds this one send and no definition anywhere, so the
gap is in the 1983 sources rather than in how they were read.

It is reached the first time a TextView displays: View>>controller makes a
ParagraphEditor, whose resetState builds a CharacterBlock this way.  The
sibling that does exist takes the rectangle already taken apart, so this is
that method with the two halves put back together."!

!CharacterBlock methodsFor: 'bootstrap support'!
newStringIndex: anInteger Character: aCharacter BoundingRectangle: aRectangle
	stringIndex _ anInteger.
	character _ aCharacter.
	super origin: aRectangle origin.
	super extent: aRectangle extent! !

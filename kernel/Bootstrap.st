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

"
setToEnd on a ReadWriteStream goes to the end of what was WRITTEN.

1983's PositionableStream>>setToEnd sets the position to readLimit, and a
ReadWriteStream raises readLimit only when contents is asked for; a stream
that has been written to and never read answers setToEnd by going back to
where it was before the writes.  A WriteStream never noticed, because nothing
sends it setToEnd.  A FileStream never noticed, because it keeps its end on
disk.

The changes stream of a bootstrap that is not writing an image is neither: it
is a ReadWriteStream on a String, because there is no image to name a changes
file after and CompiledMethod>>getSource only needs position: and nextChunk.
And putSource:class:category:inFile: begins with `file setToEnd'.  So every
method compiled in a -bootstrap -tests or -eval run was written at the same
place, each on top of the last, and every method's source pointer pointed
there: `sourceCodeAt:' answered the LAST method compiled whatever selector it
was asked for.  The Tonel writer found it -- a class written out had one
method's body under every pattern -- and it had been true of every test run
since the bootstrap was written.

This is what contents does to readLimit, done where setToEnd needs it.
"
Extension { #name : 'ReadWriteStream' }

{ #category : '*Streams-Protocol' }
ReadWriteStream >> setToEnd [
	"Set the position to the end of everything written or read so far."
	readLimit := readLimit max: position.
	position := readLimit
]

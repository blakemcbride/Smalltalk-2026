"
Reading and writing a RemoteString under the source-file lock.

A RemoteString is a position in one of SourceFiles, and reading one moves
that shared stream there and reads a chunk; writing one moves it to the end.
Another process doing either in between left a reader with somebody else's
text, or a writer's chunk in the middle of another's (Bugs5 FILES-2).  See
LibraryLocks>>holdingSources:.
"
Extension { #name : 'RemoteString' }

{ #category : '*Files-Fixes' }
RemoteString >> string [
	"Answer the receiver's string if remote files are enabled."
	| theFile |
	(SourceFiles == nil or: [sourceFileNumber == nil]) ifTrue: [^''].
	^LibraryLocks holdingSources: [
		theFile := SourceFiles at: sourceFileNumber.
		theFile position: (filePositionHi bitShift: 8) + filePositionLo.
		theFile nextChunk]
]

{ #category : '*Files-Fixes' }
RemoteString >> verbatimString [
	"Answer the receiver's string as it was written, line feeds and all,
	 where string turns every line ending into a carriage return.

	 For a method's source -- CompiledMethod>>getSource -- which the image
	 wrote itself, in its own convention, and which may hold a line feed
	 only because the method does: inside a string literal, the HTTP line
	 end.  Through string that line feed came back a carriage return, so
	 the Browser showed a different literal, a reshape recompiled the
	 method from the changed text, and a write-back or a fileOut wrote it
	 (Bugs5 FILES-5).  A class comment still reads through string: the
	 bootstrap stores comments with the line feeds of the file they came
	 from, and the translation is what turns those into lines."
	| theFile |
	(SourceFiles == nil or: [sourceFileNumber == nil]) ifTrue: [^''].
	^LibraryLocks holdingSources: [
		theFile := SourceFiles at: sourceFileNumber.
		theFile position: (filePositionHi bitShift: 8) + filePositionLo.
		theFile nextChunkVerbatim]
]

{ #category : '*Files-Fixes' }
RemoteString >> string: aString onFileNumber: anInteger [
	"Store this as my string if source files exist."
	| theFile |
	SourceFiles == nil ifTrue: [^self].
	LibraryLocks holdingSources: [
		theFile := SourceFiles at: anInteger.
		theFile setToEnd; readWriteShorten; cr.
		self string: aString onFileNumber: anInteger toFile: theFile.
		theFile readOnly]
]

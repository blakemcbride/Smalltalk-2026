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

"
File out a protocol or a single method in the host's line ending.

Both of these are the 1983 methods with `Disk file:' replaced by
`Disk textFile:'.  See TextFileStream for why.  And both write through
FileDirectory>>replaceTextFile:using:, so that one stopped partway leaves an
earlier file of the same name as it was (Bugs07 COMP-2).
"
Extension { #name : 'ClassDescription' }

{ #category : '*Files-Fixes' }
ClassDescription >> fileOutCategory: aString [
	"Create a file whose name is the name of the receiver with -.st- as the
	 extension, and file a description of the receiver's category aString
	 onto it."

	| fileName |
	fileName := Disk checkName: self name , '-' , aString , '.st' fixErrors: true.
	Disk replaceTextFile: fileName using:
		[:fileStream |
		 fileStream timeStamp.
		 self fileOutCategory: aString on: fileStream moveSource: false toFile: 0]
]

{ #category : '*Files-Fixes' }
ClassDescription >> fileOutMessage: aString fileName: fileName [
	"Create a local file named fileName and file a description of the
	 receiver's message aString onto it."

	Disk replaceTextFile: fileName using:
		[:fileStream |
		 fileStream timeStamp.
		 self fileOutMessage: aString on: fileStream moveSource: false toFile: 0]
]

{ #category : '*Files-Fixes' }
ClassDescription >> logOrganizationChange [
	"Record that the receiver is being reorganized on the changes file --
	 1983's, holding the source-file lock (Bugs5 FILES-2)."
	| file |
	SourceFiles == nil ifTrue: [^self].
	LibraryLocks holdingSources: [
		file := SourceFiles at: 2.
		file setToEnd; readWriteShorten.
		file cr; nextChunkPut:
			self name, ' organization changeFromString: ',
				self organization printString storeString.
		file cr; readOnly]
]

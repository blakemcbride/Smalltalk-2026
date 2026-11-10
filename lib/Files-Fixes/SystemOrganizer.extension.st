"
File out a whole system category in the host's line ending.
"
Extension { #name : 'SystemOrganizer' }

{ #category : '*Files-Fixes' }
SystemOrganizer >> fileOutCategory: category [
	"File out every class in the category.  The 1983 method with
	 `Disk file:' replaced by `Disk textFile:'; see TextFileStream.  And
	 written through FileDirectory>>replaceTextFile:using:, so that a file
	 out stopped partway by any one class leaves an earlier file of the
	 same name as it was (Bugs07 COMP-2)."

	Disk replaceTextFile: (category , '.st.') asFileName using:
		[:aFileStream | self fileOutCategory: category on: aFileStream]
]

{ #category : '*Files-Fixes' }
SystemOrganizer >> changeFromString: aString [
	"Intercept to notify the system change set of the system
	 reorganization -- 1983's, writing the changes file under the
	 source-file lock (Bugs5 FILES-2)."
	| file |
	SourceFiles == nil ifFalse: [
		LibraryLocks holdingSources: [
			file := SourceFiles at: 2.
			file setToEnd; readWriteShorten.
			file cr; nextChunkPut: 'SystemOrganization changeFromString: ',
				SystemOrganization printString storeString.
			file cr; readOnly]].
	Smalltalk changes reorganizeSystem.
	super changeFromString: aString
]

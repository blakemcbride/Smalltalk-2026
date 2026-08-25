"
A directory can hold a directory, and 1983's protocol cannot say so.

FileDirectory class>>directoryFromName:setFileName: answered `Disk' for every
designator it was given and handed the whole string back as the local name:

	directory _ Disk.
	stream _ ReadStream on: fileDesignator.
	localNameBlock value: (stream upTo: nil).
	^directory

One directory, the one the system was started in, the way the Alto had one
disk.  The file system underneath is not that shape -- a readdir walk of a
source tree turns up src, doc, lib and tests alongside the files -- and the
whole of the difference shows up at that method: with every designator
resolving to the same place, `doc/BUILD.md' is a file name that cannot be
opened, and FileStream>>oldFileNamed: answers `file not found' for a file
that is plainly there.

So the designator is split at its last separator, which is the one piece of
path syntax this needs to know, and the part in front names the directory.
A designator with no separator in it takes the old path through this method
exactly as before: Disk, and the whole string as the name.  Every 1983
caller passes one of those.
"
Extension { #name : 'FileDirectory' }

{ #category : '*Files-Fixes' }
FileDirectory class >> directoryFromName: fileDesignator setFileName: localNameBlock [
	"Answer the file directory the designator implies, and evaluate the block
	 with the rest of the designator -- the name within that directory."

	| cut |
	cut := 0.
	1 to: fileDesignator size do: [:i |
		(fileDesignator at: i) = $/ ifTrue: [cut := i]].
	cut = 0 ifTrue: [
		localNameBlock value: fileDesignator.
		^Disk].
	localNameBlock value: (fileDesignator copyFrom: cut + 1 to: fileDesignator size).
	^Disk directoryNamed: (fileDesignator copyFrom: 1 to: cut - 1)
]

{ #category : '*Files-Fixes' }
FileDirectory >> directoryNamed: aString [
	"Answer the directory of that name within me.  A file system with no
	 directories inside a directory has only itself to answer."

	^self
]

{ #category : '*Files-Fixes' }
FileDirectory >> isDirectoryNamed: aString [
	"Answer whether the name denotes a directory rather than a file.  A file
	 system with no directories inside a directory answers false to all of
	 them, which is 1983's world and the safe answer everywhere else."

	^false
]

{ #category : '*Files-Fixes' }
FileDirectory >> textFile: aFileName [
	"Answer a stream on an old or new file of that name, which writes the
	 host's line ending where the image holds a carriage return.  `file:'
	 with the translation on, and what every file out uses."

	^(self findOrAddKey: aFileName) asTextFileStream
]

{ #category : '*Files-Fixes' }
FileDirectory class >> addExternalReference: anElement [
	"Remember an open directory, so a snapshot can release it.  Under
	 LibraryLocks' files lock: the list is one OrderedCollection for the
	 image, and open and close from two workers at once were an add and a
	 remove on it with nothing between them."
	LibraryLocks holdingFiles: [
		(ExternalReferences includes: anElement)
			ifFalse: [ExternalReferences addLast: anElement]]
]

{ #category : '*Files-Fixes' }
FileDirectory class >> removeExternalReference: anElement [
	LibraryLocks holdingFiles: [ExternalReferences remove: anElement ifAbsent: []]
]

{ #category : '*Files-Fixes' }
FileDirectory class >> releaseExternalReferences [
	"Release every open directory.  Taken off the list under the lock and
	 released outside it, because a release may close and closing takes
	 the lock."
	| taken |
	taken := LibraryLocks holdingFiles: [ | all |
		all := ExternalReferences copy.
		ExternalReferences := OrderedCollection new.
		all].
	taken reverseDo: [:each | each release]
]

{ #category : '*Files-Fixes' }
FileDirectory >> open [
	"Open the directory."
	closed := false.
	self class addExternalReference: self
]

{ #category : '*Files-Fixes' }
FileDirectory >> close [
	"Close the directory."
	closed ifFalse: [
		self class removeExternalReference: self.
		self release]
]

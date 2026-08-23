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

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

A designator is also written the way a person writes one at a shell prompt,
because the File List's top pane is a place a person types a path: a leading
`~' is the home directory, and `.' and `..' are the directory you are in and
the one above it.  The tilde is expanded here rather than in the File List so
that every designator gets it -- a tilde handed to FileStream>>oldFileNamed:
is the same request as one typed into the pattern pane -- and because two
spellings of one name that resolve differently is the kind of disagreement
nothing ever reports.

The dots need no expanding: the file system resolves them itself, and a
readdir of `doc/../lib' walks lib.  What they need is somewhere to be typed,
which is FileList's half of this.

And the last separator is not always the one that divides.  `/tmp' cuts at
position 1 and leaves nothing in front of it; nothing is the empty
directoryName, which is where the system was started, so `/tmp' asked for a
file named tmp in the current directory.  The one character before position 1
is the root, and that is what the cut answers there.
"
Extension { #name : 'FileDirectory' }

{ #category : '*Files-Fixes' }
FileDirectory class >> directoryFromName: fileDesignator setFileName: localNameBlock [
	"Answer the file directory the designator implies, and evaluate the block
	 with the rest of the designator -- the name within that directory."

	| name cut |
	name := self expandTilde: fileDesignator.
	cut := 0.
	1 to: name size do: [:i |
		(name at: i) = $/ ifTrue: [cut := i]].
	cut = 0 ifTrue: [
		localNameBlock value: name.
		^Disk].
	localNameBlock value: (name copyFrom: cut + 1 to: name size).
	cut = 1 ifTrue: [^Disk directoryNamed: '/'].
	^Disk directoryNamed: (name copyFrom: 1 to: cut - 1)
]

{ #category : '*Files-Fixes' }
FileDirectory class >> expandTilde: aDesignator [
	"The designator with a leading `~' made into the home directory, the way a
	 shell expands one before the command ever sees it.

	 Only a `~' that is the whole designator or is followed by a separator,
	 because `~scratch' is a legal file name here and a shell's `~user' form
	 would need a password file to answer.  A designator with no tilde in
	 front is answered as it stands, which is every name this system builds
	 for itself -- so nothing asks the environment during a bootstrap, where
	 Smalltalk>>environmentAt: has not been loaded yet."

	| home |
	aDesignator isEmpty ifTrue: [^aDesignator].
	(aDesignator at: 1) = $~ ifFalse: [^aDesignator].
	(aDesignator size = 1 or: [(aDesignator at: 2) = $/]) ifFalse: [^aDesignator].
	home := self homeDirectoryName.
	home isNil ifTrue: [^aDesignator].
	^home , (aDesignator copyFrom: 2 to: aDesignator size)
]

{ #category : '*Files-Fixes' }
FileDirectory class >> homeDirectoryName [
	"Where the user's files are, read from where the shell put it: HOME, and
	 USERPROFILE for a Windows that has no HOME.  Nil when neither is set,
	 which leaves a `~' standing as the file name it also is."

	| home |
	home := Smalltalk environmentAt: 'HOME'.
	(home isNil or: [home isEmpty])
		ifTrue: [home := Smalltalk environmentAt: 'USERPROFILE'].
	(home isNil or: [home isEmpty]) ifTrue: [^nil].
	[home size > 1 and: [home last = $/]]
		whileTrue: [home := home copyFrom: 1 to: home size - 1].
	^home
]

{ #category : '*Files-Fixes' }
FileDirectory class >> withoutDotSegments: aDesignator [
	"The designator with its `.' and `..' segments taken out by arithmetic on
	 the text: `doc/../lib' is `lib' and `/home/blake/..' is `/home'.

	 The file system does not need this -- it resolves the dots itself -- and
	 the File List does, because the pattern pane is the display of where you
	 are as well as the control that takes you there.  Going up four times
	 without it reads `../../../../*', and every name in the list carries the
	 same string in front of it.

	 A leading `..' has nowhere to go and stays: nothing here knows what
	 directory the system was started in, so `..' cannot be turned into a
	 name.  Above the root there is nothing, so `/..' is `/'.  Answering `.'
	 for a designator that cancels out entirely -- `doc/..' -- keeps the
	 result a designator rather than the empty string, which names nothing.

	 This is the logical answer, the one a shell's `cd ..' gives: a symbolic
	 link followed by `..' goes back to where the name was written rather
	 than to where the link pointed."

	| absolute out stream segment result |
	absolute := aDesignator notEmpty and: [(aDesignator at: 1) = $/].
	out := OrderedCollection new.
	stream := ReadStream on: aDesignator.
	[stream atEnd] whileFalse: [
		segment := stream upTo: $/.
		(segment isEmpty or: [segment = '.']) ifFalse: [
			segment = '..'
				ifTrue: [
					(out notEmpty and: [out last ~= '..'])
						ifTrue: [out removeLast]
						ifFalse: [absolute ifFalse: [out addLast: segment]]]
				ifFalse: [out addLast: segment]]].
	out isEmpty ifTrue: [^absolute ifTrue: ['/'] ifFalse: ['.']].
	result := WriteStream on: (String new: aDesignator size).
	absolute ifTrue: [result nextPut: $/].
	out do: [:each | result nextPutAll: each] separatedBy: [result nextPut: $/].
	^result contents
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

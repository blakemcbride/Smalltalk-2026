"
Where a POSIX directory answers for a place other than the one the system
was started in.

`directoryName' is 1983's own instance variable and PosixFileDirectory class
>>new sets it to the empty string, which is where a relative name resolves.
Nothing else ever read it.  Reading it is the whole of this: an instance
whose directoryName is `doc' walks doc, names its files doc/..., and
answers doc/phases when asked about phases.

Every name a directory hands out is therefore a designator that can be
opened as it stands, which is what keeps FileDirectory>>find:ifAbsentDo:
honest -- it compares the name it was asked for against the names it
enumerates, and the two have to be written the same way.
"
Extension { #name : 'PosixFileDirectory' }

{ #category : '*Files-Fixes' }
PosixFileDirectory >> directoryNamed: aString [
	"Answer a directory on the named place within me."

	^self class new setDirectory: nil directoryName: (self pathFor: aString)
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> fileNames [
	"Command 3 of primitive 131, walking my own place.  Nil is the directory
	 the system was started in, which is what an empty directoryName means."

	^self
		doPrimitive: 3
		arg1: ((directoryName isNil or: [directoryName isEmpty])
			ifTrue: [nil]
			ifFalse: [directoryName])
		arg2: nil
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> initFileName: aFileName [
	"A file of mine is named the way it must be opened: from where the system
	 was started, not from where I am."

	^super initFileName: (self pathFor: aFileName)
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> isDirectoryNamed: aString [
	"Ask the file system, which is the only one that knows.

	 Command 4 of primitive 131, beside the three 1983 asked for.  Tested
	 against true rather than for falsity, because a primitive that is not
	 there at all falls through to an empty body and answers the receiver."

	^(self doPrimitive: 4 arg1: (self pathFor: aString) arg2: nil) == true
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> pathFor: aFileName [
	"The name, written from where the system was started."

	^(directoryName isNil or: [directoryName isEmpty])
		ifTrue: [aFileName]
		ifFalse: [directoryName, '/', aFileName]
]

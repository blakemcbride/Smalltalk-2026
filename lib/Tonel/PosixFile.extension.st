"
A file's modification time: command 6 of primitive 130, beside 1983's five.
"
Extension { #name : 'PosixFile' }

{ #category : '*Tonel' }
PosixFile >> modificationTime [
	"When the file was last written, in seconds on the Smalltalk epoch --
	 what Time totalSeconds counts in.  A server that loads a class from a
	 file reloads it when this changes."

	^self
		doCommand: 6
		name: nil
		page: nil
		error: nil
]

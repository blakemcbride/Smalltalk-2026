"
A file's modification time: command 6 of primitive 130, beside 1983's five.
"
Extension { #name : 'PosixFile' }

{ #category : '*Tonel' }
PosixFile >> modificationTime [
	"When the file was last written, in seconds on the Smalltalk epoch --
	 UTC, which is what Time utcSeconds counts in and NOT what
	 Time totalSeconds does: that one is corrected for the host's time
	 zone (Bugs4 CHRON-3), so the two are hours apart wherever the host is
	 not on GMT.  Nothing here compares the two -- a reload compares this
	 against the last reading of itself -- and the comment used to name
	 the wrong one.  A server that loads a class from a file reloads it
	 when this changes."

	^self
		doCommand: 6
		name: nil
		page: nil
		error: nil
]

"
File out through a stream that writes the host's line ending.
"
Extension { #name : 'Class' }

{ #category : '*Files-Fixes' }
Class >> fileOut [
	"Create a file whose name is the name of the receiver with -.st- as the
	 extension, and file a description of the receiver onto it.

	 1983 opened it with `Disk file:', which writes the carriage returns the
	 image holds and nothing else.  `Disk textFile:' is the same file with
	 the ending translated on the way out.

	 And it writes through FileDirectory>>replaceTextFile:using: (Bugs07
	 COMP-2), so that a file out that stops partway -- a chunk the text
	 stream refuses -- leaves an earlier file of the same name as it was
	 and no stream open, where it left the new first page in front of the
	 old file's remaining bytes.  The class is taken out of the change set
	 only once its file is written.  Everything else here is the
	 original."

	Transcript refresh; cr; cr; show: 'Filing out class:'.
	Disk replaceTextFile: self name , '.st' using:
		[:fileStream |
		 fileStream timeStamp.
		 self fileOutOn: fileStream moveSource: false toFile: 0].
	self removeFromChanges
]

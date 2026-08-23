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
	 the ending translated on the way out.  Everything else here is the
	 original."

	| fileStream |
	Transcript refresh; cr; cr; show: 'Filing out class:'.
	fileStream := Disk textFile: self name , '.st'.
	fileStream timeStamp.
	self fileOutOn: fileStream moveSource: false toFile: 0.
	fileStream shorten; close.
	self removeFromChanges
]

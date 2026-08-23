"
A file opened as text: written in the host's line ending, not the Alto's.
"
Extension { #name : 'File' }

{ #category : '*Files-Fixes' }
File >> asTextFileStream [
	"Answer a TextFileStream on the first page of the receiver.  As
	 asFileStream, except that carriage returns written to it reach the disk
	 as the host's own line ending."

	self open.
	^TextFileStream on: (self readOrAddPageNumber: 1)
]

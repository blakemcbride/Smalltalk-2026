"
A page handed to a binary stream holds nothing past its data.

A binary FileStream works on its page's own buffer -- 1983 wrote it that way
to save a copy, where a text stream is given a fresh String -- and the
buffer is one object for the life of the stream: every page turn reads or
renumbers the same page into it.  So what lies past the data is whatever was
in the buffer before, and a writer positioned past the end of the file writes
it, as though it were the file's (Bugs07 FILES-3).
"
Extension { #name : 'FilePage' }

{ #category : '*Files-Fixes' }
FilePage >> asFileStream: aStream [
	"Initialize aStream (typically a FileStream) to access the data portion
	 of the receiver -- having cleared, when I am binary, whatever my buffer
	 holds past my data (Bugs07 FILES-3).

	 1983's own method, with that one loop put in front of the binary case.
	 A binary stream's collection IS my buffer, read up to my size and
	 written as far as its position.  Past my size the buffer held the
	 tail of the page that was in it before: a FileStream turns pages by
	 reading or renumbering this same object (readPageNumber:,
	 readOrAddPageNumber:), primitive 130 copies in only the bytes it read,
	 so a short last page leaves the rest as it was; File>>readOrAdd:
	 answers a page past the end without reading anything into it; and
	 PosixFile>>read: answers a page the file claims but the disk does not
	 have as empty, again without touching it.  Nothing read those bytes --
	 the stream's readLimit stops at my size -- until a writer was
	 positioned past the end: position: or skip: within the page, or
	 FileStream>>read: filling up the last page when positioning past it,
	 raises my end over them, and they were written to the file.  So
	 `position: 700' on a 600-byte file and one byte written put a hundred
	 bytes of the file's first page at 601, and on a file of two full
	 pages `position: 1100' put seventy-six of page one in front of the
	 byte at 1101.  A gap made by positioning has to read as zeros, as
	 File>>readOrAdd: says and does for the pages it writes between; a
	 text stream's fresh String already gives it them.

	 Here, and not in the primitive, because two of those three never
	 reach the primitive, and every page a stream works on comes through
	 this method: FileStream>>on:, read: and setToEnd all hand their page
	 over with it.  A page that is full has nothing past its data, so the
	 loop costs nothing on any page but the last."
	| offset len sourceData destData |
	len := self size.
	offset := self headerSize.
	sourceData := self dataString.
	binary ifTrue: [
		offset + len + 1 to: (offset + self dataSize min: sourceData size) do: [:index |
			sourceData at: index put: 0].
		^aStream on: sourceData from: offset + 1 to: offset + len].
	destData := String new: self dataSize.
	destData replaceFrom: 1 to: len withByteArray: sourceData startingAt: offset + 1.
	^aStream on: destData from: 1 to: len
]

{ #category : '*Files-Fixes' }
FilePage >> writeTurning [
	"Write me, as write does, for a stream that is leaving me for the page
	 after me (File>>writeTurning:, Bugs07 FILES-8)."
	^file writeTurning: self
]

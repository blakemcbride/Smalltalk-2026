"
A file opened as text: written in the host's line ending, not the Alto's, and
the three numbers that say how a file may be used.

Those numbers are meant to be one bit each and one of them is not, which is why
no file this system wrote was ever shortened.  See File class>>initialize.
"
Extension { #name : 'File' }

{ #category : '*Files-Fixes' }
File >> asTextFileStream [
	"Answer a TextFileStream on the first page of the receiver.  As
	 asFileStream, except that carriage returns written to it reach the disk
	 as the host's own line ending."

	| stream |
	self open.
	stream := TextFileStream on: (self readOrAddPageNumber: 1).
	self isReadOnly ifTrue: [stream readOnly].
	^stream
]

{ #category : '*Files-Fixes' }
File class >> initialize [
	"The file modes are bits, and Shorten has to be one.

	 1983's own initializer is

	    FilePool at: #Read put: 1.
	    FilePool at: #Write put: 2.
	    FilePool at: #Shorten put: 3

	 and every place that reads them treats them as a bit each:
	 FileStream>>writing is `(rwmode bitAnd: Write) = Write' and
	 FileStream>>close is `(rwmode bitAnd: Shorten) = Shorten'.  Three is
	 not a bit; it is Read and Write together, so the mode word and the
	 tests on it do not describe the same thing:

	    readOnly           1   writing no        shorten no
	    readWrite          3   writing YES       shorten YES   <- must not
	    readWriteShorten   6   writing YES       shorten no    <- must
	    writeShorten       5   writing NO        shorten no    <- both wrong

	 The one that matters is the third line, because it is the default:
	 FileStream>>nextPutAll: sets readWriteShorten when no mode was asked
	 for, and every write in this system arrives that way.  So close took
	 the flush branch instead of the shorten branch and no file written
	 through a stream was ever truncated -- writing a short text over a
	 longer file left the tail of the old one behind it:

	    zzfoo.txt held `-new file-aaa'; the File List put `bbb'
	    and the file on disk read `bbbw file-aaa'

	 which is what a File List that saves the first time and appears not to
	 the second time actually is.  It is not the editor and it is not
	 FileModel: acceptText:from: wrote the five bytes it was given and
	 close declined to cut the file to five bytes.

	 Four makes each mode a set of bits and every test above answer what its
	 name says.  Nothing reads these three for anything but bitAnd:, and
	 File>>readWrite -- the instance variable of the same idea -- is never
	 tested at all, so this is the whole of the change."

	FilePool at: #Read put: 1.
	FilePool at: #Write put: 2.
	FilePool at: #Shorten put: 4
]

{ #category : '*Files-Fixes' }
File >> isReadOnly [
	"Whether I may only be read.  A file that cannot say answers false,
	 which is what 1983 assumed of every file; PosixFile asks the VM."
	^false
]

{ #category : '*Files-Fixes' }
File >> asFileStream [
	"Answer a FileStream on my first page -- opened read-only when I am
	 (Bugs3 B58).

	 A FileStream's mode is nil until something asks, and the first thing
	 that asks is close, which then takes the default readWriteShorten and
	 FLUSHES: `(FileStream oldFileNamed: '/etc/hostname') close' was
	 `write:, Bad file descriptor' on a file nobody had written.  A stream
	 told it is read-only from the start does not flush at close, and a
	 write to it is refused where it is attempted, with `no writing
	 allowed', which is the sentence 1983 wrote for exactly this."
	| stream |
	self open.
	stream := FileStream on: (self readOrAddPageNumber: 1).
	self isReadOnly ifTrue: [stream readOnly].
	^stream
]

{ #category : '*Files-Fixes' }
File >> readOrAdd: aFilePage [
	"Read aFilePage if it exists, or extend me so that it does, and answer
	 it empty.

	 1983's own method, with its loop repaired.  Positioning a writable
	 stream past the end of its file arrives here with a page number past
	 the last, and every page between has to be written full first, so
	 that the file has a page where the stream is about to write.  1983
	 wrote that loop as

	     (lastPageNumber + 1 to: pageNumber - 1)
	         do: [filePage pageNumber: pageNumber. ...]

	 -- a block with NO argument handed to Interval>>do:, which raises
	 `The block needs more or fewer arguments defined' the moment there is
	 an intermediate page, and a body that numbered the page it wrote as
	 the TARGET rather than the intermediate.  A stream positioned one
	 page past its end never runs the loop, which is why `position: 600'
	 on an empty file worked and `position: 2000' raised and lost the
	 write (Bugs3 B58).

	 The intermediate pages are written full of zeros: the page handed in
	 is the stream's own, whose buffer still holds whatever was last read
	 or written through it, and a file extended by positioning should read
	 as zeros up to where the writing starts, as it does on every system
	 that has a file.  Write: answers a fresh empty page after a full last
	 one, which is what the loop carries forward, and lastPageNumber
	 advances inside it."
	| pageNumber filePage |
	filePage := aFilePage.
	pageNumber := filePage pageNumber.
	pageNumber <= lastPageNumber ifTrue: [^self read: filePage].
	lastPageNumber + 1 to: pageNumber - 1 do: [:intermediate |
		filePage pageNumber: intermediate.
		filePage page atAllPut: 0.
		filePage size: filePage dataSize.
		filePage := self write: filePage].
	filePage pageNumber: pageNumber.
	filePage size: 0.
	^filePage
]

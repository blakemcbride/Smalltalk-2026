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

	self open.
	^TextFileStream on: (self readOrAddPageNumber: 1)
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

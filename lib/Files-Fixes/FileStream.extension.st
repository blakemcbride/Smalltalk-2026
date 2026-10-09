"
What line ending this host writes, and a file opened to write it; and the
end of a file, met the way a ReadStream meets the end of its collection.

A FileStream and a ReadStream disagreed about everything past the end
(Bugs3 B51): next: past it raised where the other answered short, skip:
backwards raised where the other clamped, and position: past it was taken
by one and refused by the other.  Each of the three is answered below in
the ReadStream's way, with one deliberate difference kept for a stream
that can write: positioning a writer past its end is how a file grows.

And a stream that was only read leaves its file as it found it.  1983's
writing gave a stream with no mode the default readWriteShorten the first
time anybody asked, and close asks; so once Shorten was made a real bit (see
File class>>initialize), opening an old file, reading part of it and closing
it cut the file off at the read position (Bugs5 FILES-1).  writing and close
are answered below so that only a write sets the mode, which nextPut: and
nextPutAll: already do for themselves.

And the end of a file is where the file ends, not where this stream last
saw it end (Bugs6 FILES-4).  1983's setToEnd positioned at the File's own
cached last page, and the cache was filled when the stream was opened; so
two streams appending to one file -- eight request handlers logging, each
through a stream of its own -- each put its page back over the other's
lines, and close cut the file at its own position.  setToEnd below asks the
file system for the end and re-reads the last page, and the VM's page write
puts new bytes at the end the kernel has (src/interp/prim.c, fd_believed),
so appends from any number of streams land one after another.
"
Extension { #name : 'FileStream' }

{ #category : '*Files-Fixes' }
FileStream class >> nativeLineEnd [
	"The line ending native to the machine this is running on: a line feed
	 everywhere except Windows, which takes a carriage return and a line
	 feed.

	 A primitive because the image has no other way to find out, and asked
	 at RUN time rather than baked in at bootstrap, so that an image built
	 on one host and opened on another answers for the host it is on."

	<primitive: 254>
	^String with: (Character value: 10)
]

{ #category : '*Files-Fixes' }
FileStream >> next: anInteger [
	"The next anInteger characters, or as many as are left.

	 1983's own loop, with its end handled as a ReadStream handles it.
	 The original allocated the answer at full size and, on reaching the
	 end of the file, filled the rest with `self next' -- which is nil
	 past the end -- so asking a three-byte file for ten characters failed
	 inside at:put: with `Strings only store Characters', where
	 `(ReadStream on: 'abc') next: 10' answers 'abc' (Bugs3 B51).  A short
	 answer is one every caller can check; the error was one none could.

	 The rest is kept as written because it is the fast path: whole runs
	 are moved with replaceFrom:to:with:startingAt:, and the page is
	 turned by atEnd, which reads the next one when this one is used up."
	| newCollection howManyRead increment |
	newCollection := collection species new: anInteger.
	howManyRead := 0.
	[howManyRead < anInteger] whileTrue: [
		self atEnd ifTrue: [^newCollection copyFrom: 1 to: howManyRead].
		increment := (readLimit - position) min: (anInteger - howManyRead).
		newCollection
			replaceFrom: howManyRead + 1
			to: (howManyRead := howManyRead + increment)
			with: collection
			startingAt: position + 1.
		position := position + increment].
	^newCollection
]

{ #category : '*Files-Fixes' }
FileStream >> skip: anInteger [
	"Move anInteger characters along, clamped at the beginning -- and at
	 the end too when I am read-only.

	 1983's went through positionPage:character:, which for a target
	 before the beginning asked for page zero and raised `cannot skip -5'
	 where a ReadStream clamps to 0 (lib/Streams-Protocol); a parser that
	 steps back over what it has looked at should not have to know which
	 kind of stream it was handed (Bugs3 B51).  Forward past the end is
	 clamped only when I cannot write: for a writer, a position past the
	 end is how a file is extended, and 1983 means it -- see
	 File>>readOrAdd:."
	| target |
	anInteger = 0 ifTrue: [^self].
	target := (self position + anInteger) max: 0.
	self isReadOnly ifTrue: [target := target min: self size].
	^self position: target
]

{ #category : '*Files-Fixes' }
FileStream >> position: anInteger [
	"Set my position, refusing one before the beginning, and one past the
	 end when I am read-only.

	 1983 accepted both and failed later, in the page arithmetic, with
	 `cannot read page 0' or `cannot read page 1' -- the page's number,
	 which the caller never mentioned.  A ReadStream refuses with
	 positionError, and this is the same refusal (Bugs3 B51).  A writer
	 is different: positioning it past its end is how a file is extended,
	 and the default mode IS a writer -- FileStream>>writing makes a
	 stream that was never told a mode readWriteShorten -- which is why
	 `(FileStream oldFileNamed: f) position: 99' is still accepted where
	 `readOnly; position: 99' is refused."
	anInteger < 0 ifTrue: [^self positionError].
	(self isReadOnly and: [anInteger > self size]) ifTrue: [^self positionError].
	^self position: anInteger size: 1
]

{ #category : '*Files-Fixes' }
FileStream >> isReadOnly [
	"Whether I have been told readOnly and not since told otherwise.

	 Asked of rwmode directly rather than as `self writing not', because
	 writing answers true for a stream whose mode was never set -- it may
	 still be written -- and that is not the question here."
	^rwmode notNil and: [(rwmode bitAnd: Write) = 0]
]

{ #category : '*Files-Fixes' }
FileStream >> writing [
	"Whether I may be written.  A stream that was never told a mode may.

	 Without setting the mode.  1983's version made readWriteShorten the
	 mode of any stream that asked, and close asks; once Shorten was a
	 real bit, `(FileStream oldFileNamed: f) next: 5; close' left f five
	 bytes long (Bugs5 FILES-1).  nextPut: and nextPutAll: set the default
	 mode themselves before they write, so a stream that has written has a
	 mode and one that has not does not."
	rwmode == nil ifTrue: [^true].
	^(rwmode bitAnd: Write) = Write
]

{ #category : '*Files-Fixes' }
FileStream >> close [
	"Set my file's status to closed -- flushing, or shortening it to my
	 position if my mode says so, only if I was written.

	 A stream with no mode was never written (see writing), so there is
	 nothing to flush and nothing to cut off.  Otherwise this is 1983's
	 close: a write through the default mode still shortens the file to
	 where the writing stopped, which is what saving a shorter text over
	 a longer file needs."
	closed ifFalse: [
		(rwmode notNil and: [self writing]) ifTrue: [
			(rwmode bitAnd: Shorten) = Shorten
				ifTrue: [self shorten]
				ifFalse: [self flush]].
		closed := true.
		readLimit := writeLimit := 0.
		self file close.
		FileDirectory removeExternalReference: self]
]

{ #category : '*Files-Fixes' }
FileStream >> setToEnd [
	"Set my position to the end of my file -- the end the file has NOW, on
	 the disk, which is not necessarily the end I last saw (Bugs6 FILES-4).

	 1983 wrote `self read: (self file size max: page pageNumber)', and
	 File>>size answers lastPageNumber, a number found when the file was
	 opened and advanced only by this stream's own writes.  Nothing in that
	 asks the disk, and read: does not re-read a page the stream is already
	 on.  So a stream opened while a file had one page went on believing it
	 had one page however much anybody else appended, and the eight workers
	 of the audit, each opening its own stream, calling setToEnd, writing a
	 line and closing, left eighty lines of four hundred: each stream's
	 setToEnd was the end of the page as that stream had read it, and each
	 write put the whole page back.  A stream kept open was worse: its
	 cached page was put back over everything appended since it was read.

	 So this asks for the end three ways.  Whatever I have written and not
	 yet flushed is written first, as an append (the VM puts new bytes at
	 the kernel's end), because the re-read that follows would drop it; a
	 stream that has not written has nothing to flush, and flushing a
	 read-only one would be refused.  findLastPageNumber is one stat, and
	 the last page is then read again even when it is the page I hold,
	 since its bytes on the disk may be more than mine by now; readPageNumber:
	 reads into my own page object and asFileStream: positions me on it as
	 read: would.  The position is then the end of that page, which is the
	 end of the file as it was the moment the stat was made.  Between that
	 moment and my next write someone else may append again, and that is
	 the VM's problem, not this method's: the write goes at the end the
	 kernel has then."
	| last |
	self reopen.
	(rwmode notNil and: [self writing]) ifTrue: [self flush].
	self file findLastPageNumber.
	last := self file lastPageNumber max: 1.
	(page readPageNumber: last) isNil ifTrue: [^self error: 'setToEnd'].
	page asFileStream: self.
	position := readLimit
]

{ #category : '*Files-Fixes' }
FileStream >> release [
	"Let go of my file without closing it -- what a snapshot sends to every
	 open stream, so that the image on the disk holds no open file -- having
	 first written out whatever I was given and had not yet written.

	 1983's release wrote nothing: it was the forgiving close, for a stream
	 whose file might be gone.  Sent by releaseExternalReferences it threw
	 away the page a stream had been written to and not flushed, and not
	 only in the image being saved: in the process that took the snapshot
	 and went on, the stream was now closed, its later close did nothing,
	 and its next write reopened by re-reading the page from the disk --
	 so `f nextPutAll: 'hello world'. Smalltalk snapshotAs: 'x'. f close'
	 left an empty file (Bugs6 FILES-3).  The flush is close's own, under
	 close's own condition: a stream with no mode was never written and
	 has nothing to write, and one that may not be written is not asked
	 to.  What is NOT done is close's shorten, which cuts the file at the
	 position; release is not close, and a stream overwriting the front
	 of a longer file is not finished with it."
	closed ifFalse: [
		(rwmode notNil and: [self writing]) ifTrue: [self flush].
		closed := true.
		readLimit := writeLimit := 0.
		self file release]
]

{ #category : '*Files-Fixes' }
FileStream >> next: anInteger into: aCollection [
	"Copy the next anInteger bytes from the receiver into aCollection --
	 or as many as are left, answering the collection either way.  1983
	 filled the rest with `self next', which is nil past the end, so a
	 short file died inside at:put: with `Strings only store Characters'
	 (Bugs6 FILES-10); next: was given the same end by Bugs3 B51."
	| howManyRead increment |
	howManyRead := 0.
	[howManyRead < anInteger] whileTrue: [
		self atEnd ifTrue: [^aCollection].
		increment := (readLimit - position) min: (anInteger - howManyRead).
		aCollection
			replaceFrom: howManyRead + 1
			to: (howManyRead := howManyRead + increment)
			with: collection
			startingAt: position + 1.
		position := position + increment].
	^aCollection
]

{ #category : '*Files-Fixes' }
FileStream >> nextPutAll: aCollection [
	"1983's, with nothing at all done for an empty collection.

	 A write through the default mode shortens the file at close to
	 where the writing stopped (Bugs5 FILES-1 kept that), and the mode
	 is set here, by the first write.  An empty nextPutAll: wrote no
	 byte and still set the mode, so `oldFileNamed:' then `next: 5' then
	 `nextPutAll: \'\'' then `close' cut a 33-byte file to 5 -- a program
	 joining possibly-empty strings destroyed the tail of a file it had
	 not written to (Bugs6 FIXES-4).  Nothing written, nothing set."
	| dmax howManyWritten increment collectionSize |
	((aCollection isKindOf: ByteArray) or: [aCollection isKindOf: String])
		ifFalse: [^super nextPutAll: aCollection].
	aCollection size = 0 ifTrue: [^aCollection].
	rwmode == nil
		ifTrue: [self readWriteShorten "default mode"].
	(rwmode bitAnd: Write) = 0
		ifTrue: [^self error: 'no writing allowed!'].
	closed ifTrue: [self reopen].
	howManyWritten := 0.
	collectionSize := aCollection size.
	dmax := page dataMaxEnd.
	[increment := (dmax - position) min: (collectionSize - howManyWritten).
	 collection replaceFrom: position + 1 to: (position := position + increment)
		with: aCollection startingAt: howManyWritten + 1.
	 howManyWritten := howManyWritten + increment.
	 howManyWritten < collectionSize] whileTrue: [
		self nextPage isNil ifTrue: [^self error: 'cannot write!!!'].
		position = readLimit ifTrue: [writeLimit := dmax]].
	position > writeLimit ifTrue: [writeLimit := dmax].
	^aCollection
]

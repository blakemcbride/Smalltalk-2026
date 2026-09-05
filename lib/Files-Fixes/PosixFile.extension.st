"
What the descriptor can do, asked of the primitive that opened it.

A file whose size the kernel will not tell is read until it ends.

1983 finds a file's last page by dividing its size on disk by the page size,
and the size on disk is what stat answers.  For a file on a disk that is the
truth.  For the files a modern kernel makes up as they are read -- anything
under /proc, a device, a pipe -- stat answers ZERO, because there are no
blocks anywhere to count, and 1983 concluded the file had one page.  So
`(FileStream oldFileNamed: '/proc/cpuinfo') contentsOfEntireFile' was 512
bytes of a 55,000-byte file, and atEnd was true after them (Bugs3 B58).  Not
an error: a plausible, truncated answer.

Reading until it ends assumes it ends.  /dev/zero does not, so the reading has
a ceiling and a file that reaches it is refused rather than read for ever
(Bugs4 FILES-2); and the read that finds the end says so by answering nil,
which is what File>>read: has always promised and what PosixFile>>read: did
not do (Bugs4 FILES-A).
"
Extension { #name : 'PosixFile' }

{ #category : '*Files-Fixes' }
PosixFile >> isReadOnly [
	"Whether I was opened for reading only because I could not be opened
	 for writing (Bugs3 B58).

	 1983 opens every old file for reading and writing, and the VM falls
	 back to reading alone when the file may only be read -- /etc/hostname
	 is worth opening -- but it used to fall back silently, and a stream on
	 me then flushed at close and was told `Bad file descriptor' for a
	 write it never made.  Command 7 of primitive 130 answers what the
	 fallback recorded; a File not yet opened, or one this build's
	 primitive does not know the question for, answers false, which is the
	 1983 assumption and the safe one."
	^(self doPrimCommand: 7 name: nil page: nil) == true
]

{ #category : '*Files-Fixes' }
PosixFile >> findLastPageNumber [
	"Set and answer the number of my last page.

	 From the size on disk when there is one, as 1983 does, because that is
	 one stat and no reading.  When stat says zero the file is either empty
	 or one of the kinds whose length stat cannot know, and the two cannot
	 be told apart without reading: pages are read from the first until one
	 comes back short or missing, and that one -- or the one before it, if
	 it was missing -- is the last.  An empty file costs one read of nothing
	 this way, which is the price of being right about /proc.

	 The primitive answers false for a page past the end, and read: turns
	 that into nil; a page that exists but is short is the end too, and a
	 full one means there may be more.  Page one always exists, even in an
	 empty file, which is why the loop can start there without a special
	 case -- prim.c says so in its own words.  What the walk actually tests
	 is fullness, so it is right whatever a stale lastPageNumber makes
	 read: answer for the page past the data: an empty page and a nil both
	 stop it, and both leave `number' one past the last page with data.

	 AND THE LOOP HAS A CEILING, because some of the files stat cannot
	 measure have no end at all (Bugs4 FILES-2).  /dev/zero and /dev/full
	 answer a full page at every offset for ever, so `read a page, is it
	 full, read the next' never came back: `FileStream oldFileNamed:
	 ''/dev/zero''' pinned a worker at a hundred percent of a core and
	 never returned, and a server that opens a path a client chose could
	 be made to lose every worker it has by naming that path once each.
	 The ceiling is unsizedPageLimit, and reaching it is not a truncated
	 answer but an error: a file with no last page cannot be described by
	 a lastPageNumber, and saying so is the only honest report.  A FIFO
	 never gets that far -- pread on a pipe is ESPIPE, the primitive
	 answers false, and read: answers nil on page one."
	| bytes page number limit |
	bytes := self sizeOnDisk.
	(bytes isInteger and: [bytes > 0])
		ifTrue: [^lastPageNumber := (bytes + self pageSize - 1) // self pageSize].
	page := self initPageNumber: 1.
	limit := self unsizedPageLimit.
	number := 0.
	[number := number + 1.
	 number > limit ifTrue: [^self errorNoLastPage].
	 page pageNumber: number.
	 page size: 0.
	 (self read: page) notNil and: [page isFull]] whileTrue.
	(page size = 0 and: [number > 1]) ifTrue: [number := number - 1].
	^lastPageNumber := number
]

{ #category : '*Files-Fixes' }
PosixFile >> unsizedPageLimit [
	"How many pages I will read looking for the end of a file whose size
	 stat would not tell (Bugs4 FILES-2).

	 Thirty-two megabytes.  The number has to clear every file a kernel
	 makes up as it is read, because those are the files this walk exists
	 for, and the largest of them is not small: /proc/kallsyms is twenty-one
	 megabytes on this machine and stat calls it empty.  It has to be a
	 number all the same, because the alternative is the endless devices --
	 /dev/zero, /dev/full, /dev/urandom -- which answer a full page at every
	 offset until the process is killed.  Between those two the choice is
	 arithmetic: the largest plausible synthesized file, rounded up.

	 Reaching it costs about two seconds of one worker.  That is the price
	 of the guard and it is paid only by a file that has no end, which is
	 the case the guard is for."
	^65536
]

{ #category : '*Files-Fixes' }
PosixFile >> errorNoLastPage [
	"Refuse a file that goes on for ever (Bugs4 FILES-2).

	 Separate from findLastPageNumber so that the sentence is written once
	 and reads as a sentence.  File>>error: would append the last errno,
	 and there is no errno here: nothing failed.  The file is simply not
	 the kind of thing a File can describe."
	^Error new signal: fileName , ' has no last page: it answered ',
		self unsizedPageLimit printString,
		' full pages and never ended, and the kernel will not say how',
		' long it is.  An endless device -- /dev/zero, /dev/full,',
		' /dev/urandom -- cannot be read as a file of pages.'
]

{ #category : '*Files-Fixes' }
PosixFile >> read: aPage [
	"Read aPage if it is there; answer it empty if it is a page I claim but
	 have never written; answer nil past my end; raise if the read could not
	 be attempted at all (Bugs4 FILES-A, REFLECTION-6).

	 Four outcomes, and 1983's version could tell two of them apart.
	 Command 0 of primitive 130 answers true when it filled the page, false
	 when there are no bytes at that offset -- `Past the last page is a real
	 end and still answers false', in prim.c's own words -- and fails
	 outright, leaving the empty method body to answer the receiver, when
	 there is no descriptor to read from because the file could not be
	 opened.

	 1983 wrote `(self doCommand: 0 ... error: ''read:'') ifFalse: [^nil]',
	 which gets everything but the first wrong.  Passing an error string
	 makes doCommand: RAISE on false, so the ordinary end of a file arrived
	 as an error -- and with errno untouched by a read that did not fail,
	 the error read `s6.txt read:, Success'.  The other outcome, the
	 primitive failing, left a PosixFile in front of ifFalse:, so `-o' to a
	 directory nobody may write reported `NonBooleanReceiver: a PosixFile is
	 not a Boolean' three times before the exit code that was right all
	 along.

	 AND THE THIRD OUTCOME IS THE ONE THAT MATTERS, because a page can be
	 mine and still have no bytes.  write: glues an empty page onto a full
	 last one -- `last page can't be full, so glue on another page' -- by
	 raising lastPageNumber and writing nothing, so a file whose data ends
	 exactly on a 512-byte boundary claims one more page than the disk
	 holds.  That page is where the next write goes, and everything that
	 looks for the end of a file asks for it: FileStream>>setToEnd reads
	 `self file size', File>>characterSize reads `self size'.  Answering nil
	 there turns the end of a file into `setToEnd'; answering the empty page
	 says what is true, which is that the file has that page and it has
	 nothing in it yet.  This is the whole of FILES-A: a write after
	 setToEnd on a 1024-byte file was lost, and the changes file -- appended
	 to by setToEnd after every single compile -- lands on a 512-byte
	 boundary once every 512 bytes.  In a run compiling 10 KB methods it
	 stopped the system dead at 2,253,312 bytes, which is 4401 pages and not
	 one byte more.

	 Past lastPageNumber there is no claim to honour and nil is the answer;
	 that is what findLastPageNumber's walk reads to find the end, and what
	 readOrAdd: reads before it extends the file."
	| answer |
	answer := self doPrimCommand: 0 name: nil page: aPage.
	answer == true ifTrue: [^aPage].
	answer == false ifTrue:
		[(lastPageNumber notNil and: [aPage pageNumber <= lastPageNumber])
			ifTrue: [aPage size: 0. ^aPage].
		 ^nil].
	error := self errorString: self lastError.
	^self error: 'read:'
]

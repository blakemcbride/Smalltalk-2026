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
	 case -- prim.c says so in its own words."
	| bytes page number |
	bytes := self sizeOnDisk.
	(bytes isInteger and: [bytes > 0])
		ifTrue: [^lastPageNumber := (bytes + self pageSize - 1) // self pageSize].
	page := self initPageNumber: 1.
	number := 0.
	[number := number + 1.
	 page pageNumber: number.
	 page size: 0.
	 (self read: page) notNil and: [page isFull]] whileTrue.
	(page size = 0 and: [number > 1]) ifTrue: [number := number - 1].
	^lastPageNumber := number
]

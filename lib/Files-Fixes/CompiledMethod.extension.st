"
Where a method's source lives, and how far into the file that may be.

A CompiledMethod's last three bytes are its source pointer: six bits of file
index and twenty-two bits of byte offset, which is 1983's whole answer to
`where did this method come from'.  Twenty-two bits is four megabytes, and on
an Alto with a floppy that was a file nobody would reach the end of.

A 2026 server compiles at run time -- a Browser accept, a Tonel service
reloaded when its file changes, a REST endpoint installed by a request -- and
appends to the changes file every time it does.  Four megabytes is a morning.
Past it the offset wrapped into the file-index field, so getSource read the
wrong place in the wrong file: usually a subscript error, sometimes another
method's source returned as this one's, and the only warning was a Transcript
show: on a system with no Transcript to show it on (Bugs4 FILES-3).
"
Extension { #name : 'CompiledMethod' }

{ #category : '*Files-Fixes' }
CompiledMethod >> setSourcePosition: position inFile: fileIndex [
	"Store the location of my source -- which file, and how far into it --
	 in my last three bytes, or refuse (Bugs4 FILES-3).

	 1983's method, with its warning turned into the refusal it should
	 always have been.  The trailer is

	     last     = (fileIndex - 1) * 64 + (position // 65536)
	     last - 1 = (position // 256) \\ 256
	     last - 2 = position \\ 256

	 so the offset has twenty-two bits and the file index has six, and they
	 share a byte.  An offset of 4194304 or more carries one into the file
	 index: the method then says it lives in file 3 or 4 -- files this image
	 does not have, or a spill file that holds something else entirely --
	 at an offset 4 MB short of the truth.  1983 noticed the edge and wrote
	 `hiByte > 62 ifTrue: [Transcript show: <Source file is getting full>]',
	 which is a sentence for a person sitting at a Dorado.  Under -serve
	 there is nobody reading the Transcript, and the audit ran 27 of those
	 warnings past a server that then answered `subscript out of bounds' for
	 the source of every method compiled after them.

	 Losing source silently is the worst of the three things that could
	 happen here, so it is the one this refuses to do.  Nothing is stored
	 unless all three bytes can be: a half-written trailer would point
	 somewhere arbitrary, which is the fault, not the fix.  The message
	 names the position and the file, because the person who meets this
	 error is compiling a method and has no reason to know that a file they
	 never mentioned is why they cannot.

	 It does not tell them what to do about it, because the thing 1983
	 provides does not work here.  `Smalltalk condenseChanges' rewrites the
	 changes file with only the source still reachable and renumbers every
	 method that pointed into it, which is exactly the remedy -- but it ends
	 in `Disk removeKey: (SourceFiles at: 2) name', and that name is a path
	 while Disk is keyed by the plain names in one directory, so it answers
	 `<image>.changes file not found' and condenses nothing.  That is a
	 fault of its own and not this one's to fix; what is refused here is
	 refused whether or not there is a way back."
	| index hiByte middleByte lowByte |
	fileIndex > 4 ifTrue: [^self error: 'invalid file number'].
	middleByte := position bitShift: -8.
	hiByte := middleByte bitShift: -8.
	middleByte := middleByte bitAnd: 255.
	lowByte := position bitAnd: 255.
	self checkSourcePosition: position inFile: fileIndex.
	index := self size - 2.
	self at: index + 2 put: fileIndex - 1 * 64 + hiByte.
	self at: index + 1 put: middleByte.
	self at: index put: lowByte
]

{ #category : '*Files-Fixes' }
CompiledMethod >> checkSourcePosition: position inFile: fileIndex [
	"Raise if a source starting at position in file fileIndex could not be
	 recorded in my trailer -- see setSourcePosition:inFile: -- and do
	 nothing otherwise.

	 Asked twice for every method compiled: by setSourcePosition:inFile:,
	 which is where 1983 noticed the edge, and by the putSource: methods
	 BEFORE they write anything, which is where it has to be noticed.
	 Noticed only at the end, the source was already appended to the
	 changes file, and the method -- installed by the compile before
	 putSource: was reached -- stayed in its class with a trailer of
	 0 0 0: it ran, and asking for its source raised `subscript is out
	 of bounds' (Bugs5 FILES-3)."
	(position bitShift: -22) > 0 ifTrue:
		[^Error new signal: 'the source file is full: a method''s source',
			' position is stored in 22 bits, and ', position printString,
			' needs more.  Source file ', fileIndex printString,
			' has passed 4194304 bytes; nothing further can be compiled',
			' into it, and what is already in it is still readable.']
]

{ #category : '*Files-Fixes' }
CompiledMethod >> putSource: sourceStr class: class category: catName inFile: fileIndex [
	"1983's, holding the source-file lock (Bugs5 FILES-2): the stream is
	 moved to its end, made writable, written and made read-only again, and
	 another compile doing the same in between wrote into the middle of it.

	 And refusing BEFORE writing when the source would start past what a
	 trailer can record (Bugs5 FILES-3): where it will start is the end of
	 the file plus the category chunk and the line break written ahead of
	 it, which are measured by writing them to a String first.  Nothing is
	 appended to the file, and nothing is set in me, when the answer is no;
	 the callers that install a method send this before installing it, so
	 a refusal leaves no method behind either."
	| file remoteString header |
	SourceFiles == nil ifTrue: [^self].
	LibraryLocks holdingSources: [
		file := SourceFiles at: fileIndex.
		file setToEnd.
		header := WriteStream on: (String new: 64).
		class printCategoryChunk: catName on: header.
		header cr.
		self checkSourcePosition: file position + header contents size inFile: fileIndex.
		file readWriteShorten.
		class printCategoryChunk: catName on: file.
		file cr.
		remoteString := RemoteString
			newString: sourceStr
			onFileNumber: fileIndex
			toFile: file.
		file nextChunkPut: ' '; readOnly.
		self setSourcePosition: remoteString position inFile: fileIndex]
]

{ #category : '*Files-Fixes' }
CompiledMethod >> putSource: sourceStr inFile: fileIndex [
	"1983's, holding the source-file lock, and refusing before writing
	 past the ceiling, for the reasons putSource:class:category:inFile:
	 gives.  What goes ahead of the source here is a fixed 19 characters:
	 a line break, `!Behavior method!' and a line break."
	| file remoteString |
	SourceFiles == nil ifTrue: [^self].
	LibraryLocks holdingSources: [
		file := SourceFiles at: fileIndex.
		file setToEnd.
		self checkSourcePosition: file position + 19 inFile: fileIndex.
		file readWriteShorten.
		file cr; nextPut: $!; nextChunkPut: 'Behavior method'; cr.
		remoteString := RemoteString
			newString: sourceStr
			onFileNumber: fileIndex
			toFile: file.
		file nextChunkPut: ' '; readOnly.
		self setSourcePosition: remoteString position inFile: fileIndex]
]

{ #category : '*Files-Fixes' }
CompiledMethod >> getSource [
	"Answer my source as it was compiled, or nil if there are no source
	 files or I have no source pointer.

	 1983's, reading through RemoteString>>verbatimString rather than
	 string: the source files hold a method's text exactly as the image
	 had it, and string's line-ending translation turned a line feed in
	 a literal into a carriage return (Bugs5 FILES-5).  The Cursor is
	 1983's."
	| end highByte position source |
	SourceFiles == nil ifTrue: [^nil].
	Cursor read showWhile: [
		end := self size.
		highByte := self at: end.
		position := highByte bitAnd: 63.
		position := position * 256 + (self at: end - 1).
		position := position * 256 + (self at: end - 2).
		position = 0
			ifTrue: [source := nil]
			ifFalse: [source :=
				(RemoteString newFileNumber: (highByte bitShift: -6) + 1 position: position)
					verbatimString]].
	^source
]

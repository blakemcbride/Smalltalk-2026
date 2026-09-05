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
	hiByte > 63 ifTrue:
		[^Error new signal: 'the source file is full: a method''s source',
			' position is stored in 22 bits, and ', position printString,
			' needs more.  Source file ', fileIndex printString,
			' has passed 4194304 bytes; nothing further can be compiled',
			' into it, and what is already in it is still readable.'].
	index := self size - 2.
	self at: index + 2 put: fileIndex - 1 * 64 + hiByte.
	self at: index + 1 put: middleByte.
	self at: index put: lowByte
]

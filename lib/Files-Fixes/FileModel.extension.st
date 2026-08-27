"
Files come with three line endings and a Paragraph breaks on one of them.

1983 wrote carriage returns, because the Alto did: CompositionScanner stops
on `CR asInteger + 1' and on nothing else, so a file written by anything
since -- a Unix line feed, a Windows carriage return and line feed -- has no
line break in it at all as far as the text pane is concerned.  doc/Display.md
arrived as one line thousands of characters long.

So the ending is translated on the way in, and translated back on the way
out.  A file keeps the convention it was written in: the convention is read
off the file itself at the moment it is accepted, rather than remembered
from when it was displayed, which means it is the file on disk that decides
and no state has to be carried between the two.  A file that has no line
ending yet -- a new one, or a single line with nothing after it -- is
written with line feeds.

Reading a directory is not reading a file.  Only a file's contents belong in
this pane; a directory's contents replace the list, which FileList>>getFile
does instead of coming here.  So the directory answer below is a guard
rather than a path anyone takes, and it is in the shape 1983 already uses
one line down -- a name that is not in the directory at all comes back as
the note `-new file-' rather than as an error.

The block parameter is written out.  The original is `setFileName:
[:localName]' -- an empty block whose parameter shadows the method
temporary of the same name, which sets that temporary only under 1983's
scope rules.  This profile compiles with closures, where it would not, and
lib/Scope-Fixes exists because of the one place that difference had already
bitten.  An explicit assignment means the same thing under both.
"
Extension { #name : 'FileModel' }

{ #category : '*Files-Fixes' }
FileModel >> acceptText: aText from: aController [
	"Write my text back, in the line ending the file is already written in."

	| ending |
	ending := self lineEndingOfFile.
	Cursor write showWhile: [
		((fileName includes: $!)
			ifTrue: [FileStream fileNamed: fileName]
			ifFalse: [FileStream newFileNamed: fileName])
				nextPutAll: (self string: aText string lineEnding: ending);
				close].
	^true
]

{ #category : '*Files-Fixes' }
FileModel >> lineEndingOfFile [
	"The line ending the file on disk is written in: the first one in it, or
	 a line feed if it has none.  Read from the file rather than remembered
	 from the display, so that the file itself is what decides.

	 readOnly before the first character, because a FileStream that is asked
	 to close without ever having been given a mode gives itself one:
	 FileStream>>writing answers `rwmode == nil ifTrue: [self
	 readWriteShorten. ^true]', and close then shortens the file at wherever
	 reading stopped.  Without it this method truncated the file it was only
	 looking at -- a three-line file came back one line long.  1983's own
	 readers all say readOnly first and this is the same discipline; see
	 File class>>initialize in this package for why the shortening had been
	 dormant."

	| dir localName stream lf cr answer |
	cr := Character cr.
	lf := Character value: 10.
	dir := FileDirectory
		directoryFromName: fileName
		setFileName: [:each | localName := each].
	(dir includesKey: localName) ifFalse: [^String with: lf].
	answer := nil.
	stream := FileStream oldFileNamed: fileName.
	stream readOnly.
	[answer isNil and: [stream atEnd not]] whileTrue: [
		| c |
		c := stream next.
		c = cr ifTrue: [
			answer := (stream atEnd not and: [stream peek = lf])
				ifTrue: [String with: cr with: lf]
				ifFalse: [String with: cr]].
		c = lf ifTrue: [answer := String with: lf]].
	stream close.
	^answer isNil ifTrue: [String with: lf] ifFalse: [answer]
]

{ #category : '*Files-Fixes' }
FileModel >> string: aString lineEnding: anEnding [
	"Answer the string with each carriage return written as anEnding."

	| out cr |
	cr := Character cr.
	(anEnding size = 1 and: [anEnding first = cr]) ifTrue: [^aString].
	out := WriteStream on: (String new: aString size + 64).
	aString do: [:c |
		c = cr
			ifTrue: [out nextPutAll: anEnding]
			ifFalse: [out nextPut: c]].
	^out contents
]

{ #category : '*Files-Fixes' }
FileModel >> text [
	"Answer the contents of my file, or a note saying why there are none."

	| dir localName |
	fileName == nil ifTrue: [^nil].
	dir := FileDirectory
		directoryFromName: fileName
		setFileName: [:each | localName := each].
	(dir isDirectoryNamed: localName) ifTrue: [
		^'-directory-' asText emphasizeFrom: 2 to: 10 with: 3].
	(dir includesKey: localName) ifFalse: [		"don't create file"
		^'-new file-' asText emphasizeFrom: 2 to: 9 with: 3].
	^Cursor read showWhile: [
		self textWithCarriageReturns:
			(FileStream oldFileNamed: fileName) contentsOfEntireFile]
]

{ #category : '*Files-Fixes' }
FileModel >> textWithCarriageReturns: aString [
	"Answer the string as text, with every common line ending turned into
	 the carriage return a Paragraph breaks lines on.  A carriage return and
	 line feed together are one ending, not two."

	| out in lf cr |
	cr := Character cr.
	lf := Character value: 10.
	out := WriteStream on: (String new: aString size).
	in := ReadStream on: aString.
	[in atEnd] whileFalse: [
		| c |
		c := in next.
		c = cr
			ifTrue: [
				(in atEnd not and: [in peek = lf]) ifTrue: [in next].
				out nextPut: cr]
			ifFalse: [
				out nextPut: (c = lf ifTrue: [cr] ifFalse: [c])]].
	^out contents asText
]

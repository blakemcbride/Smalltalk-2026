"
Where a POSIX directory answers for a place other than the one the system
was started in.

`directoryName' is 1983's own instance variable and PosixFileDirectory class
>>new sets it to the empty string, which is where a relative name resolves.
Nothing else ever read it.  Reading it is the whole of this: an instance
whose directoryName is `doc' walks doc, names its files doc/..., and
answers doc/phases when asked about phases.

Every name a directory hands out is therefore a designator that can be
opened as it stands, which is what keeps FileDirectory>>find:ifAbsentDo:
honest -- it compares the name it was asked for against the names it
enumerates, and the two have to be written the same way.

That cuts both ways, and filesMatching: is where it shows.  1983 matches the
pattern against the name a file answers to, and the name a file of mine
answers to is the whole path -- so `n*' asked of /home/blake was matched
against /home/blake/notes, and every pattern that did not begin with `*'
found nothing.  The pattern is the half of the designator it was cut from,
so it is matched against the half of the name it was cut from, and the
answer is written back out whole.

Renaming is the same concern from the other end.  A file that has been
renamed answers to the new name and to nothing else, so the File must be
told: the Alto directory's rename:newName: ends `file fileName: newName'
and the POSIX one, which was written from the primitive up rather than
from the Alto across, renamed on disk and left the File saying the name
that no longer exists.  Every later read reopened by that name and got
nothing.  Smalltalk condenseChanges is where it showed -- it writes the
changed sources to ST80.temp, installs that as SourceFiles at: 2 and
renames it over the changes file, and the rename was the step that made
every method's source unreadable, so the one remedy 1983 offers for a
changes file that has grown too big destroyed what it was condensing.
"
Extension { #name : 'PosixFileDirectory' }

{ #category : '*Files-Fixes' }
PosixFileDirectory >> directoryNamed: aString [
	"Answer a directory on the named place within me."

	^self class new setDirectory: nil directoryName: (self pathFor: aString)
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> fileNames [
	"Command 3 of primitive 131, walking my own place.  Nil is the directory
	 the system was started in, which is what an empty directoryName means.

	 A place that cannot be walked has no names in it, and that is what is
	 answered.  The primitive fails when opendir does -- a directory that is
	 not there, or one this user may not read -- and a primitive that fails
	 falls through to an empty body and answers the RECEIVER, so `fileNames'
	 handed back the directory itself.  A typed pattern is where that showed:
	 `nosuchdir/*' in the File List reached `aPosixFileDirectory collect:'
	 and opened a doesNotUnderstand, where the honest answer to a pattern
	 nothing matches is that nothing matches."

	| answer |
	answer := self
		doPrimitive: 3
		arg1: ((directoryName isNil or: [directoryName isEmpty])
			ifTrue: [nil]
			ifFalse: [directoryName])
		arg2: nil.
	^(answer isMemberOf: Array) ifTrue: [answer] ifFalse: [Array new]
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> filesMatching: pattern [
	"The names in me that match the pattern, each written the way it must be
	 opened.

	 The pattern is matched against the LOCAL name, which is the half of the
	 designator it was cut from.  FileDirectory>>filesMatching: matches it
	 against `each fileName' -- and a file of mine answers the whole path
	 there, because that is what makes a listed name openable -- so `n*' in
	 /home/blake was matched against /home/blake/notes and found nothing.
	 Only patterns beginning with `*' worked, and `*' hid it by swallowing
	 the directory in front.

	 Nor is a trailing period taken off the name.  The Alto did not keep one
	 and 1983 strips it here; a POSIX name that ends in a period is that
	 name, and shortening it in the list would hand back a name that opens
	 nothing."

	| names |
	names := WriteStream on: (Array new: 10).
	self fileNames do: [:each |
		(pattern match: each) ifTrue: [names nextPut: (self pathFor: each)]].
	^names contents
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> initFileName: aFileName [
	"A file of mine is named the way it must be opened: from where the system
	 was started, not from where I am."

	^super initFileName: (self pathFor: aFileName)
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> isDirectoryNamed: aString [
	"Ask the file system, which is the only one that knows.

	 Command 4 of primitive 131, beside the three 1983 asked for.  Tested
	 against true rather than for falsity, because a primitive that is not
	 there at all falls through to an empty body and answers the receiver."

	^(self doPrimitive: 4 arg1: (self pathFor: aString) arg2: nil) == true
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> realPathNamed: aString [
	"The name with every `.', `..' and symbolic link resolved, or nil when
	 there is no file of that name.

	 Command 5 of primitive 131, added for Bugs3 B57 (lower): the static
	 file handler can refuse a request path that climbs out of the document
	 root with `..', because that is visible in the text of the path, but a
	 SYMBOLIC LINK inside the root goes out of it with nothing in the text
	 to see -- `/link' pointing at /etc/passwd was served.  The only test
	 that catches it is to ask the file system what the name really is and
	 compare that with what the root really is, and only the file system
	 knows.

	 Nil rather than the name for a file that is not there: a caller
	 deciding whether to serve something wants `no' from a name it cannot
	 resolve, and answering the unresolved name would hand back exactly the
	 text the caller was trying to get behind."
	| answer |
	answer := self doPrimitive: 5 arg1: (self pathFor: aString) arg2: nil.
	^(answer isKindOf: String) ifTrue: [answer] ifFalse: [nil]
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> pathFor: aFileName [
	"The name, written from where the system was started.

	 The root is the one directory whose name already ends in the separator,
	 and putting another one after it writes `//' -- a spelling POSIX leaves
	 to the implementation, and one this system would then have to match
	 against the names it enumerates."

	(directoryName isNil or: [directoryName isEmpty]) ifTrue: [^aFileName].
	directoryName last = $/ ifTrue: [^directoryName , aFileName].
	^directoryName , '/' , aFileName
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> findKey: aFileName [
	"The old file of that name in me, or an error that says why not.

	 1983 answers `file not found' for every failure, followed by the
	 File's error text, which nothing has set: `zz.txt file not found, '
	 (Bugs3 B58).  A file the listing does not show is asked its SIZE
	 instead, because that opens it without creating it -- command 4, the
	 open, creates, since by the time 1983 opens a file it has decided the
	 file should exist -- and the failure of that open is where the
	 operating system says what is wrong: `No such file or directory',
	 `Permission denied'.  doCommand:name:page:error: raises with that text
	 after the 1983 words.  A file the listing missed but the size finds is
	 plainly there and is answered, closed again first so that the
	 descriptor the question opened is not left behind the one asFileStream
	 opens next: the listing is a readdir, and a directory this user may not
	 read lists nothing while the files in it may still be opened by name."
	^self
		find: (self initFileName: aFileName)
		ifAbsentDo: [:file |
			file doCommand: 3 name: nil page: nil error: 'file not found'.
			file close.
			file]
]

{ #category : '*Files-Fixes' }
PosixFileDirectory >> rename: aFile newName: newFileName [
	"Rename aFile to newFileName, and tell aFile its new name.

	 Command 2 of primitive 131 renames on disk.  That is half the job: a
	 File carries the name it opens itself by, and a File still saying the
	 old name after the rename opens nothing -- the name is gone from the
	 file system.  1983's Alto directory sets it (`file fileName: newName',
	 AltoFileDirectory>>rename:newName:); this one did not.

	 The name written into the File is the one the file must be OPENED by,
	 which is the pathFor: form, for the same reason initFileName: uses it:
	 a name a directory hands out is a designator, and a designator that
	 has to be prefixed before it works is not one."

	self doPrimitive: 2 arg1: (self pathFor: newFileName) arg2: aFile.
	aFile fileName: (self pathFor: newFileName)
]

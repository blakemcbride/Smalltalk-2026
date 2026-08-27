"
File names, and the File List's top pane.

The pane is where a person types a path, so what is checked here is the
spellings a person writes: a leading tilde, a bare directory name, `.' and
`..', and a pattern that has a directory in front of it.  All of it is
arithmetic on text and questions for the file system about the tree this
system is built in, so it runs wherever the build runs and opens no window.

The list pane is here too, for the two things about it that are not drawing:
the row a name is shown in -- a directory's carries ` (dir)' after it -- and
the selection, which the pane keeps by asking the model which row to
highlight.

The home directory is read from the environment, and a build machine may
have no HOME at all -- so the tilde tests say what must happen in both
cases rather than assuming one of them.
"
Class {
	#name : 'St80FileTest',
	#superclass : 'TestCase',
	#category : 'Library-Tests'
}

{ #category : 'private' }
St80FileTest >> patternFor: aString [
	"Accept the pattern on a File List with no views, and answer what the
	 top pane says afterwards."

	| fileList |
	fileList := FileList new list: SortedCollection new.
	fileList acceptPattern: aString asText from: nil.
	^fileList pattern string
]

{ #category : 'private' }
St80FileTest >> namesFor: aString [
	"Accept the pattern on a File List with no views, and answer the list."

	| fileList |
	fileList := FileList new list: SortedCollection new.
	fileList acceptPattern: aString asText from: nil.
	^fileList fileNameList asArray
]

{ #category : 'names' }
St80FileTest >> testTildeIsTheHomeDirectory [
	| home |
	home := FileDirectory homeDirectoryName.
	home isNil
		ifTrue: [
			"No HOME and no USERPROFILE: a tilde is a file name and stays one."
			self assert: (FileDirectory expandTilde: '~') equals: '~'.
			self assert: (FileDirectory expandTilde: '~/doc') equals: '~/doc']
		ifFalse: [
			self deny: (home last = $/ and: [home size > 1]).
			self assert: (FileDirectory expandTilde: '~') equals: home.
			self assert: (FileDirectory expandTilde: '~/doc') equals: home , '/doc']
]

{ #category : 'names' }
St80FileTest >> testTildeOnlyInFront [
	"A tilde that is not the whole name and is not followed by a separator is
	 part of the name -- `~scratch' is a legal file here, and answering a
	 user's home directory for it would need a password file.  A tilde
	 anywhere but the front is a name too."

	self assert: (FileDirectory expandTilde: '~scratch') equals: '~scratch'.
	self assert: (FileDirectory expandTilde: 'doc/~') equals: 'doc/~'.
	self assert: (FileDirectory expandTilde: '') equals: ''.
	self assert: (FileDirectory expandTilde: 'doc/*') equals: 'doc/*'
]

{ #category : 'names' }
St80FileTest >> testDotSegmentsAreTakenOut [
	self assert: (FileDirectory withoutDotSegments: 'doc/../lib') equals: 'lib'.
	self assert: (FileDirectory withoutDotSegments: 'a/b/../c') equals: 'a/c'.
	self assert: (FileDirectory withoutDotSegments: './doc') equals: 'doc'.
	self assert: (FileDirectory withoutDotSegments: '/home/blake/..') equals: '/home'.
	self assert: (FileDirectory withoutDotSegments: '/') equals: '/'.
	"A leading `..' has nowhere to go: nothing here knows what directory the
	 system was started in.  Above the root there is nothing.  And a name
	 that cancels out entirely is the directory you are in, which is a name,
	 where the empty string is not."
	self assert: (FileDirectory withoutDotSegments: '..') equals: '..'.
	self assert: (FileDirectory withoutDotSegments: '../..') equals: '../..'.
	self assert: (FileDirectory withoutDotSegments: '/..') equals: '/'.
	self assert: (FileDirectory withoutDotSegments: 'doc/..') equals: '.'
]

{ #category : 'names' }
St80FileTest >> testDesignatorSplitsAtTheLastSeparator [
	| local |
	self assert: (FileDirectory directoryFromName: 'README.md' setFileName: [:each | local := each])
		== Disk.
	self assert: local equals: 'README.md'.
	self assert: (FileDirectory directoryFromName: 'lib/Kernel' setFileName: [:each | local := each])
		directoryName equals: 'lib'.
	self assert: local equals: 'Kernel'.
	"The root is the one directory with nothing in front of its separator.
	 Answering the empty directoryName there would be the directory the
	 system was started in, so `/tmp' would have asked for a file named tmp
	 in the current directory."
	self assert: (FileDirectory directoryFromName: '/tmp' setFileName: [:each | local := each])
		directoryName equals: '/'.
	self assert: local equals: 'tmp'
]

{ #category : 'the file list' }
St80FileTest >> testAcceptingADirectoryEntersIt [
	"A pattern that names a directory is a move, not a filter: the pane is
	 rewritten to the contents of that directory.  1983 had no way to ask,
	 and answered a one-name list holding the directory itself."

	self assert: (self patternFor: 'lib') equals: 'lib/*'.
	self assert: (self patternFor: 'src/../lib') equals: 'lib/*'.
	self assert: (self patternFor: '..') equals: '../*'.
	"`.' is where the system was started, and that is spelled `*' here --
	 the same name everything else in this system uses for it."
	self assert: (self patternFor: '.') equals: '*'.
	self assert: (self patternFor: '/') equals: '/*'
]

{ #category : 'the file list' }
St80FileTest >> testAcceptingATildeEntersTheHomeDirectory [
	| home |
	home := FileDirectory homeDirectoryName.
	home isNil ifTrue: [^self].
	self assert: (self patternFor: '~') equals: home , '/*'.
	self assert: (self patternFor: '~/') equals: home , '/*'
]

{ #category : 'the file list' }
St80FileTest >> testPatternMatchesInsideADirectory [
	"The pattern is matched against the LOCAL name.  Matched against the
	 whole designator -- which is what a listed name has to be, so that
	 selecting it opens it -- `Files-F*' in lib was compared against
	 lib/Files-Fixes and found nothing, and only patterns beginning with `*'
	 worked at all.

	 The way up is in the list beside what matched, and sorts in front of it
	 because it carries the same directory and `.' comes before a letter."

	| names |
	names := self namesFor: 'lib/Files-F*'.
	self assert: names equals: #('lib/..' 'lib/Files-Fixes')
]

{ #category : 'the file list' }
St80FileTest >> testListedNamesCarryTheirDirectory [
	"Every name in the list is a designator that can be opened as it stands,
	 which is also what lets SelectionInListView find the highlighted row."

	| names |
	names := self namesFor: 'lib/*'.
	self deny: names isEmpty.
	names do: [:each |
		self assert: (each copyFrom: 1 to: 4) equals: 'lib/']
]

{ #category : 'the file list' }
St80FileTest >> testAcceptingAFileNameKeepsIt [
	"A name that is neither a pattern nor a directory is 1983's case and is
	 answered as the one-name list it always was."

	self assert: (self namesFor: 'README.md') equals: (Array with: 'README.md').
	self assert: (self patternFor: 'README.md') equals: 'README.md'
]

{ #category : 'the file list' }
St80FileTest >> testAPatternOnNoDirectoryMatchesNothing [
	"A place that cannot be walked has no names in it.  Primitive 131's
	 listing fails when opendir does, and a failed primitive falls through to
	 an empty body and answers the RECEIVER -- so a mistyped path reached
	 `aPosixFileDirectory collect:' and opened a doesNotUnderstand, where the
	 answer to a pattern nothing matches is that nothing matches."

	| fileList |
	self assert: (Disk directoryNamed: 'no-such-directory-here') fileNames isEmpty.
	fileList := FileList new list: SortedCollection new.
	self assert: (fileList filesMatching: 'no-such-directory-here/*') isEmpty
]

{ #category : 'the file list' }
St80FileTest >> testANewNameIsTakenWhereTheListIs [
	"A bare name typed into `new' means the directory the list is showing,
	 which is the one the pattern names.  The pattern is the only record of
	 where the list is, so the prefix is read back out of it."

	| fileList |
	fileList := FileList new list: SortedCollection new.
	fileList acceptPattern: 'lib' asText from: nil.
	self assert: fileList listedDirectoryPrefix equals: 'lib/'.
	self assert: (fileList newFileNameFrom: 'notes.txt') equals: 'lib/notes.txt'.
	"A name with a separator in it is a path in its own right, and takes the
	 spellings the pattern pane takes."
	self assert: (fileList newFileNameFrom: 'src/../doc/notes.txt')
		equals: 'doc/notes.txt'.
	self assert: (fileList newFileNameFrom: '/tmp/notes.txt') equals: '/tmp/notes.txt'
]

{ #category : 'the file list' }
St80FileTest >> testANewNameWithNoDirectoryToBeIn [
	"Where the system was started has no prefix, which is how every name in
	 this system spells that place.  Several lines name several directories
	 and so name no one place, and a File List opened on a list of names has
	 no pattern at all."

	| fileList |
	fileList := FileList new list: SortedCollection new.
	self assert: fileList listedDirectoryPrefix equals: ''.
	self assert: (fileList newFileNameFrom: 'notes.txt') equals: 'notes.txt'.
	fileList acceptPattern: '*' asText from: nil.
	self assert: fileList listedDirectoryPrefix equals: ''.
	fileList acceptPattern: ('lib/*' , (String with: Character cr) , 'doc/*') asText
		from: nil.
	self assert: fileList listedDirectoryPrefix equals: ''
]

{ #category : 'the file list' }
St80FileTest >> testDirectoriesAreMarkedInTheList [
	"What a directory holds is files and directories, and a row that is only
	 a name does not say which of the two it is -- so a directory's row
	 carries ` (dir)' after the name.  The list itself is untouched:
	 `fileNameList' answers the designators the rest of the window opens."

	| fileList |
	fileList := FileList new list: SortedCollection new.
	fileList acceptPattern: 'lib/Files-F*' asText from: nil.
	self assert: fileList fileNameList asArray
		equals: #('lib/..' 'lib/Files-Fixes').
	self assert: fileList listedNames asArray
		equals: #('..' 'lib/Files-Fixes (dir)').
	self assert: (fileList nameFromListed: 'lib/Files-Fixes (dir)')
		equals: 'lib/Files-Fixes'.
	"A selection is reported as the row it was drawn as, and comes back as
	 the row when the pane rebuilds and asks which one to highlight."
	fileList fileName: 'lib/Files-Fixes (dir)'.
	self assert: fileList fileName equals: 'lib/Files-Fixes'.
	self assert: fileList listedFileName equals: 'lib/Files-Fixes (dir)'.
	"A file is its own row."
	fileList acceptPattern: 'README.md' asText from: nil.
	self assert: fileList listedNames asArray
		equals: (Array with: 'README.md')
]

{ #category : 'the file list' }
St80FileTest >> testAMarkedRowIsLookedUpNotCutBack [
	"A row is turned back into a name by finding it in the list, not by
	 cutting the mark off the end: ` (dir)' is a legal ending for a POSIX
	 file name, and a file that really ends in one stands in the list under
	 that whole name.  Only a row no name answers to is a name and a mark."

	| fileList |
	fileList := FileList new list: #('notes' 'notes (dir)') asSortedCollection.
	self assert: (fileList nameFromListed: 'notes (dir)') equals: 'notes (dir)'.
	self assert: (fileList nameFromListed: 'notes') equals: 'notes'.
	"A mark on a name the list does not hold stands for nothing, and is
	 answered as it came."
	self assert: (fileList nameFromListed: 'other (dir)') equals: 'other (dir)'.
	self assert: (fileList nameFromListed: nil) isNil.
	fileList := FileList new list: #('notes') asSortedCollection.
	self assert: (fileList nameFromListed: 'notes (dir)') equals: 'notes'
]

{ #category : 'the file list' }
St80FileTest >> testTheListHoldsTheWayUp [
	"A directory listing holds the way out of itself, so that going up is the
	 same gesture as going anywhere else.  The row says `..'; the name behind
	 it says it after the directory the list is showing, so it is a
	 designator like every other name in the list."

	| fileList |
	fileList := FileList new list: SortedCollection new.
	fileList acceptPattern: 'lib/*' asText from: nil.
	self assert: (fileList fileNameList includes: 'lib/..').
	self assert: (fileList listedNames includes: '..').
	self assert: (fileList nameFromListed: '..') equals: 'lib/..'.
	"And getting its contents is the way up."
	fileList fileName: '..'.
	self assert: fileList fileName equals: 'lib/..'.
	self assert: fileList listedFileName equals: '..'.
	fileList getFile.
	self assert: fileList pattern string equals: '*'.
	"Where the system was started the names carry no directory, so the name
	 and the row are the same string."
	self assert: (fileList fileNameList includes: '..').
	self assert: (fileList nameFromListed: '..') equals: '..'
]

{ #category : 'the file list' }
St80FileTest >> testTheRootHasNoWayUp [
	"`/..' is `/', and a row that takes you where you already are is a row
	 that does nothing.  Nor is there a way up out of a place that is not
	 there: a mistyped directory must be left saying that nothing matched."

	| fileList |
	fileList := FileList new list: SortedCollection new.
	fileList acceptPattern: '/*' asText from: nil.
	self deny: (fileList fileNameList includes: '/..').
	self deny: (fileList listedNames includes: '..').
	self assert: fileList parentName isNil.
	fileList acceptPattern: 'no-such-directory-here/*' asText from: nil.
	self assert: fileList parentName isNil.
	self assert: fileList fileNameList isEmpty.
	"A bare file name is 1983's one-name list, not a directory's contents,
	 and a list of patterns is showing no one place."
	fileList acceptPattern: 'README.md' asText from: nil.
	self assert: fileList parentName isNil.
	fileList acceptPattern: ('lib/*' , (String with: Character cr) , 'doc/*') asText
		from: nil.
	self assert: fileList parentName isNil
]

{ #category : 'the file list' }
St80FileTest >> testTheListKeepsItsSelection [
	"The list pane rebuilds its rows whenever #fileName is announced, and
	 asks the model which row to highlight.  It has to be able to ask: a view
	 opened with no selector for that answers `nothing' and reports
	 `fileName: nil' back, so the model loses the file it was on every time
	 the list is redrawn -- which is what left a renamed file unselected and
	 a file just created with nothing behind the text pane."

	| fileList view blind |
	fileList := FileList new list: #('a' 'b' 'c') asSortedCollection.
	fileList setFileName: 'b'.
	view := FileList listViewOn: fileList.
	view list: view getList.
	self assert: fileList fileName equals: 'b'.
	"1983's wiring, for contrast."
	blind := SelectionInListView
		on: fileList
		aspect: #fileName
		change: #fileName:
		list: #fileNameList
		menu: #fileListMenu
		initialSelection: nil.
	blind list: blind getList.
	self assert: fileList fileName isNil
]

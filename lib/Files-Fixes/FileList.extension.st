"
Getting the contents of a directory enters it.

The three panes divide the work: the pattern says where you are, the list
says what is there, and the text pane shows a file.  A directory has no
text, so its contents belong in the list -- getting them is a move, not a
read, and the text pane is left to files.

The pattern is where the move is recorded, because the pattern is already
the thing that builds the list: acceptPattern:from: matches it against a
directory and answers the names.  Entering doc sets it to `doc/*', which
puts the place you are in front of you in the top pane and makes going
somewhere else the same gesture as going anywhere else -- edit it and
accept.  `*' is the way back up, and `../*' goes above the directory the
system was started in.

Names in the list carry their directory with them, because a name in this
system is a designator resolved from where the system was started, and
because SelectionInListView finds the highlighted row by matching what the
model answers for #fileName against the list.  The two have to be the same
string.

What the pane SHOWS is not quite what those names are.  The middle pane of a
File List is what a directory holds, and what a directory holds is files and
directories -- so a row that is only a name does not say which of the two it
is, and `get contents' on it is a guess.  A directory is therefore written
with ` (dir)' after it, and the mark is display and nothing else: the pane's
rows come from `listedNames' while `fileNameList' still answers the
designators everything else in the window opens.

That makes a row and a name two different strings, so the row the pane hands
back is looked UP in the list rather than having its mark cut off.  ` (dir)'
is a legal ending for a POSIX file name, and a file that really ends in one
stands in the list under that name and is found first; only a row that no
name answers to is a name with a mark after it.  The one case that stays
ambiguous is a file named `x (dir)' beside a directory named `x', which draws
two identical rows -- and no spelling of a mark is one a file name cannot
also have.

A directory listing holds the way out of itself.  `..' is a row in the middle
pane whenever the list is a directory's contents and that directory has one
above it, so going up is the same gesture as going anywhere else -- select it
and get its contents -- rather than a path that has to be typed.  The row is
spelled `..' and nothing else, because that is the spelling every file system
this runs on uses and because no file can be named it; the name behind it is
`..' written after the directory the list is showing, so it is a designator
like every other name in the list and opens as it stands.

The root is the one place with nothing above it, and there `..' is not
offered: `/..' is `/', and a row that takes you where you already are is a row
that does nothing.  A relative pattern cannot be asked the same question --
nothing here knows what directory the system was started in, so `../../*' is
as far as this can tell where it says it is -- and above the start the rows
keep saying `..' as long as the file system keeps answering for them.

The pattern pane is a place a person types a path, so it takes the paths a
person writes.  A leading `~' is the home directory, `.' is where you are and
`..' is above it, and the pane is rewritten with the tilde expanded and the
dots taken out -- so what it says afterwards is a name that can be typed back
in, and one the names in the list are built from.

Accepting a pattern that names a DIRECTORY enters it, which is the same move
`get contents' makes on a directory in the list.  1983 had no way to ask, so
a directory name went through isLegalFileName: and came back as a list of one
-- the name typed, sitting there to be selected and read as a file.  A
pattern that names a directory is not a pattern at all, and the two gestures
that mean `go there' now agree about what they do.

A new file is made from the list's own menu, because the list is what shows
a directory and `new' means new HERE.  A bare name typed into the prompt is
taken in the directory the list is showing -- the pattern says `doc/*', so
`notes.txt' is doc/notes.txt -- and a name with a separator in it is a path
in its own right, taken exactly as the pattern pane takes one.  The file is
created empty at once rather than being left until the text is accepted:
the list is re-read from the disk at every accept of the pattern, so a name
with no file behind it would quietly go away again, and a name in this list
is meant to be a designator that opens.

A new file is typeable the moment it is made, and that turns on the list
being able to say what is selected in it.  SelectionInListView rebuilds its
rows whenever #fileName is announced and asks the model which row to
highlight, through the `initialSelection' selector it was opened with.  1983
opens the File List with nil there, alone among every pluggable list in this
system, so the answer was always `nothing' and the rebuild reported
`fileName: nil' back to the model.  Every announcement of #fileName threw the
selection away: a renamed file came back unselected, and a file just created
could not be typed into, because fileName: had already put the text pane back
to unselected.  Asking for #fileName -- the aspect the list is already
viewing -- is what the Browser's four panes and the Inspector's do, and it is
the whole of the difference.

The trailing period is 1983's and is gone from here.  checkName:fixErrors:
strips one because an Alto file name ended in a period it did not keep.  That
rule applied to a pattern turns `..' into `.' and `.' into nothing, so both
were refused by isLegalFileName: and the pane answered `No matching files' --
for the two names it most has to be able to say.
"
Extension { #name : 'FileList' }

{ #category : '*Files-Fixes' }
FileList >> acceptPattern: aText from: aController [
	"Take the accepted text as the new pattern, after writing out what a
	 person is allowed to leave short: a leading `~' becomes the home
	 directory and the dot segments are taken out, on every line.

	 A pattern that names one directory is a move rather than a filter, so it
	 enters that directory instead of being matched -- `..', `~', and the
	 name of a subdirectory all mean go there.  Everything else is 1983's
	 loop, less its trailing period: an Alto name did not keep one, and
	 taking it off a pattern is what left `..' and `.' unsayable here.

	 The way up is put in after the list is asked whether it is empty, and
	 not before: a pattern that matches nothing must still say so in the
	 pane, and a list holding only the row that leaves it is a list that
	 matched nothing.

	 There is a pane to say it in only when there is a controller.  A File
	 List with no views is how this method is driven from a test, and 1983's
	 line sends `deselect' to whatever it was handed."

	| text where readStream testPattern newList |
	text := self expandedPattern: aText string.
	where := self directoryDesignatedBy: text.
	where isNil ifFalse: [
		Cursor read showWhile: [self enterDirectory: where].
		^true].
	Cursor execute showWhile: [
		myPattern := text.
		readStream := ReadStream on: myPattern.
		newList := SortedCollection new.
		[readStream atEnd] whileFalse: [
			testPattern := (readStream upTo: Character cr) trimmed.
			testPattern isEmpty ifFalse: [
				((testPattern includes: $*) or: [testPattern includes: $#])
					ifTrue: [newList addAll: (self filesMatching: testPattern)]
					ifFalse: [
						(self isLegalFileName: testPattern)
							ifTrue: [newList add: testPattern]]]].
		list := newList.
		(list isEmpty and: [aController notNil]) ifTrue: [
			aController
				deselect;
				replaceSelectionWith: 'No matching files' asText;
				select].
		self addParentEntry.
		self changed: #pattern.
		self changed: #fileName].
	^true
]

{ #category : '*Files-Fixes' }
FileList >> directoryDesignatedBy: aString [
	"The one directory this whole pattern names, or nil when it names
	 anything else.

	 One directory: more than one line is a list of patterns and stays one,
	 and a line with `*' or `#' in it is a filter even when a directory
	 happens to answer to it."

	| trimmed |
	trimmed := aString trimmed.
	trimmed isEmpty ifTrue: [^nil].
	(trimmed includes: Character cr) ifTrue: [^nil].
	((trimmed includes: $*) or: [trimmed includes: $#]) ifTrue: [^nil].
	^(self isDirectoryNamed: trimmed) ifTrue: [trimmed] ifFalse: [nil]
]

{ #category : '*Files-Fixes' }
FileList >> enterDirectory: aName [
	"Make my list be the contents of that directory, and say so in the
	 pattern.  Nothing is selected in a directory just entered, so the text
	 pane has nothing to show and is left empty.

	 The name is written out before it is shown: the pane is the display of
	 where you are, and `~/doc' or `doc/../lib' says where you asked to go
	 rather than where you now are.  The directory the system was started in
	 is `*' rather than `./*', because that is the spelling everything else
	 in this system uses for it and the names in the list would otherwise
	 all carry a `./' they do not need."

	| name |
	name := FileDirectory withoutDotSegments: (FileDirectory expandTilde: aName).
	myPattern := name = '.'
		ifTrue: ['*']
		ifFalse: [
			name last = $/
				ifTrue: [name , '*']
				ifFalse: [name , '/*']].
	list := SortedCollection new.
	list addAll: (self filesMatching: myPattern).
	self addParentEntry.
	self setFileName: nil.
	isReading := false.
	self changed: #pattern.
	self changed: #fileName.
	self changed: #text
]

{ #category : '*Files-Fixes' }
FileList >> expandedPattern: aString [
	"Every line of the pattern with its leading `~' made into the home
	 directory.  A shell expands a tilde before the command sees it, and the
	 pane is rewritten so that what it says is what was matched."

	| stream out first |
	(aString includes: $~) ifFalse: [^aString].
	stream := ReadStream on: aString.
	out := WriteStream on: (String new: aString size).
	first := true.
	[stream atEnd] whileFalse: [
		first ifTrue: [first := false] ifFalse: [out nextPut: Character cr].
		out nextPutAll: (FileDirectory expandTilde: (stream upTo: Character cr) trimmed)].
	^out contents
]

{ #category : '*Files-Fixes' }
FileList >> pattern [
	"Answer the pattern as text, because a text view composes what it is
	 given.

	 1983 answers `myPattern' -- the String that acceptPattern:from: kept
	 out of the text the user typed -- and gets away with it because nothing
	 ever asks: the pattern pane holds the text the typing put there, and
	 the model is only ever told about it.  Entering a directory writes the
	 pattern rather than reading it, so the pane is now asked, and a String
	 arrives at Paragraph>>text: to be composed:

	    String(Object)>>doesNotUnderstand: emphasisAt:
	    CompositionScanner>>setStopConditions
	    Paragraph>>composeAll
	    Paragraph>>text:

	 Nil is left alone: an unaccepted pattern answered nil before this and
	 the pane knows what to do with it."

	^myPattern isNil ifTrue: [nil] ifFalse: [myPattern asText]
]

{ #category : '*Files-Fixes' }
FileList >> getFile [
	"Read the selected file -- or, if a directory is selected, enter it."

	self changeRequest ifFalse: [^self].
	(self isDirectoryNamed: fileName) ifTrue: [
		^Cursor read showWhile: [self enterDirectory: fileName]].
	isReading := true.
	self changed: #text
]

{ #category : '*Files-Fixes' }
FileList >> parentName [
	"The name of the directory above the one the list is showing, or nil when
	 there is none to go to.

	 The pattern is the only record of where the list is, so the name is that
	 pattern's directory with `..' written after it -- `lib/*' is showing lib,
	 and the way up out of it is `lib/..'.  Written rather than worked out:
	 `lib/..' is a designator the file system resolves itself, it carries the
	 prefix every other name in the list carries so it sorts where a person
	 looks for it, and enterDirectory: takes the dots out when it gets there.

	 Only a pattern that is one line and has a `*' or a `#' in it: a list of
	 patterns is showing several directories and so is showing no one place,
	 and a bare file name is 1983's one-name list rather than a directory's
	 contents.

	 Nil at the root, where `/..' is `/' and a row that goes where you already
	 are does nothing, and nil where there is no such directory at all -- a
	 mistyped `nosuchdir/*' has nothing above it to offer, and must be left
	 saying that nothing matched."

	| prefix parent |
	myPattern isNil ifTrue: [^nil].
	(myPattern includes: Character cr) ifTrue: [^nil].
	((myPattern includes: $*) or: [myPattern includes: $#]) ifFalse: [^nil].
	prefix := self listedDirectoryPrefix.
	parent := prefix , '..'.
	(FileDirectory withoutDotSegments: parent)
		= (FileDirectory withoutDotSegments:
			(prefix isEmpty ifTrue: ['.'] ifFalse: [prefix]))
				ifTrue: [^nil].
	^(self isDirectoryNamed: parent) ifTrue: [parent] ifFalse: [nil]
]

{ #category : '*Files-Fixes' }
FileList >> addParentEntry [
	"Put the way up in the list, when the list is a directory's contents and
	 that directory has one above it.  A name in the list, and not a row the
	 pane draws over one, because it is a name a person selects: `get
	 contents' enters it and `copy name' answers a designator that opens."

	| parent |
	parent := self parentName.
	parent isNil ifFalse: [list add: parent]
]

{ #category : '*Files-Fixes' }
FileList >> isParentName: aName [
	"Whether the name is the way up out of the directory the list is showing.

	 Read off the name rather than compared against `parentName', because
	 every row of the pane asks this and the answer is written into the name:
	 a name that ends in `..' is one, and no file can be named `..' for it to
	 be confused with."

	aName isNil ifTrue: [^false].
	aName = '..' ifTrue: [^true].
	^aName size >= 3
		and: [(aName copyFrom: aName size - 2 to: aName size) = '/..']
]

{ #category : '*Files-Fixes' }
FileList >> isDirectoryNamed: aName [
	"Whether the designator names a directory rather than a file.

	 The designator is split at its last separator and the question is put to
	 the directory in front of it, which is the only thing that knows -- the
	 same three lines `get contents' and an accepted pattern each carried a
	 copy of, and the rows of the list would have been a third.  Nil is not a
	 directory: nothing is selected in a list just filled, and that is a
	 question this is asked."

	| dir localName |
	aName isNil ifTrue: [^false].
	dir := FileDirectory
		directoryFromName: aName
		setFileName: [:each | localName := each].
	^dir isDirectoryNamed: localName
]

{ #category : '*Files-Fixes' }
FileList >> directoryMark [
	"What is written after the name of a directory in the list pane.  Here
	 rather than in the two methods that use it, because a mark one of them
	 writes and the other does not know is a row nothing can be selected
	 from."

	^' (dir)'
]

{ #category : '*Files-Fixes' }
FileList >> listedNames [
	"The rows of the list pane: the names in the list, with the directories
	 among them marked.

	 The middle pane of a File List is what a directory holds, and what a
	 directory holds is files and directories.  A row that is only a name
	 does not say which of the two it is, so `get contents' -- which reads
	 one and enters the other -- is a guess until you make it.

	 The list itself is left alone.  `fileNameList' is 1983's accessor and
	 still answers the designators, because a designator is what the rest of
	 this window opens, renames and removes."

	list isNil ifTrue: [^Array new].
	^list collect: [:each | self listedNameFor: each]
]

{ #category : '*Files-Fixes' }
FileList >> listedNameFor: aName [
	"The row the list pane shows for that name.

	 The way up is shown as `..' alone, without the directory it is written
	 after and without the mark.  It is the spelling every file system this
	 runs on uses for that place, and a row that said `lib/.. (dir)' would be
	 the one row in the pane a person had to read twice."

	(self isParentName: aName) ifTrue: [^'..'].
	^(self isDirectoryNamed: aName)
		ifTrue: [aName , self directoryMark]
		ifFalse: [aName]
]

{ #category : '*Files-Fixes' }
FileList >> listedFileName [
	"The row the selected file stands in, which is what the list pane asks
	 for when it rebuilds its rows and has to find the one to highlight.  It
	 is matched against the rows, so it has to be one of them rather than the
	 name behind one."

	^fileName isNil ifTrue: [nil] ifFalse: [self listedNameFor: fileName]
]

{ #category : '*Files-Fixes' }
FileList >> nameFromListed: aRow [
	"The name a row of the list pane stands for.

	 Looked up in the list rather than cut back, because ` (dir)' is a legal
	 ending for a POSIX file name: a file that really ends in one stands in
	 the list under that whole name, and cutting the mark off would hand back
	 a name nothing answers to.  So a row that IS a name is that name, and
	 only a row no name answers to is a name with a mark after it.

	 `..' is the way up, and the name behind it is `..' written after the
	 directory the list is showing -- which is the name the list holds, and
	 is looked up there like any other.

	 A row that is none of these is answered as it stands.  Every row this is
	 asked about came out of `listedNames' a moment ago, so there is no such
	 row -- but a model that cannot be sent a name it does not know is a
	 model with an error in it rather than one that says `nothing matches'."

	| mark bare |
	aRow isNil ifTrue: [^nil].
	list isNil ifTrue: [^aRow].
	(list includes: aRow) ifTrue: [^aRow].
	aRow = '..' ifTrue: [
		bare := self listedDirectoryPrefix , '..'.
		^(list includes: bare) ifTrue: [bare] ifFalse: [aRow]].
	mark := self directoryMark.
	aRow size > mark size ifFalse: [^aRow].
	(aRow copyFrom: aRow size - mark size + 1 to: aRow size) = mark
		ifFalse: [^aRow].
	bare := aRow copyFrom: 1 to: aRow size - mark size.
	^(list includes: bare) ifTrue: [bare] ifFalse: [aRow]
]

{ #category : '*Files-Fixes' }
FileList >> fileName: aRow [
	"1983's, taking the name back out of the row the list pane hands over.

	 The pane reports a selection by handing back the row it drew, and a row
	 for a directory is not the name of one -- so a File List that took it as
	 written held `lib (dir)' as its file name and answered `-new file-' for
	 a directory that is plainly there."

	self setFileName: (self nameFromListed: aRow).
	isReading := false.
	self changed: #text
]

{ #category : '*Files-Fixes' }
FileList >> fileListMenu [
	"The list pane's menu, with `new' on it.

	 1983 answers nil when nothing is selected, because every command it has
	 is a command on the selected file.  `new' is not: it is a command on the
	 directory the list is showing, and the moment it is most wanted is right
	 after entering one, when nothing is selected yet.  So a list with no
	 selection answers the one command that does not need one, the way
	 Browser>>protocolMenu answers `add protocol' for an empty protocol pane.

	 That menu is built each time rather than kept in a class variable.  A
	 menu built on the way up is a menu built once per gesture; FileMenu is
	 1983's cache and stays one."

	fileName isNil ifTrue: [
		^ActionMenu labels: 'new' selectors: #(newFile)].
	FileMenu isNil ifTrue: [
		FileMenu := ActionMenu
			labels: 'get contents\file in\copy name\rename\remove\new' withCRs
			lines: #(2 4)
			selectors: #(getFile fileInFile copyName renameFile deleteFile newFile)].
	^FileMenu

	"Evaluate this when you change this method:
		FileList flushMenus"
]

{ #category : '*Files-Fixes' }
FileList >> newFile [
	"Ask for a name, make an empty file of that name, and put it in front of
	 the text pane ready to be typed into.

	 The file is created now rather than at the first accept of the text.  A
	 name in this list is a designator that opens -- `get contents', `rename'
	 and `remove' all assume it -- and the list is rebuilt from the disk
	 every time the pattern is accepted, so a name standing in the list with
	 no file behind it would answer the `-new file-' note to every reader and
	 then disappear at the next accept.  Creating it also means the text pane
	 opens on an empty file rather than on that note, which is the difference
	 between a new file and a note about one.

	 The name is asked for the way renameFile asks: a name that is bad or
	 already taken is offered again rather than refused, and an empty answer
	 gives up.

	 The list is told first and the text pane second, because telling the
	 list runs SelectionInListView>>list:, which finds the new name,
	 highlights it, and reports it back through fileName: -- and fileName:
	 puts the text pane back to unselected, the way choosing a name in the
	 list always does.  So `isReading' is set after that, which is what `get
	 contents' does one gesture later and is what leaves the new file ready
	 to type into."

	| answer name continue dir localName |
	self changeRequest ifFalse: [^self].
	continue := true.
	name := nil.
	localName := nil.
	[continue] whileTrue: [
		answer := FillInTheBlank request: 'Name for the new file?' initialAnswer: ''.
		(answer isNil or: [answer trimmed isEmpty]) ifTrue: [^self].
		name := self newFileNameFrom: answer.
		dir := FileDirectory
			directoryFromName: name
			setFileName: [:each | localName := each].
		(localName notEmpty
			and: [(dir isLegalFileName: localName)
				and: [(dir includesKey: localName) not]])
			ifTrue: [continue := false]
			ifFalse: [
				(BinaryChoice message: 'File exists or bad file name. Try again?')
					ifFalse: [^self]]].
	Cursor write showWhile: [(FileStream newFileNamed: name) close].
	list isNil ifTrue: [list := SortedCollection new].
	(list includes: name) ifFalse: [list add: name].
	self setFileName: name.
	self changed: #fileName.
	isReading := true.
	self changed: #text
]

{ #category : '*Files-Fixes' }
FileList >> newFileNameFrom: aString [
	"The designator a name typed into the `new' prompt stands for.

	 A bare name means the directory the list is showing, because that is the
	 place in front of the person typing: the pattern says `doc/*' and the
	 list says what is in doc, so `notes.txt' there is doc/notes.txt.  A name
	 with a separator in it, or a leading `~', is a path in its own right and
	 is taken as written -- the same spellings the pattern pane takes, so
	 there is one set of rules for typing a path in this window rather than
	 two."

	| typed |
	typed := FileDirectory expandTilde: aString trimmed.
	typed isEmpty ifTrue: [^typed].
	^(typed includes: $/)
		ifTrue: [FileDirectory withoutDotSegments: typed]
		ifFalse: [self listedDirectoryPrefix , typed]
]

{ #category : '*Files-Fixes' }
FileList >> listedDirectoryPrefix [
	"The directory the list is showing, written so a file name can be put
	 straight after it.

	 The pattern is where that directory is recorded -- `doc/*' is the list
	 of doc -- so the prefix is the pattern up to its last separator.  It is
	 empty for the directory the system was started in, which is how every
	 name in this system spells that place, and empty for a pattern of
	 several lines, which names several directories and so names no one place
	 a bare name could mean."

	| line cut |
	myPattern isNil ifTrue: [^''].
	(myPattern includes: Character cr) ifTrue: [^''].
	line := myPattern trimmed.
	cut := 0.
	1 to: line size do: [:i | (line at: i) = $/ ifTrue: [cut := i]].
	^line copyFrom: 1 to: cut
]

{ #category : '*Files-Fixes' }
FileList class >> open [
	"Open a File List: a pattern pane, a list of names, and the text of the
	 selected file.  1983's, with the list pane built by listViewOn: so that
	 it can keep a selection."

	| topView aFileList patternView |
	aFileList := self new list: OrderedCollection new.
	topView := StandardSystemView
		model: aFileList
		label: 'File List'
		minimumSize: 200 @ 200.
	patternView := CodeView
		on: aFileList
		aspect: #pattern
		change: #acceptPattern:from:
		menu: #patternMenu
		initialSelection: nil.
	topView addSubView: patternView in: (0@0 extent: 1@0.07) borderWidth: 1.
	patternView controller: AlwaysAcceptCodeController new.
	topView
		addSubView: (self listViewOn: aFileList)
		in: (0@0.07 extent: 1@0.23)
		borderWidth: 1.
	topView
		addSubView: (OnlyWhenSelectedCodeView
			on: aFileList
			aspect: #text
			change: #acceptText:from:
			menu: #textMenu
			initialSelection: nil
			selection: #isSelected)
		in: (0@0.3 extent: 1@0.7)
		borderWidth: 1.
	topView controller open
]

{ #category : '*Files-Fixes' }
FileList class >> openOnFileNames: aFileNameList label: labelString [
	"A File List on a list of names already gathered, with no pattern pane.
	 The list pane is the one `open' builds, for the same reason."

	| topView aFileList |
	aFileList := self new list: aFileNameList.
	topView := StandardSystemView
		model: aFileList
		label: labelString
		minimumSize: 200 @ 200.
	topView
		addSubView: (self listViewOn: aFileList)
		in: (0@0 extent: 1@0.2)
		borderWidth: 1.
	topView
		addSubView: (CodeView
			on: aFileList
			aspect: #text
			change: #acceptText:from:
			menu: #textMenu
			initialSelection: nil)
		in: (0@0.2 extent: 1@0.8)
		borderWidth: 1.
	topView controller open
]

{ #category : '*Files-Fixes' }
FileList class >> listViewOn: aFileList [
	"The list pane, which both windows open the same way.

	 `initialSelection' is the selector the view sends to ask which row to
	 highlight.  1983 passes nil, alone among every pluggable list in this
	 system, and a view that cannot ask keeps no selection: rebuilding the
	 rows highlights nothing and reports `fileName: nil' back, so the model
	 loses the file it was on every time #fileName is announced.

	 It answers a ROW, and so does the list: the view compares the two with
	 `=' and highlights where they match, and a directory's row carries the
	 mark that says it is one.  #fileName would be the name behind the row,
	 which is the same string for a file and a shorter one for a directory --
	 so a directory could be selected and never found again."

	^SelectionInListView
		on: aFileList
		aspect: #fileName
		change: #fileName:
		list: #listedNames
		menu: #fileListMenu
		initialSelection: #listedFileName
]

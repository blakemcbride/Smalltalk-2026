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
"
Extension { #name : 'FileList' }

{ #category : '*Files-Fixes' }
FileList >> enterDirectory: aName [
	"Make my list be the contents of that directory, and say so in the
	 pattern.  Nothing is selected in a directory just entered, so the text
	 pane has nothing to show and is left empty."

	myPattern := aName, '/*'.
	list := SortedCollection new.
	list addAll: (self filesMatching: myPattern).
	self setFileName: nil.
	isReading := false.
	self changed: #pattern.
	self changed: #fileName.
	self changed: #text
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

	| dir localName |
	self changeRequest ifFalse: [^self].
	fileName == nil ifFalse: [
		dir := FileDirectory
			directoryFromName: fileName
			setFileName: [:each | localName := each].
		(dir isDirectoryNamed: localName) ifTrue: [
			^Cursor read showWhile: [self enterDirectory: fileName]]].
	isReading := true.
	self changed: #text
]

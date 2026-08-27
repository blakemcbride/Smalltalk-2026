"
The editing keys move the caret and delete forward, everywhere text can be
edited.

Every text editor in the system is a ParagraphEditor of some kind -- the
browser's code pane, the workspace, the transcript, the inspector, the
debugger, the file list, the one-line pane a prompt puts up -- and all of
them read the keyboard through readKeyboard and dispatch on the Keyboard
table.  So this is one override rather than one per pane.

It is normalCharacter: rather than entries in that table.  Keyboard is
built by ParagraphEditor class>>initialize, forty lines of it, and adding
seven entries would mean copying the whole thing into this package and
keeping the copy in step forever.  Every code without an entry of its own
already arrives here -- `Keyboard atAllPut: #normalCharacter:' is the first
thing that initializer does -- so this is the same hook one layer down, and
it costs one array search per character typed.

The handlers answer true.  A handler that answers false leaves readKeyboard
to paste what has accumulated in the type-in stream when the loop ends, and
by then the caret has moved: the characters would land at the new place
instead of the one they were typed at.  So the type-in is finished here,
before the caret moves, and the loop is told there is nothing left to do.

Control widens a step rather than changing it -- a word instead of a
character, the whole text instead of the line -- and it is read off the
event rather than off the keyboard, so a burst of typeahead is decided by
what was held down as each key was struck.  ParagraphEditor>>again reads
the shift key the other way, from the hardware, because a menu command is
chosen at the moment the hardware is asked; a keystroke is not.
"
Extension { #name : 'ParagraphEditor' }

{ #category : '*Keyboard-Map' }
ParagraphEditor >> normalCharacter: characterStream [
	"1983 puts the character into the type-in stream.  An editing key is not a
	 character to type, so it is taken here instead."

	| which |
	which := InputSensor editingKeyCodes indexOf: sensor keyboardPeek asciiValue.
	which = 0 ifTrue: [
		characterStream nextPut: sensor keyboard.
		^false].
	^self editingKey: which typeIn: characterStream
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> editingKey: which typeIn: characterStream [
	"Carry out editing key number which -- its position in
	 InputSensor class>>editingKeyCodes.  Six of the seven move the caret;
	 the seventh takes a character away in front of it."

	which = 7 ifTrue: [^self deleteForward: characterStream].
	^self moveCursor: which typeIn: characterStream
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> moveCursor: which typeIn: characterStream [
	"Finish what has been typed so far and put the caret where cursor key
	 number which says.  Answers true; see the class comment above."

	| index control |
	control := sensor keyboardEventPeek hasCtrl.
	sensor keyboard.				"flush the cursor key"
	characterStream isEmpty ifFalse: [
		self replaceSelectionWith:
			(Text string: characterStream contents emphasis: emphasisHere).
		startBlock := stopBlock copy.
		characterStream reset].
	index := self cursorIndexFor: which control: control.
									"before closeTypeIn moves startBlock"
	self closeTypeIn.
	self selectAt: index.
	^true
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> deleteForward: characterStream [
	"Take away the character in front of the caret, or the selection if there
	 is one.  Answers true; see the class comment above.

	 The clipboard is deliberately not written.  Cut leaves what it took in
	 CurrentSelection because cutting is how text is moved; a delete key is
	 how text is got rid of, and 1983's backspace: does not write it either.

	 The range is worked out before closeTypeIn, which moves startBlock back
	 over whatever was just typed -- that is what makes a type-in undoable as
	 a whole, and it is not what is being deleted here."

	| start stop |
	sensor keyboard.				"flush the delete key"
	characterStream isEmpty ifFalse: [
		self replaceSelectionWith:
			(Text string: characterStream contents emphasis: emphasisHere).
		startBlock := stopBlock copy.
		characterStream reset].
	start := startBlock stringIndex.
	stop := stopBlock stringIndex.
	start = stop ifTrue: [stop := stop + 1].
	self closeTypeIn.
	stop > (paragraph text size + 1) ifTrue: [		"caret past the last character"
		self selectAt: start.
		^true].
	self deselect.
	startBlock := paragraph characterBlockForIndex: start.
	stopBlock := paragraph characterBlockForIndex: stop.
	self replaceSelectionWith: Text new.
	self selectAndScroll.
	self updateMarker.
	^true
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> cursorIndexFor: which control: control [
	"Where the caret goes for cursor key number which: 1 left, 2 right,
	 3 up, 4 down, 5 home, 6 end.  With control held, left and right step a
	 word rather than a character and home and end address the whole text
	 rather than the line.

	 A selection collapses to the end the key points at rather than moving off
	 it, which is what every editor since has done.  stopBlock's index is one
	 past the last selected character -- ParagraphEditor>>selection reads
	 `to: stopBlock stringIndex - 1' -- so it is already the place a caret
	 goes when the selection is dropped to the right.

	 Up and down ask the paragraph which character is a line away at the same
	 x.  It clamps: above the first line answers the first character and below
	 the last answers the last, so the ends are not a special case here."

	| grid |
	which = 1 ifTrue: [
		control ifTrue: [^self indexOfWordStartBefore: startBlock stringIndex].
		^startBlock stringIndex = stopBlock stringIndex
			ifTrue: [1 max: startBlock stringIndex - 1]
			ifFalse: [startBlock stringIndex]].
	which = 2 ifTrue: [
		control ifTrue: [^self indexOfWordStartAfter: stopBlock stringIndex].
		^startBlock stringIndex = stopBlock stringIndex
			ifTrue: [paragraph text size + 1 min: stopBlock stringIndex + 1]
			ifFalse: [stopBlock stringIndex]].
	which = 5 ifTrue: [
		control ifTrue: [^1].
		^self indexOfLineStartAt: startBlock stringIndex].
	which = 6 ifTrue: [
		control ifTrue: [^paragraph text size + 1].
		^self indexOfLineEndAt: stopBlock stringIndex].
	grid := paragraph textStyle lineGrid.
	which = 3 ifTrue: [
		^(paragraph characterBlockAtPoint:
			startBlock left @ (startBlock top - grid)) stringIndex].
	^(paragraph characterBlockAtPoint:
		stopBlock left @ (stopBlock top + grid)) stringIndex
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> indexOfWordStartBefore: index [
	"The beginning of the word the caret at index is in, or of the one before
	 it if the caret is already at a beginning.

	 A word is what Character>>tokenish calls one -- letter, digit or colon --
	 which is 1983's answer and the right one here: the colon is what makes
	 `at:put:' one word in the language this editor is mostly used to write.
	 ParagraphEditor>>backWord:, which control-w has always run, asks the same
	 question the same way."

	| text i |
	text := paragraph text.
	i := index - 1.
	[i > 0 and: [(text at: i) asCharacter tokenish not]] whileTrue: [i := i - 1].
	[i > 0 and: [(text at: i) asCharacter tokenish]] whileTrue: [i := i - 1].
	^i + 1
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> indexOfWordStartAfter: index [
	"The beginning of the next word after the caret at index, or the end of
	 the text if there is not one.

	 Forward is the mirror of backward and lands on a beginning too, so that
	 control-left and control-right walk the same stops in opposite
	 directions.  An editor that stopped at the END of each word going right
	 would visit places going one way that it never visits going the other."

	| text size i |
	text := paragraph text.
	size := text size.
	i := index.
	[i <= size and: [(text at: i) asCharacter tokenish]] whileTrue: [i := i + 1].
	[i <= size and: [(text at: i) asCharacter tokenish not]] whileTrue: [i := i + 1].
	^i
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> indexOfLineStartAt: index [
	"The first character of the line the caret at index is on.

	 The line is the DISPLAYED line and not the one the carriage returns
	 divide the text into: a Paragraph composes its text into a `lines' array
	 of intervals, wrapping where it must, and home on a wrapped line means
	 the left edge the user is looking at.  Up and down already answer for the
	 same lines, by geometry.

	 A text ending in a carriage return has one line more than the paragraph
	 composed one for: composeAll stops at the last character, so the empty
	 line the return opens has no interval and the caret standing on it is
	 already at both of its ends.  Without that guard the last line's
	 interval would answer, and home would jump the caret up a line."

	| text |
	text := paragraph text.
	text size = 0 ifTrue: [^1].
	(index > text size and: [(text at: text size) asCharacter = Character cr])
		ifTrue: [^index].
	^(paragraph lineAt: (paragraph lineIndexOfCharacterIndex: index)) first
]

{ #category : '*Keyboard-Map' }
ParagraphEditor >> indexOfLineEndAt: index [
	"The place after the last character of the line the caret at index is on.

	 A line interval includes the character it broke on, and there are three
	 kinds.  A line ended by a carriage return holds it, and the caret goes
	 BEFORE it: after the return is the next line.  A line wrapped at a space
	 holds the space, and the caret goes before that too, or end would put it
	 on the line below.  A line that ran out of text, or wrapped in the middle
	 of a word with no space to wrap at, ends on a character the user can see,
	 and the caret goes after it.

	 The empty line a final carriage return opens is the fourth kind and has
	 no interval at all -- see indexOfLineStartAt:, which guards it the same
	 way and for the same reason."

	| text last there |
	text := paragraph text.
	text size = 0 ifTrue: [^1].
	(index > text size and: [(text at: text size) asCharacter = Character cr])
		ifTrue: [^index].
	last := (paragraph lineAt: (paragraph lineIndexOfCharacterIndex: index)) last.
	there := (text at: last) asCharacter.
	(there = Character cr or: [there = Character space])
		ifTrue: [^last].
	^last + 1
]

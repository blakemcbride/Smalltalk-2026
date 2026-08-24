"
The cursor keys move the caret, everywhere text can be edited.

Every text editor in the system is a ParagraphEditor of some kind -- the
browser's code pane, the workspace, the transcript, the inspector, the
debugger, the file list, the one-line pane a prompt puts up -- and all of
them read the keyboard through readKeyboard and dispatch on the Keyboard
table.  So this is one override rather than one per pane.

It is normalCharacter: rather than an entry in that table.  Keyboard is
built by ParagraphEditor class>>initialize, forty lines of it, and adding
four entries would mean copying the whole thing into this package and
keeping the copy in step forever.  Every code without an entry of its own
already arrives here -- `Keyboard atAllPut: #normalCharacter:' is the first
thing that initializer does -- so this is the same hook one layer down, and
it costs one array search per character typed.

The handler answers true.  A handler that answers false leaves readKeyboard
to paste what has accumulated in the type-in stream when the loop ends, and
by then the caret has moved: the characters would land at the new place
instead of the one they were typed at.  So the type-in is finished here,
before the caret moves, and the loop is told there is nothing left to do.
"
Extension { #name : 'ParagraphEditor' }

{ #category : '*Cursor-Keys' }
ParagraphEditor >> normalCharacter: characterStream [
	"1983 puts the character into the type-in stream.  A cursor key is not a
	 character to type, so it is taken here instead."

	| which |
	which := InputSensor cursorKeyCodes indexOf: sensor keyboardPeek asciiValue.
	which = 0 ifTrue: [
		characterStream nextPut: sensor keyboard.
		^false].
	^self moveCursor: which typeIn: characterStream
]

{ #category : '*Cursor-Keys' }
ParagraphEditor >> moveCursor: which typeIn: characterStream [
	"Finish what has been typed so far and put the caret where cursor key
	 number which says.  Answers true; see the class comment above."

	| index |
	sensor keyboard.				"flush the cursor key"
	characterStream isEmpty ifFalse: [
		self replaceSelectionWith:
			(Text string: characterStream contents emphasis: emphasisHere).
		startBlock := stopBlock copy.
		characterStream reset].
	index := self cursorIndexFor: which.	"before closeTypeIn moves startBlock"
	self closeTypeIn.
	self selectAt: index.
	^true
]

{ #category : '*Cursor-Keys' }
ParagraphEditor >> cursorIndexFor: which [
	"Where the caret goes for cursor key number which: 1 left, 2 right,
	 3 up, 4 down.

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
		^startBlock stringIndex = stopBlock stringIndex
			ifTrue: [1 max: startBlock stringIndex - 1]
			ifFalse: [startBlock stringIndex]].
	which = 2 ifTrue: [
		^startBlock stringIndex = stopBlock stringIndex
			ifTrue: [paragraph text size + 1 min: stopBlock stringIndex + 1]
			ifFalse: [stopBlock stringIndex]].
	grid := paragraph textStyle lineGrid.
	which = 3 ifTrue: [
		^(paragraph characterBlockAtPoint:
			startBlock left @ (startBlock top - grid)) stringIndex].
	^(paragraph characterBlockAtPoint:
		stopBlock left @ (stopBlock top + grid)) stringIndex
]

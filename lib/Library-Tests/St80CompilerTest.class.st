"
What a `do it' in a text pane is given, and where its complaint goes.

A workspace hands the compiler ParagraphEditor>>selectionAsStream, which is a
ReadStream on the WHOLE pane bounded to the selection -- `on: text from:
startBlock stringIndex to: stopBlock stringIndex - 1'.  Nothing is copied: the
selection is a position and a read limit.  So the two questions a compiler has
to ask of it are where it starts and where it ends, and PositionableStream>>
contents answers only the second.

These tests ask both, and they ask the end-to-end question too: evaluating a
stream positioned partway through a text must answer what THAT part says and
must report a mistake at a character inside it.

The test case is its own requestor.  A requestor is anything that answers
insertAndSelect:at:, which is how the complaint reaches an editor's text, and
implementing it here is cheaper than a class that exists to be implemented
once.
"
Class {
	#name : 'St80CompilerTest',
	#superclass : 'TestCase',
	#instVars : [ 'noticed', 'noticedAt' ],
	#category : 'Library-Tests'
}

{ #category : 'requestor' }
St80CompilerTest >> insertAndSelect: aString at: anInteger [
	"The requestor protocol: an editor puts the complaint into its text here."

	noticed := aString.
	noticedAt := anInteger
]

{ #category : 'source' }
St80CompilerTest >> testSourceTextOfAReadStreamBeginsAtItsPosition [
	"`contents' answers from the first character whatever the position, so
	 the text of a selection has to be taken from the position instead."

	| text |
	text := 'above the selection. 3 + 4'.
	self assert:
		(Compiler sourceTextOf: (ReadStream on: text from: 22 to: text size))
			= '3 + 4'
]

{ #category : 'source' }
St80CompilerTest >> testSourceTextOfAStringIsItself [
	self assert: (Compiler sourceTextOf: '3 + 4') = '3 + 4'
]

{ #category : 'source' }
St80CompilerTest >> testSourceTextOfAReadStreamIsRepeatable [
	"A failed compile asks for the text a second time to say where it went
	 wrong, so taking it must not consume the stream."

	| stream |
	stream := ReadStream on: 'above the selection. 3 + 4' from: 22 to: 26.
	self assert: (Compiler sourceTextOf: stream) = (Compiler sourceTextOf: stream)
]

{ #category : 'source' }
St80CompilerTest >> testSourceOffsetOfAReadStreamIsItsPosition [
	self assert:
		(Compiler sourceOffsetOf: (ReadStream on: 'abcdef' from: 4 to: 6)) = 3.
	self assert: (Compiler sourceOffsetOf: 'abcdef') = 0
]

{ #category : 'do it' }
St80CompilerTest >> testDoItSeesOnlyTheSelection [
	"Everything above the selection is out of scope, whether it would have
	 compiled or not -- so a do it neither fails on it nor runs it again."

	| head tail text |
	head := 'St80CompilerTestNotAName 42. '.
	tail := '3 + 4'.
	text := head , tail.
	self assert:
		(Compiler new
			evaluate: (ReadStream on: text from: head size + 1 to: text size)
			in: nil
			to: nil
			notifying: nil
			ifFail: [#failed]) = 7
]

{ #category : 'do it' }
St80CompilerTest >> testDoItReportsTheMistakeInsideTheSelection [
	"The C compiler counts from the start of the text it was given and the
	 editor counts from the start of the pane, so the offset of the selection
	 has to be added on the way back."

	| head tail text answer |
	head := 'these words are not selected. '.
	tail := '3 + )'.
	text := head , tail.
	noticed := noticedAt := nil.
	answer := Compiler new
		evaluate: (ReadStream on: text from: head size + 1 to: text size)
		in: nil
		to: nil
		notifying: self
		ifFail: [#failed].
	self assert: answer == #failed.
	self assert: noticed notNil.
	self assert: noticedAt > head size.
	self assert: noticedAt <= (text size + 1)
]

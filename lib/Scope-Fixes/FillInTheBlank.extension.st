Extension { #name : 'FillInTheBlank' }

{ #category : '*Scope-Fixes' }
FillInTheBlank class >> request: messageString initialAnswer: aString [
	"Create an instance of me whose question is messageString.
	Display it centered around the cursor.
	Supply aString as an initial answer.
	Simply return whatever the user accepts.

	The one of these eight that a user meets: `response' was the action
	block's parameter and the answer was read from a global of the same
	name, so this prompt returned nil whatever was typed into it.
	FileList>>renameFile is a sender, which is why renaming a file did
	nothing."

	| response |
	self
		request: messageString
		displayAt: Sensor cursorPoint
		centered: true
		action: [:answer | response := answer]
		initialAnswer: aString.
	^response
]

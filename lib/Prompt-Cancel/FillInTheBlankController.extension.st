"
A prompt can be given up on.

A FillInTheBlank is the strongest kind of modal this system has.  Its top
view's controller answers `^model actionTaken not' to isControlActive, so
control cannot leave the prompt for the window underneath no matter where
the pointer goes, and nothing but accept ever sets actionTaken.  One way in,
one way out.

The way out is not the one the pane offers.  The type-in pane is a
StringHolderController, so its middle-button menu is the code pane's --
again, undo, copy, cut, paste, do it, print it, accept, cancel -- and the
last item reads exactly like the way out.  It is not.
ParagraphEditor>>cancel restores the text to what it was when the editor was
initialized and carries straight on editing; on a prompt that opened empty
that is the empty text already there, so choosing it changes nothing on the
screen and the prompt stays up.  From the chair, cancel does nothing and the
prompt cannot be dismissed.

There is a way out, and it is that a prompt with nothing in it means `never
mind': every caller in the system tests the answer for empty and gives up on
one.  But it is a gesture rather than a command -- put the field back to
empty, then carriage return -- and a user who does not already know it is
stuck in front of a menu item that says the right word and does the wrong
thing.

So cancel cancels.  It empties the answer and sets actionTaken, which is
precisely accepting an empty answer: the action block runs with '', every
caller reaches the give-up branch it already has, and the modal loop unwinds
through the same path that accept unwinds through.  Nothing downstream has
to learn a new answer, because '' is the answer the system already spells
`never mind'.

The escape key does the same thing.  1983 gave ESC to
ParagraphEditor>>selectCurrentTypeIn:, which makes the characters just typed
the selection so that the next keystroke replaces them.  That is a
reasonable thing to want in a code pane and a strange thing to want in a
one-line prompt, and Esc is the key a hand reaches for to get out of a
dialog, so here it is the key that gets out.  Only prompts are affected:
FillInTheBlankController is used for nothing else.
"
Extension { #name : 'FillInTheBlankController' }

{ #category : '*Prompt-Cancel' }
FillInTheBlankController >> cancel [
	"Give up on the prompt rather than on the type-in.

	 Throwing the answer away and setting actionTaken is the same ending
	 accept has, which is what keeps the unwinding in one place: the pane's
	 control loop stops, controlTerminate closes a scheduled prompt window if
	 there is one, and the action block is evaluated with the empty answer
	 that every caller reads as `never mind'.

	 unlockModel matches accept's, so a prompt that was typed into is not
	 left holding a lock nobody will clear."

	model contents: ''.
	self unlockModel.
	model setAction: true
]

{ #category : '*Prompt-Cancel' }
FillInTheBlankController >> selectCurrentTypeIn: characterStream [
	"The escape key gives the prompt up.

	 What has been typed is dropped with the rest of the answer, so the
	 type-in stream is not written back; answering true tells readKeyboard so,
	 and it returns without pasting what it has accumulated into a pane that
	 is about to be erased."

	sensor keyboard.				"flush the escape"
	self cancel.
	^true
]

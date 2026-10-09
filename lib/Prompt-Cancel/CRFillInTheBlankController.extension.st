"
Return at a prompt takes what is in the field, deleted characters included.
"
Extension { #name : 'CRFillInTheBlankController' }

{ #category : '*Prompt-Cancel' }
CRFillInTheBlankController >> cr: characterStream [
	"The carriage return was typed by the user.  This designates that the
	 receiver should give up control.

	 The selection is replaced by the type-in whether or not anything was
	 typed.  1983 replaced it only for a non-empty type-in, and a
	 Backspace with nothing typed yet only widens the selection over the
	 character it deletes -- the replacement comes when the type-in
	 closes.  A Return already queued behind the Backspace reached here
	 first, found the stream empty, and accepted the field with the
	 deleted default still in it: Backspace then Return typed together
	 saved under the name the user had just erased (Bugs6 GUI-6)."
	sensor keyboard.	"gobble cr"
	self replaceSelectionWith:
		(Text string: characterStream contents emphasis: emphasisHere).
	self accept
]

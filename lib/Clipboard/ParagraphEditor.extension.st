"
The three editor commands that touch the clipboard, made to touch the
system's as well as the image's.

copySelection and cut are 1983's bodies with one line added: the selection
goes to Clipboard as well as into CurrentSelection.  paste is 1983's body
with one question in front: if the system's clipboard holds text, and it
is not the text the image itself put there last, that is what pastes --
somebody copied it elsewhere since -- and otherwise CurrentSelection is,
emphasis and all.  So copying inside the image and pasting inside it keeps
its bold and italic, and copying outside and pasting inside brings the
outside text; and the image never pastes something older than what the
desktop has.

These supersede the 1983 methods of the same names, as Cursor-Keys and
Scroll-Wheel supersede theirs; the class variables they use are
ParagraphEditor's own.
"
Extension { #name : 'ParagraphEditor' }

{ #category : '*Clipboard' }
ParagraphEditor >> copySelection [
	"Copy the current selection into the shared buffer, and onto the
	 system's clipboard."

	CurrentSelection := UndoSelection := self selection.
	Clipboard text: CurrentSelection
]

{ #category : '*Clipboard' }
ParagraphEditor >> cut [
	"Cut out the current selection, redisplay the paragraph if necessary,
	 and put what was cut on the system's clipboard."

	self deselect.
	self closeTypeIn.
	self replaceSelectionWith: Text new.
	self selectAndScroll.
	self updateMarker.
	CurrentSelection := UndoSelection copy.
	Clipboard text: CurrentSelection
]

{ #category : '*Clipboard' }
ParagraphEditor >> paste [
	"Paste over the current selection: what the system's clipboard holds
	 when that is something the image did not put there itself, else the
	 shared buffer.  Redisplay if necessary."

	| outside |
	outside := Clipboard text.
	(outside notNil and: [outside notEmpty and: [outside ~= CurrentSelection asString]])
		ifTrue: [CurrentSelection := outside asText].
	self deselect.
	self closeTypeIn.
	self replaceSelectionWith: CurrentSelection.
	self selectAndScroll.
	self updateMarker
]

"
ParagraphEditor overrides controlActivity and does not call super, so the
wheel has to be asked about here as well.

It is the same three lines as ScrollController's, with the keyboard and the
buttons where the superclass sends to super -- which is the whole reason
this cannot be inherited.  Every text view in the system is a
ParagraphEditor of some kind, so leaving it out would mean the wheel
scrolled lists and nothing else.
"
Extension { #name : 'ParagraphEditor' }

{ #category : '*Scroll-Wheel' }
ParagraphEditor >> controlActivity [
	"1983's, with the wheel asked about first."

	self scrollByWheel ifTrue: [^self].
	self scrollBarContainsCursor
		ifTrue:
			[self scroll]
		ifFalse:
			[self processKeyboard.
			self processMouseButtons]
]

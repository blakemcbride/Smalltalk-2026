"
The wheel scrolls whatever the pointer is over.

A scroll bar in this system is not furniture on the edge of a window: it is
drawn when a controller takes control and taken away again when it gives it
up, and it is driven by holding a button down inside it.  Which means the
view under the pointer is already the view whose controller is running, and
its control loop is already spinning on `Processor yield'.  So the wheel
needs no route of its own through the input ring and no view to be told
about it -- the controller that would have read the scroll bar reads the
wheel instead, at the top of the activity it was going to do anyway.

Sign follows the window system's: away from the user is positive, and
positive scrolls toward the beginning, which is what View>>scrollBy: does
for a positive y -- it translates the window by the negation, so the window
moves back over the document.  ScrollController>>scrollView: clamps it to
the document at both ends, so a notch at the top or the bottom does nothing
rather than something wrong.

Three lines a notch, measured in the default text style's line grid, because
that is the grid everything in this interface is laid out on -- lists fix
their line height from it at class-initialisation time and paragraphs
compose to it.
"
Extension { #name : 'ScrollController' }

{ #category : '*Scroll-Wheel' }
ScrollController >> controlActivity [
	"1983's, with the wheel asked about first."

	self scrollByWheel ifTrue: [^self].
	self scrollBarContainsCursor
		ifTrue: [self scroll]
		ifFalse: [super controlActivity]
]

{ #category : '*Scroll-Wheel' }
ScrollController >> scrollByWheel [
	"Scroll by however many notches the wheel has turned since this last
	 asked.  Answer whether anything moved: nothing turned, or nothing to
	 scroll, and the activity this interrupted should carry on."

	| notches |
	notches := sensor wheelDelta.
	notches = 0 ifTrue: [^false].
	self canScroll ifFalse: [^false].
	self scrollView: notches * self wheelScrollAmount.
	self moveMarker.
	^true
]

{ #category : '*Scroll-Wheel' }
ScrollController >> wheelScrollAmount [
	"How far one notch scrolls, in the view's own coordinates."

	^TextStyle default lineGrid * 3
]

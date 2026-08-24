"
A confirm insists by staying up, not by strobing.

BinaryChoiceController>>isControlActive never answers false while the choice
still stands, and it spends the wait flashing:

	isControlActive
		model actionTaken ifTrue: [^false].
		[super isControlActive] whileFalse: [view flash].
		^true

`super isControlActive' is `the pointer is inside my view', so the loop runs
the whole time the pointer is somewhere else, and the comment on
BinaryChoiceView class>>openOn: says what it is for: insist that the user
respond.

Two things make that come out wrong here rather than in 1983.

The first is where the class is used.  BinaryChoiceView
class>>buildSwitchesFor:width: gives the same controller to the plain View
holding the two switches, and it has to -- the actionTaken test is how
control unwinds back out of the switches once one has been picked -- but the
flash comes with it, and for that view `inside my view' means `on the yes/no
row'.  So the switches flashed whenever the pointer was anywhere else,
including over the question being asked.  A subview whose isControlActive
cannot answer false cannot hand control back either, so it went on flashing
until the pointer returned to it.

The second is what a flash costs now.  DisplayScreen>>flash: complements the
rectangle, waits 60 ms, complements it back and waits again, so the loop
above is a half-second-on, half-second-off strobe at full contrast for as
long as the pointer is away.  On a machine whose screen was the Form itself
that was a blip; driven by a 60 ms delay against a window that is uploaded
and held, it is a strobe -- and a strobing dialog is harder to read and act
on than a still one, which is the opposite of insisting.

So the insisting is left to the thing that already does it: the dialog is
modal, it holds control until it is answered, and it does not move while it
waits.  The actionTaken exit is untouched, so yes and no dismiss it exactly
as before.
"
Extension { #name : 'BinaryChoiceController' }

{ #category : '*Confirm-Nag' }
BinaryChoiceController >> isControlActive [
	"Keep control until the choice has been made.

	 Answering true regardless of where the pointer is what makes the dialog
	 modal: control cannot leave it for the browser underneath, and the
	 switches keep displaying their labels instead of complementing on their
	 own."

	^model actionTaken not
]

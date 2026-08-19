"
The caret points where the next character will appear.  1983's does not.

Text class>>initialize builds CaretForm with `offset: 8@0', and
Form>>displayOn:at:clippingBox:rule:mask: ADDS the offset to the point it is
given -- `at: aDisplayPoint + offset'.  So the sixteen-wide form's left edge
lands eight pixels RIGHT of the insertion point, and its ink is a small
up-arrow whose tip sits at columns 7 and 8 of that form, so the tip lands
fifteen pixels right of the place the next character will be drawn.

Measured, before this: type HELLO and the O ends at x=216, the caret tip is
at x=233, and the X you type next appears at x=218.  At this face that is a
character and a half, which is how Blake reported it.

The sign is the whole of the fault.  Because displayOn:at: adds, a hot spot
INSIDE the form has to be written as a negative, and every other one in the
family is: Cursor class>>initialize has -2@-2, -5@-7, -7@-7, -8@-8 and 0@-7,
and 0@0 where the hot spot really is the top left.  8@0 is the only positive
offset anywhere in that family and it is the only one that misbehaves.
-8@0 puts the tip on the insertion point -- the same -8 that the other
sixteen-wide cursor uses to centre itself.

sources/ is never edited, so this corrects the value after the fact rather
than at the line that sets it.  It goes in Paragraph class>>initialize
because that initializer already exists for this class and needs no new
call site, and because the bootstrap runs initializers in load order:
Text is line 23 of the manifest and Paragraph is line 49, so this runs after
the value it corrects on both of the bootstrap's two passes.  Correcting
rather than replacing also means the 100-line literal in Text class>>
initialize is not duplicated here, where it would rot.

CaretForm is reached as a pool variable -- TextConstants is one of
Paragraph's poolDictionaries -- so mutating the Form the pool already holds
is what every reference sees, and there is no second copy to keep in step.

The bluebook profile does not load lib/, so the museum piece keeps 1983's
caret and trace2 is untouched.
"
Extension { #name : 'Paragraph' }

{ #category : '*Graphics-Fixes' }
Paragraph class >> initialize [
	"1983's one line, and then the caret."
	DefaultCompositionRectangle := 0@0 corner: 10000@10000.
	self repairCaretOffset
]

{ #category : '*Graphics-Fixes' }
Paragraph class >> repairCaretOffset [
	"Turn the caret's offset into the hot spot it was meant to be.

	 Guarded on the sign rather than assigning unconditionally, so that
	 running the initializers twice -- which the bootstrap does -- cannot
	 negate it back."
	| caret |
	caret := TextConstants at: #CaretForm ifAbsent: [^self].
	caret offset x > 0 ifTrue: [
		caret offset: caret offset x negated @ caret offset y]
]

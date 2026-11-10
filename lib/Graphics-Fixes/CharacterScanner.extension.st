"
CharacterScanner>>setStopConditions, into a table of the scanner's own.

The table the scanning loop stops on -- 258 slots, one per character code
and two past them for EndOfRun and CrossedX -- says which character makes
the scanner stop and what it then sends itself.  1983 kept one in each
StrikeFont and had every scanner write its own selectors straight into it:
`stopConditions _ font stopConditions' and then eight at:put:s, after which
each subclass set the space slot its own way.  They do not agree about that
slot.  CompositionScanner puts #space there, because a space is where it
records that a line may break; DisplayScanner and CharacterBlockScanner put
nil there (or #paddedSpace for justified text), because to them a space is
only a character to draw or step over.  Each re-set the slot at the start
of every run, and with one process scanning at a time that was enough.

Every Paragraph of a style shares the style's fonts, so every scanner in the
image was writing ONE Array.  Under `-serve -workers N' two processes scan
at once, and each finds the other's selectors in it (Bugs07 GUI-3).  A
composer that finds nil in the space slot never records a space and breaks
the line wherever the width runs out, mid-word, and one that finds nil
after the scan stopped on #space sends `perform: nil'; a displayer that
finds #space sends #space, which DisplayScanner does not implement.  On
eight workers, three compositions in four came out with the wrong line
breaks and some three thousand displays died, against none on one worker.
Nothing about it is a class variable, so the audit of class variables
recorded in doc/CONCURRENCY.md could not see it: the shared state was an
instance variable of a font every text in the image reaches.

So the table is the scanner's.  The first run copies the font's table into
a new Array, and every later run refills that same Array from whichever
font the run is in, so a run boundary costs a copy of 258 slots and no
allocation, and a font change mid-paragraph -- bold, italic, another size
-- still starts from that font's own marks (the characters it cannot draw,
which the bootstrap marks #characterNotInFont).  The subclasses' writes to
the space slot then land in the copy, which is why nothing below them had
to change.  The font's table is now only ever read.

A scanner is one process's object -- a Paragraph makes one per composition,
display or hit test, and a Paragraph shared between processes is shared by
contract, as a WriteStream is -- so a table owned by the scanner is owned by
one process.

The test against the font's own table is for a scanner older than this
method: one made before lib was loaded, or saved holding a font's table,
must not refill the font's Array in place, which would be the same fault
again.  It gets a copy like a new scanner does.
"
Extension { #name : 'CharacterScanner' }

{ #category : '*Graphics-Fixes' }
CharacterScanner >> setStopConditions [
	"Set default stop conditions for the font, in a table that belongs to
	 this scanner rather than to the font; see the comment above."

	| fontStops |
	spaceWidth := font widthOf: Space.
	sourceForm := font glyphs.
	xTable := font xTable.
	height := font height.
	fontStops := font stopConditions.
	(stopConditions isNil
		or: [stopConditions == fontStops
		or: [stopConditions size ~= fontStops size]])
			ifTrue: [stopConditions := fontStops copy]
			ifFalse: [stopConditions
					replaceFrom: 1 to: fontStops size
					with: fontStops startingAt: 1].
	stopConditions at: Space asInteger + 1 put: #space.
	stopConditions at: Tab asInteger + 1 put: #tab.
	stopConditions at: CR asInteger + 1 put: #cr.
	stopConditions at: EndOfRun put: #endOfRun.
	stopConditions at: CrossedX put: #crossedX.

	stopConditions at: Ctrls asInteger + 1 put: #onePixelSpace.
	stopConditions at: CtrlS asInteger + 1 put: #onePixelBackspace.
	stopConditions at: Ctrlz asInteger + 1 put: #characterNotInFont
]

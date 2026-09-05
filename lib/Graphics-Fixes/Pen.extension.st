"
Pen>>direction:, the setter for the heading the accessor already reads.

1983 gives a Pen three ways to change where it is pointing -- `turn:' by a
relative amount, `north' for the one absolute heading it bothered to name,
and nothing else -- while `direction' reads the value out.  Setting it
absolutely meant `p turn: degrees - p direction', which is the sort of
arithmetic a turtle library exists to spare you.

The manual has documented `direction:' beside `go:', `turn:' and `north'
since chapter 16 was written; the image answered MessageNotUnderstood
(Bugs4 DOCS-4).  Of the two ways to make the manual right, adding the
method is the one that leaves the reader with what the manual promised.

Degrees, and the same degrees `turn:' and `direction' use: 0 points right,
90 down, 270 up -- which is what `north' sets, and why the accessor's own
1983 comment saying 0 is toward the top of the screen has never been true.
Normalised modulo 360 exactly as `turn:' normalises, so `p direction: -90'
and `p north' leave the same Pen.
"
Extension { #name : 'Pen' }

{ #category : '*Graphics-Fixes' }
Pen >> direction: degrees [
	"Set the direction the receiver faces, in degrees, absolutely.  See the
	 comment above."

	direction := degrees \\ 360
]

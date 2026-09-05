"
Point>>hash, so that points along a line spread across a table.

1983's is `(x hash bitShift: 2) bitXor: y hash'.  For `i @ i' that is 4i
xor i, which is distinct enough; for `i @ 0' it is 4i, every value a
multiple of four, so a Set whose capacity is a power of two -- which
1983's Set always is -- uses a quarter of its slots and probes through the
rest.  And Rectangle>>hash was built on it, xor-ing an origin and a corner
that move together, so five thousand rectangles of the shape
`i @ i corner: (i + 5) @ (i + 7)' hashed to 58 values and took thirty-nine
seconds to put in a Set (Bugs3 B33).

Multiply-and-add rather than shift-and-xor: x's hash times an odd
constant plus y's, masked to thirty-two bits.  The odd multiplier keeps
every bit of x alive in the sum, the addition of y keeps x and y from
cancelling when they are equal, and the mask keeps the answer the width of
every other hash in the library.  Each coordinate's hash is masked to
thirty-two bits first, because a hash may be negative (`-5 hash' is -5) and
because a SmallInteger's hash is itself and may be sixty-two bits wide.
Coordinates that are Floats get Float>>hash, which agrees with the Integer
hash for an integral Float, so `1 @ 2' and `1.0 @ 2.0' -- which are equal --
hash the same.

Point>>theta is also here, for an unrelated reason: 1983 answers a printed
five-digit constant on the two axes and a full precision arcTan everywhere
else.  Its own comment carries that story.
"
Extension { #name : 'Point' }

{ #category : '*Graphics-Fixes' }
Point >> hash [
	"See the comment above."
	^((x hash bitAnd: 16rFFFFFFFF) * 1664525 + (y hash bitAnd: 16rFFFFFFFF))
		bitAnd: 16rFFFFFFFF
]

{ #category : '*Graphics-Fixes' }
Point >> theta [
	"Answer the angle the receiver makes with the origin, in radians.
	 Right is 0, down is pi/2.

	 1983's version answers `arcTan' off the axes and a five-digit decimal
	 literal on them: `1.5708' for a point straight down and `4.71239' for
	 one straight up, with the exact values written beside them in comments
	 as `90.0 degreesToRadians' and `270.0 degreesToRadians'.  Those
	 literals are what an Alto's printed table gave, and on a machine with
	 doubles they are simply wrong: `(0@1) theta' came back 3.67e-6 short of
	 pi/2, while `(1@1) theta' is correct to the last bit.  So the answer
	 was accurate everywhere except the two directions a person is most
	 likely to ask about, and `(0@1) theta * 2 = (0@1) theta + (0@1) theta'
	 held while `(0@1) theta = ((0@1) theta cos arcCos)' did not.

	 Same branches, same answers to five digits, the constants computed
	 instead of quoted.  Nothing that rounds an angle to a pixel can see the
	 difference; anything that compares one to a computed right angle now
	 can.  `(0@0) theta' still answers pi/2, which is 1983's reading of an
	 angle that has none -- a point at the origin makes no angle, and there
	 is no better answer to invent for it here."

	| tan theta |
	x = 0 ifTrue: [
		^y >= 0
			ifTrue: [Float pi / 2]
			ifFalse: [Float pi * 3 / 2]].
	tan := y asFloat / x asFloat.
	theta := tan arcTan.
	x >= 0 ifFalse: [^Float pi + theta].
	^y >= 0
		ifTrue: [theta]
		ifFalse: [Float pi * 2 + theta]
]

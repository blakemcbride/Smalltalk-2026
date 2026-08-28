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
"
Extension { #name : 'Point' }

{ #category : '*Graphics-Fixes' }
Point >> hash [
	"See the comment above."
	^((x hash bitAnd: 16rFFFFFFFF) * 1664525 + (y hash bitAnd: 16rFFFFFFFF))
		bitAnd: 16rFFFFFFFF
]

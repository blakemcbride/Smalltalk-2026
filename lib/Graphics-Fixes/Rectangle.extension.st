"
Rectangle>>hash, with a shift between the origin and the corner.

1983's is `origin hash bitXor: corner hash', and a rectangle's corner moves
with its origin: for `i @ i corner: (i + 5) @ (i + 7)' the two hashes
differ in a handful of low bits and the xor cancels the rest, so five
thousand such rectangles produced 58 distinct hashes and thirty-nine
seconds in a Set (Bugs3 B33).  Rectangles are what a window system keys
by -- damage lists, clipping, hit testing -- and every one of them is a
pair of points that move together.

Shifting the origin's hash before the xor puts it in different bits from
the corner's, so the two can no longer cancel; five is enough to clear the
low bits that nearby points differ in and small enough that the answer
stays a SmallInteger for any thirty-two bit Point hash.  Point>>hash in
this package is the other half of the repair.
"
Extension { #name : 'Rectangle' }

{ #category : '*Graphics-Fixes' }
Rectangle >> hash [
	"See the comment above."
	^(origin hash bitShift: 5) bitXor: corner hash
]

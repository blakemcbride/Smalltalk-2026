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

Rectangle class>>fromUser: is also here, for an unrelated reason: it waits
for a mouse button, and headless there is nobody to press one.  Its own
comment carries that story, and InputSensor>>waitButton in this package
carries the rule the two of them follow.
"
Extension { #name : 'Rectangle' }

{ #category : '*Graphics-Fixes' }
Rectangle >> hash [
	"See the comment above."
	^(origin hash bitShift: 5) bitXor: corner hash
]

{ #category : '*Graphics-Fixes' }
Rectangle class >> fromUser: gridPoint [
	"Answer an instance of me that the user designates by dragging out its
	 two corners, gridded by gridPoint.

	 The 1983 body is below unchanged; what is new is the first line.  Its
	 two loops wait on `Sensor redButtonPressed', and headless that is a
	 button nothing can press: `st80 -serve img 'Rectangle fromUser'' spun
	 until the process was killed, and `Form fromUser' -- which is
	 `Rectangle fromUser:' followed by `fromDisplay:' -- did the same
	 (Bugs4, section 11).  Unlike Sensor waitButton this one never sends a
	 wait method, so it needs the guard in its own right.

	 InputSensor>>waitButton in this package carries the reasoning for
	 raising an Error rather than resuming with something invented."

	| originPoint newSelf minCorner oldCursor |
	self canOpenUserInterface ifFalse: [
		^Error new signal:
			'no display: fromUser would wait forever for a rectangle nobody can drag out'].
	oldCursor := Sensor currentCursor.
	Cursor cursorLink: false.
	originPoint := Sensor cursorPoint grid: gridPoint.
	[Sensor redButtonPressed] whileFalse: [
		Processor yield.
		Cursor origin showGridded: gridPoint.
		originPoint := Sensor cursorPoint grid: gridPoint].
	newSelf := self new origin: originPoint.
	minCorner := originPoint + gridPoint.
	Sensor cursorPoint: minCorner.
	[Sensor redButtonPressed] whileTrue: [
		Processor yield.
		Cursor corner showGridded: gridPoint.
		newSelf corner: ((Sensor cursorPoint grid: gridPoint) max: minCorner).
		Display flash: newSelf].
	Cursor cursorLink: true.
	oldCursor show.
	^newSelf
]

"
BitBlt>>drawLoopX:Y:, clipped before it runs instead of during.

This is Bresenham, and it is where every line in the system is drawn: Pen
go: and goto:, Line and Curve and Spline displayOn:, and the border of
every rectangle.  1983's loop steps once per pixel of the LONGER coordinate
span and calls copyBits at every step, and copyBits clips -- so a step
outside the clipping rectangle costs a whole blit setup, a clip, and a
decision to write nothing.  That is the right shape when the line is the
size of the screen.  It is not the right shape when it is not: `Pen new go:
1000000000' is a billion clipped blits on one worker, minutes of a core to
draw nothing, and `Line from: 0@0 to: 1000000000@1000000000 withForm: f'
displayed on a 640-pixel Display is the same (Bugs4, section 11).  Neither
is contrived -- a coordinate arrives from a computation, and the difference
between a line that leaves the screen and a line that leaves the screen a
million times over is invisible in the source.

Two bounds go in front of the loop.

The first is the whole line's bounding box against the clipping rectangle.
A line that cannot touch the clip at any point draws nothing, and now costs
nothing rather than one blit per pixel of its length.

The second is the step range.  The loop's major coordinate moves by exactly
one pixel per step and never turns back, so the steps at which the nib can
overlap the clipping rectangle are a contiguous run, and its two ends are
arithmetic: drawLoopFirst:step:nib:clipAt:extent:count: answers them.  The
steps before that run are skipped by advancing the loop's state in closed
form -- the minor coordinate moves once per wrap of the Bresenham
potential, and after n steps the number of wraps is
`((n * decrement - potential) / period) ceiling', which is the same integer
the loop would have counted one step at a time.  So the pixels drawn are
the pixels 1983 draws, one for one; only the ones that were going to be
clipped away are not attempted.  Work is now bounded by the size of the
clipping rectangle instead of by the length of the line.

What is not preserved is where destX and destY are left afterwards: the
loop stops at the last visible step rather than at the end of the line.
Nothing reads them -- drawFrom:to: sets them, Pen>>goto: keeps its own
location, and Form>>drawLine: throws the BitBlt away -- and a Bresenham
loop already leaves them wherever it happened to stop.

The `primitive: 104' pragma is kept because it is the seam: this VM does
not implement 104, so the Smalltalk below is what runs, and an
implementation that appears later must be clipped the same way.
"
Extension { #name : 'BitBlt' }

{ #category : '*Graphics-Fixes' }
BitBlt >> drawLoopX: xDelta Y: yDelta [
	"This is the Bresenham plotting algorithm (IBM Systems Journal Vol 4
	 No. 1, 1965).  It chooses a principal direction, and maintains a
	 potential, P.  When P's sign changes, it is time to move in the minor
	 direction as well.  Pre-clipped; see the comment above."

	| dx dy px py p i first last bounds wraps |
	<primitive: 104>
	"Nothing on this line can reach the clipping rectangle."
	((destX max: destX + xDelta) + width <= clipX
		or: [(destX min: destX + xDelta) >= (clipX + clipWidth)
		or: [(destY max: destY + yDelta) + height <= clipY
		or: [(destY min: destY + yDelta) >= (clipY + clipHeight)]]])
			ifTrue: [^self].
	dx := xDelta sign.
	dy := yDelta sign.
	px := yDelta abs.
	py := xDelta abs.
	py > px ifTrue: [
		"more horizontal: the major coordinate is destX, py steps of dx"
		bounds := self
			drawLoopBoundsFrom: destX step: dx nib: width
			clipAt: clipX extent: clipWidth count: py.
		first := bounds first.
		last := bounds last.
		first > last ifTrue: [^self].
		p := py // 2.
		first > 0 ifTrue: [
			wraps := (((first * px) - p / py) ceiling max: 0).
			destX := destX + (first * dx).
			destY := destY + (wraps * dy).
			p := p - (first * px) + (wraps * py)].
		self copyBits.
		i := first.
		[(i := i + 1) <= last] whileTrue: [
			destX := destX + dx.
			(p := p - px) < 0 ifTrue: [
				destY := destY + dy.
				p := p + py].
			self copyBits].
		^self].
	"more vertical: the major coordinate is destY, px steps of dy"
	bounds := self
		drawLoopBoundsFrom: destY step: dy nib: height
		clipAt: clipY extent: clipHeight count: px.
	first := bounds first.
	last := bounds last.
	first > last ifTrue: [^self].
	p := px // 2.
	first > 0 ifTrue: [
		wraps := (((first * py) - p / px) ceiling max: 0).
		destY := destY + (first * dy).
		destX := destX + (wraps * dx).
		p := p - (first * py) + (wraps * px)].
	self copyBits.
	i := first.
	[(i := i + 1) <= last] whileTrue: [
		destY := destY + dy.
		(p := p - py) < 0 ifTrue: [
			destX := destX + dx.
			p := p + px].
		self copyBits]
]

{ #category : '*Graphics-Fixes' }
BitBlt >> drawLoopBoundsFrom: origin step: step nib: nibSize clipAt: clipLow extent: clipLength count: count [
	"The first and last step of drawLoopX:Y: at which the nib can overlap
	 the clipping rectangle along the major coordinate, as a two element
	 Array.  The nib at step n covers `origin + (n * step)' through
	 `origin + (n * step) + nibSize - 1', and step is 1 or -1, so the
	 visible steps are one contiguous run and both ends solve directly.
	 A first greater than a last means no step is visible at all.

	 A zero step happens only when the line has no length, and then count
	 is zero as well; the arithmetic below still answers 0 to 0 exactly
	 when the one step there is can be seen."

	| first last |
	step > 0
		ifTrue: [
			first := clipLow - nibSize - origin + 1.
			last := clipLow + clipLength - origin - 1]
		ifFalse: [
			first := origin - (clipLow + clipLength) + 1.
			last := origin - clipLow + nibSize - 1].
	^Array with: (first max: 0) with: (last min: count)
]

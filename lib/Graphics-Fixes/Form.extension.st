"
Form>>extent: without the Alto's ceiling on a bitmap.

1983 built a Form's bitmap as `WordArray new: (extentSize min: WordArray
maxSize)' -- 64,640 words, the largest object the Alto could hold -- while
keeping the full width and height, so every Form of more than 1,034,240
pixels was born with a bitmap too small for its extent.  The primitive
refused it, rightly, and the Smalltalk fallback raised `a primitive has
failed' with nothing said about why.  1983 had no screen bigger than 640 by
480; this system grows the Display to the window, and 1280 by 960 is 76,800
words, so `Form fromDisplay: Display boundingBox' -- the screen is an
ordinary object -- failed on any ordinary desktop, `DisplayText text:' of
2,000 characters failed, and `Form new extent: 1024@1023' was a Form whose
every black raised.  extent:offset: had no such cap, so the same size worked
one way and not the other (Bugs6 GUI-5).

The cap is gone: a bitmap is as big as its extent, and the primitive's own
refusal of a bitmap shorter than the extent stays, as the check it always
was.  WordArray class>>maxSize, whose one sender this was, answers what
every collection answers.

Form>>valueAt: and valueAt:put: are also here, for an unrelated reason:
they are the only pixel accessors a Form has, and 1983 ran both through one
1x1 Form that every process shares.  Form class>>initialize makes it, once,
into the class variable OneBitForm; valueAt: blits the pixel into it and
then reads its one word, and valueAt:put: paints it black or white and then
blits it into the receiver.  Each is two steps on one object with nothing
held between them.  With one process at a time nothing can come between
the steps.  Under `-serve -workers N' another worker's step does: a read
answers some other form's pixel and a write stores some other caller's
colour -- on eight workers, each reading its own form, 13% of reads came
back wrong, and half of the writes (Bugs07 GUI-4).  Nothing assigns the
variable after initialize, it is only written THROUGH, which is why the
class-variable audit recorded in doc/CONCURRENCY.md (assignments outside
initialize) did not list it.

Both now make the 1x1 Form a temporary of the call, so the scratch pixel is
one process's.  Still a blit rather than a read of the bitmap's words, for
two reasons.  A Form's bits may be a WordArray, a DisplayBitmap, a
ByteArray or a String (1983 keeps run-length scan lines in Strings), and
the primitive is the one place that knows how each of them is a row of
sixteen-bit words, and refuses a bitmap too short for its extent; and a
write to Display has to go through the primitive, which is what tells the
window which part of the screen changed.  What a call costs beyond 1983's
is one Form of one word, beside the BitBlt and Rectangle 1983 already makes
for each.

Form>>rotateBy: is here because 1983's reduced the angle and then forgot
it.  Its first line is `angle4 _ angle \\ 4' and every test after it reads
`angle': `angle = 0', `angle = 2', `angle = 1'.  So only 0, 1 and 2 meant
what the comment promises, and every other angle took the last branch, a
quarter turn counter-clockwise -- right for 3 and for -1, which is why the
slip lasted, and wrong for the rest: `rotateBy: 4', a full turn, answered
the form turned a quarter back, 5 was not 1 and 6 was not 2 (Bugs07
GUI-7).  The method below is 1983's with `angle4' read wherever `angle'
was, so the rotation is decided by the angle modulo four turns, as the
comment always said.

One answer changes, and becomes an answer: valueAt: of a point outside the
receiver.  The blit is clipped to nothing there, so 1983 answered whatever
the shared pixel held from the call before -- any process's.  A fresh pixel
is white, so a point outside a Form reads 0.  valueAt:put: outside it still
changes nothing.  OneBitForm itself is still made by 1983's initialize and
nothing reads it now.
"
Extension { #name : 'Form' }

{ #category : '*Graphics-Fixes' }
Form >> extent: aPoint [
	"Create a virtual bit map with width = (extent x) and height = (extent y)
	 with the bits all zeros (white).  The extent is guaranteed to be zero
	 or a positive size.  1983's method without the Alto's ceiling on the
	 bitmap; see the comment above."

	| nonZeroExtent |
	nonZeroExtent := aPoint >= (0 @ 0)
		ifTrue: [aPoint]
		ifFalse: [0 @ 0].
	self extent: nonZeroExtent
		offset: 0 @ 0
		bits: (WordArray new: nonZeroExtent y * (nonZeroExtent x + 15 // 16))
]

{ #category : '*Graphics-Fixes' }
Form >> valueAt: aPoint [
	"Answer 1 if the pixel at aPoint is black in the receiver and 0 if it is
	 white, or outside the receiver.  Through a 1x1 Form of this call's
	 own rather than one every process shares; see the comment above."

	| pixel |
	pixel := Form new extent: 1 @ 1.
	pixel
		copyBits: (aPoint extent: 1 @ 1)
		from: self
		at: 0 @ 0
		clippingBox: self boundingBox
		rule: Form over
		mask: Form black.
	^(pixel bits at: 1) = 0
		ifTrue: [0]
		ifFalse: [1]
]

{ #category : '*Graphics-Fixes' }
Form >> valueAt: aPoint put: maskCode [
	"Set the pixel at aPoint in the receiver to white (0) or black (1);
	 outside the receiver, do nothing.  Through a 1x1 Form of this call's
	 own rather than one every process shares; see the comment above."

	| pixel |
	pixel := Form new extent: 1 @ 1.
	maskCode = 0 ifFalse: [pixel black].
	self
		copyBits: pixel boundingBox
		from: pixel
		at: aPoint
		clippingBox: self boundingBox
		rule: Form over
		mask: Form black
]

{ #category : '*Graphics-Fixes' }
Form >> rotateBy: angle [
	"Answer a copied Form rotated clockwise by angle in units of 90 degrees.
	 angle = 0 means unchanged, 1 means clockwise 90 degrees, etc.  1983's
	 method deciding by angle modulo 4 everywhere instead of only where it
	 computed it; see the comment above."

	| angle4 rotSize newForm rotForm all destPt sourcePt sourceDelta destDelta rotOrigin |
	angle4 := angle \\ 4.
	angle4 = 0 ifTrue: [^self deepCopy].	"null rotation"
	angle4 = 2 ifTrue: [^(self reflect: 0 @ 1) reflect: 1 @ 0].	"two reflections does 180 degrees"

	"Break the problem up into squares of size 2^N, and rotate them"
	rotSize := (width min: height)	"minimum rotation size"
		max: 64.	"but faster if we do larger chunks"
	rotSize := 2 raisedTo: ((rotSize - 1) asFloat floorLog: 2) + 1.	"force up to a power of 2"
	newForm := Form extent: self extent transpose.
	rotForm := Form extent: rotSize asPoint.
	all := rotForm boundingBox.
	sourcePt := 0 @ 0.
	width >= height
		ifTrue:
			[sourceDelta := rotSize @ 0.
			angle4 = 1
				ifTrue:
					[destPt := 0 @ 0.
					destDelta := sourceDelta transpose.
					rotOrigin := (rotSize - height) @ 0]
				ifFalse:
					[destPt := 0 @ (width - rotSize).
					destDelta := (0 @ 0) - sourceDelta transpose.
					rotOrigin := 0 @ 0]]
		ifFalse:
			[sourceDelta := 0 @ rotSize.
			angle4 = 1
				ifTrue:
					[destPt := (height - rotSize) @ 0.
					destDelta := (0 @ 0) - sourceDelta transpose.
					rotOrigin := 0 @ 0]
				ifFalse:
					[destPt := 0 @ 0.
					destDelta := sourceDelta transpose.
					rotOrigin := 0 @ (rotSize - width)]].
	(width max: height) - 1 // rotSize + 1 timesRepeat:
		[rotForm copy: all from: sourcePt in: self rule: 3.
		rotForm rotate2: (angle4 = 1 ifTrue: [1] ifFalse: [-1]).
		newForm copy: (all translateBy: destPt) from: rotOrigin in: rotForm rule: 3.
		sourcePt := sourcePt + sourceDelta.
		destPt := destPt + destDelta].
	^newForm
]

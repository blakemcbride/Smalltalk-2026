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

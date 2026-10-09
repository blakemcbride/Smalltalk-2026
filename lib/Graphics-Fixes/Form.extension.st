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

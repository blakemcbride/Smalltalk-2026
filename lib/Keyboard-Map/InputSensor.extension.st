"
The keyboard in front of the user, rather than the Alto's.

The window sends the code a key carries UNSHIFTED -- SDL's keycode for the
layout, before any modifier is applied -- and the image is what applies the
shift, through a map built by InputSensor class>>initialize.  That map is
1983's, which is to say the Alto's, and the Alto's keyboard is not the one
anybody has.  Everything unshifted agrees; three shifted keys do not, and
all three are keys a Smalltalk programmer reaches for.  Four keys are
missing outright: the arrows.

The keyboard map is a String of 256 entries for each of the five meta states
-- none, shift, control, shift and control, lock -- indexed by
`256 * meta + code + 1', and every code the Alto did not have holds 255.
Two hundred and fifty five means unassigned, and unassigned is not inert: it
arrives at ParagraphEditor>>normalCharacter: as a character to be typed, so
a key that decodes to it puts a box in your text rather than doing nothing.
In this system it is worse than a box -- code 255 is past the end of the
face, so the scanner substitutes the zero-width `character not in font' and
the key appears to do nothing at all.

The window sends the cursor keys as codes of their own -- see the comment in
src/gfx/display.c for why those four numbers and not others -- and this is
where they stop being unassigned.  They decode to themselves, in every meta
state, so that a shift or a control held down while an arrow is pressed
still yields the arrow instead of a 255.
"
Extension { #name : 'InputSensor' }

{ #category : '*Keyboard-Map' }
InputSensor class >> cursorKeyCodes [
	"The codes the window sends for left, right, up and down, in that order.

	 This list and the one in src/gfx/display.c are the same four numbers and
	 have to stay that way; nothing checks it, because the image cannot see
	 the C."

	^#(152 153 154 155)
]

{ #category : '*Keyboard-Map' }
InputSensor class >> shiftedKeys [
	"What shift does to the three keys whose 1983 answer a US keyboard
	 disagrees with, as unshifted code -> shifted character.

	    -    gave code 21, an Alto code with no glyph in this face, so
	         shift-minus typed a blank five pixels wide instead of _
	    6    gave ~, because on the Alto the tilde was over the 6
	    `    gave 255, unassigned, because the Alto had no key carrying a
	         backquote unshifted -- so ~ could not be typed at all, and
	         trying put a zero-width nothing in the text

	 ^ and ~ are the two a user meets first: ^ begins the return of every
	 method and ~ is half of both ~= and ~~.  The Alto put ^ over the _ key
	 and the map still says so, at 256 + 95; nothing here removes it,
	 because no key on a modern keyboard sends 95 unshifted, so the entry
	 costs nothing and it is 1983's.

	 Only the shift plane is corrected.  Shift-with-control is the Alto's
	 own table of codes above 127 and has nothing to do with punctuation."

	^Array with: 45 -> $_ with: 54 -> $^ with: 96 -> $~
]

{ #category : '*Keyboard-Map' }
InputSensor >> initMap [
	"1983's map, with the cursor keys decoding to themselves and the shifted
	 punctuation reading off a keyboard that exists.

	 Copied rather than poked in place: DefaultKeyboardMap is one String
	 shared by every InputSensor and rebuilt by InputSensor class>>initialize,
	 so writing into it would be both a change to something we do not own and
	 a change that the next initializer pass undoes."

	| map |
	map := DefaultKeyboardMap copy.
	InputSensor cursorKeyCodes do: [:code |
		#(0 256 512 768 1024) do: [:meta |
			map at: meta + code + 1 put: code asCharacter]].
	InputSensor shiftedKeys do: [:each |
		map at: 256 + each key + 1 put: each value].
	keyboardMap := map
]

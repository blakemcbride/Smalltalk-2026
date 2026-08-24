"
Four keycodes that decode to themselves.

The keyboard map is a String of 256 entries for each of the five meta states
-- none, shift, control, shift and control, lock -- indexed by
`256 * meta + code + 1', and every code the Alto did not have holds 255.
Two hundred and fifty five means unassigned, and unassigned is not inert: it
arrives at ParagraphEditor>>normalCharacter: as a character to be typed, so
a key that decodes to it puts a box in your text rather than doing nothing.

The window sends the cursor keys as codes of their own -- see the comment in
src/gfx/display.c for why those four numbers and not others -- and this is
where they stop being unassigned.  They decode to themselves, in every meta
state, so that a shift or a control held down while an arrow is pressed
still yields the arrow instead of a 255.
"
Extension { #name : 'InputSensor' }

{ #category : '*Cursor-Keys' }
InputSensor class >> cursorKeyCodes [
	"The codes the window sends for left, right, up and down, in that order.

	 This list and the one in src/gfx/display.c are the same four numbers and
	 have to stay that way; nothing checks it, because the image cannot see
	 the C."

	^#(152 153 154 155)
]

{ #category : '*Cursor-Keys' }
InputSensor >> initMap [
	"1983's map, with the cursor keys decoding to themselves.

	 Copied rather than poked in place: DefaultKeyboardMap is one String
	 shared by every InputSensor and rebuilt by InputSensor class>>initialize,
	 so writing into it would be both a change to something we do not own and
	 a change that the next initializer pass undoes."

	| map |
	map := DefaultKeyboardMap copy.
	InputSensor cursorKeyCodes do: [:code |
		#(0 256 512 768 1024) do: [:meta |
			map at: meta + code + 1 put: code asCharacter]].
	keyboardMap := map
]

"
The keyboard in front of the user, rather than the Alto's.

The window sends the code a key carries UNSHIFTED -- SDL's keycode for the
layout, before any modifier is applied -- and the image is what applies the
shift, through a map built by InputSensor class>>initialize.  That map is
1983's, which is to say the Alto's, and the Alto's keyboard is not the one
anybody has.  Everything unshifted agrees; three shifted keys do not, and
all three are keys a Smalltalk programmer reaches for.  Seven keys are
missing outright: the four arrows, home, end, and the delete that goes
forward.

The keyboard map is a String of 256 entries for each of the five meta states
-- none, shift, control, shift and control, lock -- indexed by
`256 * meta + code + 1', and every code the Alto did not have holds 255.
Two hundred and fifty five means unassigned, and unassigned is not inert: it
arrives at ParagraphEditor>>normalCharacter: as a character to be typed, so
a key that decodes to it puts a box in your text rather than doing nothing.
In this system it is worse than a box -- code 255 is past the end of the
face, so the scanner substitutes the zero-width `character not in font' and
the key appears to do nothing at all.

The window sends those seven as codes of their own -- see the comment in
src/gfx/display.c for why those numbers and not others -- and this is where
they stop being unassigned.  They decode to themselves, in every meta state,
so that a shift or a control held down while one is pressed still yields the
key instead of a 255.

That the map answers the same thing in all five planes is the point rather
than an omission.  These keys are not characters, so there is no shifted
form for the map to hold; what control does to left is not a different
character but a different distance, and distance is the editor's business.
It reads the control bit off the event, through keyboardEventPeek, where
the state at the moment the key was struck is kept.
"
Extension { #name : 'InputSensor' }

{ #category : '*Keyboard-Map' }
InputSensor class >> editingKeyCodes [
	"The codes the window sends for the keys the Alto keyboard did not have:
	 left, right, up, down, home, end and delete-forward, in that order.  The
	 order is the meaning -- ParagraphEditor>>normalCharacter: dispatches on
	 the position of a code in this list and nothing else.

	 This list and the one in src/gfx/display.c are the same seven numbers and
	 have to stay that way; nothing checks it, because the image cannot see
	 the C.

	 Delete is out of line at 146 because 158, which would have continued the
	 run, is BS2 -- ParagraphEditor>>leaveBrackets: already answers for it."

	^#(152 153 154 155 156 157 146)
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
	"1983's map, with the editing keys decoding to themselves and the shifted
	 punctuation reading off a keyboard that exists.

	 Copied rather than poked in place: DefaultKeyboardMap is one String
	 shared by every InputSensor and rebuilt by InputSensor class>>initialize,
	 so writing into it would be both a change to something we do not own and
	 a change that the next initializer pass undoes."

	| map |
	map := DefaultKeyboardMap copy.
	InputSensor editingKeyCodes do: [:code |
		#(0 256 512 768 1024) do: [:meta |
			map at: meta + code + 1 put: code asCharacter]].
	InputSensor shiftedKeys do: [:each |
		map at: 256 + each key + 1 put: each value].
	keyboardMap := map
]

{ #category : '*Keyboard-Map' }
InputSensor >> keyboardEventPeek [
	"The next keystroke undecoded: the key's own code, and the state of the
	 meta keys at the moment it was struck.

	 keyboardPeek answers what the map makes of the two together, which is
	 the character to type.  That is the whole answer for a key that IS a
	 character, and no answer at all for a key whose meaning depends on the
	 meta state without being a character in any of them -- control-left is
	 not a letter, it is a longer left.

	 The state has to come off the event rather than off the keyboard now.
	 A keystroke waits in the queue until the controller whose window it
	 belongs to reaches it, and the control key can be let go in between; ask
	 the hardware and a burst of typeahead decides on whichever keys happen
	 to be down when it is drained."

	self keyboardPressed ifTrue: [^self primKbdPeek].
	self error: 'Cant peek when nothing there'
]

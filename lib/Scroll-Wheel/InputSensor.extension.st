"
The wheel the 1983 mouse did not have.

Every other thing the mouse does arrives as an event word in the ring the
input process drains -- a position, or the edge of a button going down or
coming up.  A notch is neither.  It is an amount, and two notches are twice
as much rather than a second edge, so there is no bistate for it to be and
no bit of `bitState' that could mean it.

So the window counts notches and this takes them: read once, taken away, and
any fraction the window had left over kept for next time.  That also makes
it safe to ask from a control loop, which is where a controller can ask from
-- an event would have to be caught by whoever happened to be looking when
it landed, and a notch turned while a menu is up would simply be lost.
"
Extension { #name : 'InputSensor' }

{ #category : '*Scroll-Wheel' }
InputSensor >> wheelDelta [
	"Answer how many whole wheel notches have been turned since this was
	 last asked, positive away from the user, and forget them.  Answer zero
	 where there is no wheel, or no window at all."

	<primitive: 242>
	^0
]

"
Sensor waitButton and Sensor waitNoButton, when there is nobody to press a
button.

Both are 1983's, and both are a spin on the mouse state with no exit but
the mouse: `[self anyButtonPressed] whileFalse'.  In a headless image there
is no mouse, so `st80 -serve img 'Sensor waitButton'' never answered, never
raised, and never finished; the worker stayed in that loop until the
process was killed, and with one worker configured that was the whole
server (Bugs4, section 11).  Line, Circle, Curve, Spline and LinearFit all
open their examples with `Sensor waitButton', so `Line example' was the
same hang under a different name, and so was `Rectangle fromUser', which is
where `Form fromUser' goes.

Bugs2 B11 settled what the answer is for the Object side of this -- halt,
notify:, confirm: and inspect -- and the rule it arrived at is the rule
here: ask Object>>canOpenUserInterface, and where there is a screen do
exactly what 1983 did.  Where there is not, the two families part company.
B11's methods signal a resumable Warning, because a person dismissing a
notifier IS resumption and there is a sensible unhandled answer -- carry
on.  These have no such answer.  waitButton must produce the point the
person clicked, and there is no point; resuming with nil would put nil into
`aCircle center:' and raise something unrecognisable three sends later.  So
these raise an Error, which under -serve ends the one request that asked
for a click and gives the worker back.

The display path also gained a `Processor yield' inside the spin, which
1983's does not have.  Its loop compiles to a bare backwards jump, and with
native workers that is one core held at 100% for as long as the person
takes to decide.  Every neighbouring 1983 wait loop -- ScrollController's,
Rectangle class>>fromUser:'s -- already yields, so this is the same wait
written the way the code beside it writes one.
"
Extension { #name : 'InputSensor' }

{ #category : '*Graphics-Fixes' }
InputSensor >> waitButton [
	"Wait for the user to press any mouse button and then answer with the
	 current location of the cursor.  See the comment above for what
	 happens when there is no user."

	self canOpenUserInterface ifFalse: [
		^Error new signal:
			'no display: waitButton would wait forever for a mouse button nothing can press'].
	[self anyButtonPressed] whileFalse: [Processor yield].
	^self cursorPoint
]

{ #category : '*Graphics-Fixes' }
InputSensor >> waitNoButton [
	"Wait for the user to release any mouse button and then answer with the
	 current location of the cursor.  See the comment above.

	 waitClickButton is waitButton followed by this one, so it needs no
	 repair of its own."

	self canOpenUserInterface ifFalse: [
		^Error new signal:
			'no display: waitNoButton would wait forever for a mouse button nothing can release'].
	[self anyButtonPressed] whileTrue: [Processor yield].
	^self cursorPoint
]

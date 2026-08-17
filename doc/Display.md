# The window, and what the 1983 interface expects of you

Two things about running `./st80 -run st80.image` surprise everyone, and neither is a
fault. This file is here so they are surprising once.

## The screen is the window

The Smalltalk display is a `Form` — three fields and a word array — and the image asks
`Display extent` every time it wants to know how big the screen is. Nothing caches it. So
rather than fit a fixed 640x480 screen into whatever window the window manager hands back,
the window is measured when it opens and **the Form is grown to fill it**.

The Form's identity is kept, because `Display` is reachable from `ScheduledControllers`,
from every controller and view, from `Cursor`, and from the VM state word a snapshot
carries. Only the three fields change. The old pixels are copied in and the new area filled
with the desktop halftone, so the screen is right the instant it changes — the image is
never asked to redraw and never learns that anything happened. Windows stay where they
were; the new space is desktop. `ScheduledControllers restore` then re-reads
`Display boundingBox` on its own, which is why "restore display" does the right thing
afterwards without being told.

This matters most under a **tiling window manager**. i3 answers a request for 1280x960 with
its tile — 956x1557 here. 4:3 does not fit a 0.61 aspect at any scale, so better than half
the tile stayed border however the fitting was done. Now the desktop is 956x1557 and there
is no border at all.

The screen only ever **grows**. Shrinking it would put scheduled windows off the screen,
where MVC gives you no way to reach them.

### The image has to be told, or the mouse dies

Growing the Form is only half of it, and the other half fails in a way that looks nothing
like a display fault. `ControlManager>>searchForActiveController` offers control only to a
controller whose view contains the cursor, and the one that answers for the desktop is
`screenController`, whose view was windowed to `Display boundingBox` **when the snapshot was
taken**. Grow the screen and that rectangle is stale: with the cursor in the new area no
controller wants control at all, the search loop spins for ever, and the buttons are never
read. The desktop looks perfect and no mouse button does anything.

`ControlManager>>restore` is the message that fixes it, and a message cannot be sent from C
between bytecodes — the reply would land on the stack of whatever frame was interrupted, one
slot above where its own bytecodes expect to find things. So the VM does what
`View>>setWindow:` does, which is a state change rather than a computation: set the window,
drop the viewport, and unlock the cached transformation and inset box so the image
recomputes them next time it asks.

Every field is located **by name**, through the class's own `instanceVariables`. If any name
is missing the screen is not resized at all — a letterboxed 640x480 desktop is a much
smaller disappointment than a desktop whose mouse does nothing. `ST_DISPLAY_TRACE=1` names
the step that refused.

This is why the Xerox `VirtualImage` is never resized: its `Smalltalk` is not laid out the
way this lookup walks (`-disasm` has never found a class in it either), so the refusal is
reported and the old letterboxing applies. Any image this system bootstraps is resized.

| | |
|---|---|
| `ST_DISPLAY_SCALE` | screen pixels per display pixel. Dropped automatically if the chosen scale would leave the desktop smaller than the image's own screen |
| `ST_DISPLAY_WINDOW` | `WxH` — open the window at exactly this size. How a tiling manager's behaviour is reproduced anywhere else |
| `ST_DISPLAY_FIT=off` | keep the image's screen size and letterbox it, as before |
| `ST_DISPLAY_THEME` | `paper` (default), `classic` (pure black on pure white), `dark` |
| `ST_DISPLAY_PRESENTATION` | `integer`, `letterbox`, `stretch` — only reachable when fitting is off, or the window is smaller than the screen |
| `ST_DISPLAY_TRACE` | report why a resize was refused |

## The menus are press-and-hold

There is no click-to-open menu anywhere in this interface. **Press and keep the button
down**, and the menu stays up only while you hold it; drag onto the item you want and
release there. A click — press and release in the same instant — puts the menu up and takes
it straight back down, which looks like nothing happened.

Blue is the window menu, yellow the view's own menu, red selects. On a three-button mouse
that is left = red, middle = yellow, right = blue.

**The pointer jumps onto the menu, and it is supposed to.** A menu is displayed centred on
the cursor, translated if that would put it off the screen, and then it puts the cursor back
on its own current item — `Sensor cursorPoint: marker center`, the second line of
`PopUpMenu>>startUp:`. That warp is primitive 91, and it used to do nothing here, on the
grounds that the host owns the pointer. The host owns the pointer's *shape*; 1983 owns its
*position*. Near an edge the menu moves and the pointer did not follow, so `manageMarker`
found the cursor outside the frame, `markerOff` set the selection to nothing, and releasing
the button chose nothing — a click that did not register, coming right the moment you moved
the mouse into the menu. Red was never affected, because red does not raise a menu.

`StandardSystemController>>move`, `StandardSystemView>>getFrame` and `Rectangle>>fromUser`
warp too, and now work for the same reason.

## The rubber band is a flash, not a drawing

Dragging out a new window's size runs this, once per turn of the tracking loop:

```smalltalk
Display fill: frame rule: Form reverse mask: Form gray.
Display fill: frame rule: Form reverse mask: Form gray.
```

Twice, same rectangle, same reversing rule — so the two cancel and the rectangle is **never
left on the Form**. It is not a drawing, it is a flash. On the Alto that was enough: the CRT
scanned display memory continuously, so whatever was there between the two fills was on the
glass.

Here the Form is the truth and the window is a copy taken between bytecode slices. Measured
over one drag: **3474 draws to the display, 67 of them presented** — two per cent, and the
state worth seeing lasts a handful of bytecodes out of thousands. So the rectangle appeared
at random instead of tracking the pointer, which is what "the window blinks a lot" is.

The fix presents at the right moment rather than more often. A reversing blit that exactly
repeats the one before it is, by construction, an undo, so the frame worth showing is the
one before it lands. Two clocks keep it honest: the flash presents at most at the refresh
rate, and while a drag is running the pump's own present stays out of the way — it would
land on the undone state and take the rectangle straight back off, which is the blinking
again by another route.

`ST_DISPLAY_TRACE=1` reports the draw and present counts at exit.

## Opening a browser is two gestures, not one

**The system is not slow. It is waiting for you.**

Choosing `browser` from the yellow-button menu does not open a window. It arms one. The
next thing Smalltalk-80 wants is for you to say *where*: press the **red** (left) button,
drag out a rectangle, release. The window appears when you let go.

**The pointer is what tells you so.** `StandardSystemView>>getFrame` is the whole of it:

```smalltalk
Sensor waitNoButton.
Cursor origin showWhile:
    [[Sensor redButtonPressed] whileFalse: [Processor yield]].
```

It draws nothing and prints nothing. The single signal that a rectangle is wanted is the
cursor turning into a top-left corner, then a bottom-right corner while you drag, then back
to the arrow when the window appears. Primitive 101 used to discard the Form and let the
host draw its own arrow, so there was no signal at all — a frozen screen and a vanished
menu, which reads exactly like a hang. It now hands the image's 16x16 Form to the window
system, hot spot and all. `ST_DISPLAY_TRACE=1` reports each change.

Measured end to end — load a 31 MB image, open the menu, choose `browser`, frame it, and
draw the whole System Browser — **1.1 seconds**. The browser's model builds in 0 ms.

If you choose `browser` and then wait, nothing happens, forever, and it looks exactly like
a system that has hung. It is the single most common way to conclude this interface is
broken.

Scripted, the whole gesture is:

```sh
./st80 -inject "m 320 240; w 20; d 129; w 40; m 320 271; w 40; u 129; w 60;
                m 60 60; w 20; d 130; w 20; m 900 1400; w 20; u 130; w 200" \
       -screenshot /tmp/browser.pbm -run st80.image
```

`m X Y` moves, `d`/`u` press and release a button (128 blue, 129 yellow, 130 red), `k`
types a key, and `w N` waits N bytecode slices so the image has time to respond.

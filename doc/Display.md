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

| | |
|---|---|
| `ST_DISPLAY_SCALE` | screen pixels per display pixel. Dropped automatically if the chosen scale would leave the desktop smaller than the image's own screen |
| `ST_DISPLAY_WINDOW` | `WxH` — open the window at exactly this size. How a tiling manager's behaviour is reproduced anywhere else |
| `ST_DISPLAY_FIT=off` | keep the image's screen size and letterbox it, as before |
| `ST_DISPLAY_THEME` | `paper` (default), `classic` (pure black on pure white), `dark` |
| `ST_DISPLAY_PRESENTATION` | `integer`, `letterbox`, `stretch` — only reachable when fitting is off, or the window is smaller than the screen |

## The menus are press-and-hold

There is no click-to-open menu anywhere in this interface. **Press and keep the button
down**, and the menu stays up only while you hold it; drag onto the item you want and
release there. A click — press and release in the same instant — puts the menu up and takes
it straight back down, which looks like nothing happened.

Blue is the window menu, yellow the view's own menu, red selects. On a three-button mouse
that is left = red, middle = yellow, right = blue.

## Opening a browser is two gestures, not one

**The system is not slow. It is waiting for you.**

Choosing `browser` from the yellow-button menu does not open a window. It arms one. The
next thing Smalltalk-80 wants is for you to say *where*: press the **red** (left) button,
drag out a rectangle, release. The window appears when you let go.

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

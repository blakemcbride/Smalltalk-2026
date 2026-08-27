/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The SDL3 front end.
 *
 *  The whole of this file runs on thread 0 and nowhere else.  SDL's video
 *  and event calls are documented main-thread-only, and on macOS "main
 *  thread" means the one that entered main() -- Cocoa's run loop is bound to
 *  it and cannot be moved.  When the worker pool arrives in Phase 7, thread
 *  0 stays a dedicated pump and never executes Smalltalk: a worker parked in
 *  a garbage-collection safepoint at the moment the window server wants a
 *  response would deadlock the compositor.
 *
 *  The Smalltalk display Form is the authoritative pixel store.  This file
 *  only ever reads it, converts damaged regions from 1 bit per pixel into
 *  the texture, and presents.  SDL_LockTexture is write-only and may hand
 *  back garbage, so nothing may be read back out of the texture -- which
 *  suits us, because BitBlt already keeps the truth in the Form.
 *
 *  Built without SDL3 the file degrades to a headless stub, so tests and CI
 *  need no display.
 */

#include "gfx.h"
#include "st_sched.h"
#include "interp.h"
#include "st_port.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  ----------  Display form and damage tracking  ----------  */

static st_oop   display_form = ST_NIL;
/*
 *  Asked before the screen changes size, and refuses on the image's behalf.
 *
 *  Growing the Form is only half of a resize: the image keeps its own idea of
 *  how big the screen is, in the window of the view the desktop belongs to,
 *  and a stale one leaves the mouse dead in the new area.  Whoever knows how
 *  to bring that up to date registers it here, and a 0 answer means the
 *  screen stays the size it is.  Nothing registered -- a test, a headless run
 *  -- means the same.
 */
static int    (*screen_hook)(int width, int height);

/*  Defined by whichever half of this file is compiled; see GFX_set_display. */
static void     adopt_display_extent(void);
static int      damage_valid;
static unsigned long damage_calls;
static unsigned long present_calls;
static int      damage_x1;
static int      damage_y1;
static int      damage_x2;
static int      damage_y2;

void
GFX_set_display(st_oop form)
{
    /*  A reference held by C must be counted like any other.  */
    OM_increase_ref(form);
    OM_decrease_ref(display_form);
    display_form = form;
    /*  Published so a snapshot carries it -- see om.h.  */
    st_om_vm_state[ST_VM_DISPLAY] = form;
    /*
     *  The IMAGE can change the screen too, and does.
     *
     *  SystemDictionary>>snapshotAs: shrinks the display to a hundred rows
     *  so the snapshot is small, writes it, and puts the height back:
     *
     *      height _ Display height.
     *      DisplayScreen displayHeight: (height min: 100).
     *      ... snapshotPrimitive ...
     *      DisplayScreen displayHeight: height.
     *
     *  Each of those installs a different Form through beDisplay and lands
     *  here.  Left alone, the texture keeps the size it was made for and
     *  present() declines to upload anything, and the screen controller's
     *  window is the stale rectangle that makes every button do nothing --
     *  the same fault as growing the screen without telling the image, from
     *  the other direction.  So adopt the new extent the same way a resize
     *  does: the window follows the image here, rather than the other way
     *  round.
     */
    adopt_display_extent();
    GFX_damage_all();
}

st_oop
GFX_display_form(void)
{
    return display_form;
}

void
GFX_set_screen_hook(int (*fn)(int width, int height))
{
    screen_hook = fn;
}

/*
 *  Damage accumulates as one bounding rectangle.  A list of rectangles would
 *  upload less, but the union of a frame's worth of edits is usually small
 *  and a single lock and copy beats several.
 */
void
GFX_damage(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    ++damage_calls;
    if (!damage_valid) {
        damage_x1 = x;
        damage_y1 = y;
        damage_x2 = x + w;
        damage_y2 = y + h;
        damage_valid = 1;
        return;
    }
    if (x < damage_x1)
        damage_x1 = x;
    if (y < damage_y1)
        damage_y1 = y;
    if (x + w > damage_x2)
        damage_x2 = x + w;
    if (y + h > damage_y2)
        damage_y2 = y + h;
}

void
GFX_damage_all(void)
{
    gfx_form    form;

    if (!GFX_form_from_oop(display_form, &form))
        return;
    damage_valid = 0;
    GFX_damage(0, 0, form.width, form.height);
}

/*  ----------  Input queue  ----------  */

/*
 *  A ring of Smalltalk event words.  Thread 0 fills it; the interpreter
 *  drains it through primitive 95.  Single producer, single consumer, which
 *  is what keeps it honest once those are different threads.
 */
#define EVENT_QUEUE_SIZE    1024

static uint16_t event_queue[EVENT_QUEUE_SIZE];
static unsigned event_head;
static unsigned event_tail;
static int      mouse_x;
static int      mouse_y;
static int      button_state;
static unsigned events_dropped;

/*
 *  ST_INPUT_TRACE=1 prints both ends of this queue.
 *
 *  Input faults divide cleanly in two and look identical from a chair: the
 *  event never arrived, or it arrived and the image did not act on it.  One
 *  is this file's fault and the other is the image's, and no amount of
 *  staring at the screen tells you which.  Queued-and-drained, with the
 *  clock beside each, does.
 */
static int
input_traced(void)
{
    static int  answer = -1;

    if (answer < 0)
        answer = getenv("ST_INPUT_TRACE") != NULL;
    return answer;
}

static const char *
event_type_name(unsigned type)
{
    switch (type) {
    case ST_EVENT_XLOCATION:    return "x";
    case ST_EVENT_YLOCATION:    return "y";
    case ST_EVENT_BISTATE_ON:   return "down";
    case ST_EVENT_BISTATE_OFF:  return "up";
    default:                    return "?";
    }
}

/*
 *  ----------  Positions coalesce; transitions never do  ----------
 *
 *  A pointer position is idempotent.  The image reads `Sensor cursorPoint',
 *  not a path, so the only sample that matters is the newest one and every
 *  earlier one may be thrown away without changing any answer.
 *
 *  A bistate is an EDGE, and that asymmetry is the whole of this code.  The
 *  image rebuilds its idea of which buttons are down from the stream --
 *  InputState>>bitState, which every controller polls -- so a lost
 *  BISTATE_OFF leaves it believing a button is still held FOR EVER.  Nothing
 *  corrects it: `Sensor noButtonPressed' never answers true again, no menu
 *  opens, no controller takes control, and the window stays on the screen
 *  looking perfectly fine.  Framing a window is where this bit: it is the
 *  one gesture that drags at full rate with a button held while the image is
 *  busy drawing a rubber band, and 700 moves is 1400 words into a ring of
 *  1024.  Measured, before this: 379 events dropped in one drag.
 *
 *  So motion is held in one slot and appended only when something else has
 *  to go in front of it or when the image comes to read.  Pending motion is
 *  therefore bounded at a single pair, the ring can only be filled by real
 *  transitions -- which arrive at the speed of a hand -- and the events that
 *  cannot be reconstructed are the ones that can no longer be lost.
 */
static int      pending_motion;
static int      pending_x;
static int      pending_y;

static void
queue_word(unsigned type, unsigned value)
{
    unsigned    next = (event_tail + 1) % EVENT_QUEUE_SIZE;

    if (next == event_head)
        ++events_dropped;
    if (input_traced()
     && (next == event_head
      || (type != ST_EVENT_XLOCATION && type != ST_EVENT_YLOCATION)))
        fprintf(stderr, "st80: %8.3f queued %s %u%s\n",
                (double) ST_time_monotonic_ns() / 1e9, event_type_name(type),
                value, next == event_head ? " -- DROPPED, queue full" : "");
    if (next == event_head)
        return;                 /*  full: drop, never overwrite  */
    event_queue[event_tail] =
        (uint16_t) ((type << ST_EVENT_TYPE_SHIFT) | (value & ST_EVENT_VALUE_MASK));
    event_tail = next;

    /*
     *  The image's input process is waiting on this semaphore.  Signalling
     *  asynchronously queues it for the next process-switch check rather
     *  than switching underneath a half-executed bytecode.
     */
    SCHED_asynchronous_signal(SCHED_input_semaphore());
}

/*  Put the held position into the stream, if there is one.  */
static void
flush_motion(void)
{
    if (!pending_motion)
        return;
    pending_motion = 0;
    queue_word(ST_EVENT_XLOCATION, (unsigned) pending_x);
    queue_word(ST_EVENT_YLOCATION, (unsigned) pending_y);
}

/*
 *  Hold a position.  mouse_x and mouse_y move at once, because primitive 90
 *  reads them directly and must never lag the pointer; only the two words
 *  are deferred.
 */
static void
queue_motion(int x, int y)
{
    if (x == mouse_x && y == mouse_y)
        return;
    mouse_x        = x;
    mouse_y        = y;
    pending_x      = x;
    pending_y      = y;
    pending_motion = 1;
}

/*
 *  Anything that is not a position.  The held motion goes first, so a click
 *  is still preceded by the place it happened.
 */
static void
queue_transition(unsigned type, unsigned value)
{
    flush_motion();
    queue_word(type, value);
}

/*
 *  How many events the ring had no room for.
 *
 *  A dropped event is not a dropped frame: the image rebuilds its whole idea
 *  of which buttons are down from this stream -- InputState>>bitState, which
 *  InputSensor>>primMouseButtons reads and every controller polls -- so a
 *  lost bistate leaves it believing a button is still held, or was never
 *  pressed, until another one happens to correct it.  Nothing else in the
 *  system carries that state: GFX_button_state() exists and no primitive
 *  reads it.  So this number is never allowed to be silent.
 */
unsigned
GFX_events_dropped(void)
{
    return events_dropped;
}

void
GFX_draw_counts(unsigned long *damages, unsigned long *presents)
{
    *damages  = damage_calls;
    *presents = present_calls;
}

int
GFX_event_pending(void)
{
    /*
     *  The consumer flushes.  A held position has to be in the stream before
     *  the image is told whether the stream is empty, and doing it here
     *  covers the headless case too -- there is no pump to do it when no
     *  window was opened, and the test suites drive input that way.
     */
    flush_motion();
    return event_head != event_tail;
}

int
GFX_next_event_word(uint16_t *word)
{
    flush_motion();
    if (event_head == event_tail)
        return 0;
    *word = event_queue[event_head];
    event_head = (event_head + 1) % EVENT_QUEUE_SIZE;
    if (input_traced()) {
        unsigned    type = (unsigned) (*word >> ST_EVENT_TYPE_SHIFT);

        if (type != ST_EVENT_XLOCATION && type != ST_EVENT_YLOCATION)
            fprintf(stderr, "st80: %8.3f drained %s %u\n",
                    (double) ST_time_monotonic_ns() / 1e9, event_type_name(type),
                    (unsigned) (*word & ST_EVENT_VALUE_MASK));
    }
    return 1;
}

void
GFX_mouse_point(int *x, int *y)
{
    *x = mouse_x;
    *y = mouse_y;
}

int
GFX_button_state(void)
{
    return button_state;
}

/*
 *  ----------  Synthetic input  ----------
 *
 *  The same path a window's events take, driven from C instead of a mouse.
 *
 *  Without it the interactive half of the system can only be tested by a
 *  person sitting in front of it, which in practice means it is not tested.
 *  These do what the SDL handlers do -- move the pointer, set the button
 *  state, queue the event words, signal the input semaphore -- so what they
 *  exercise is the real path and not a parallel one built for the occasion.
 *  They work in a headless build too, which is where the tests run.
 */
/*
 *  Put the VM's idea of the pointer at x,y and tell the image, without
 *  waiting for the window system to say so.  The image warps the pointer and
 *  then immediately reads it back -- PopUpMenu>>startUp: does both in
 *  consecutive statements -- so a warp that only takes effect when SDL gets
 *  round to reporting it is a warp that has not happened yet.
 */
static void
warp_locally(int x, int y)
{
    gfx_form    form;

    if (GFX_form_from_oop(display_form, &form)) {
        if (x < 0)              x = 0;
        if (y < 0)              y = 0;
        if (x >= form.width)    x = form.width - 1;
        if (y >= form.height)   y = form.height - 1;
    }
    if (input_traced())
        fprintf(stderr, "st80: %8.3f warped to %d,%d%s\n",
                (double) ST_time_monotonic_ns() / 1e9, x, y,
                (x == mouse_x && y == mouse_y) ? " (already there)" : "");
    queue_motion(x, y);
}

void
GFX_inject_mouse(int x, int y)
{
    gfx_form    form;

    if (GFX_form_from_oop(display_form, &form)) {
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x >= form.width)
            x = form.width - 1;
        if (y >= form.height)
            y = form.height - 1;
    }
    queue_motion(x, y);
}

/*
 *  What the image is able to decode, and why a code outside it must never
 *  reach the ring.
 *
 *  InputState>>keyAt:put: hands any code it does not recognise straight to
 *  KeyboardEvent, which asks Character for `value: code' -- and
 *  CharacterTable has 256 entries.  Anything above that raises
 *  SubscriptOutOfBounds inside the input process, and an unhandled error
 *  there is not fatal: Error>>defaultAction answers nil and the process goes
 *  round its loop for the next event.  So one bad code does not fail once,
 *  it fails forever, and the input process runs at lowIOPriority -- above
 *  the user process that draws.  Everything below it starves.  A window that
 *  stops repainting while the image is plainly busy is what that looks like.
 *
 *  The SDL path never produces one: it drops any keycode it has no mapping
 *  for.  These two take their number from a script, so they hold the same
 *  line rather than trusting the caller.
 */
#define KEY_CODE_MAX        255

/*
 *  The bits InputState reserves for the mouse and keyset -- its BitMin to
 *  BitMax, 8r200 to 8r207.  The shift below is defined only for those: the
 *  code used to be shifted by whatever the caller passed, and `1 << (0 -
 *  128)' is undefined behaviour rather than merely a wrong answer.
 */
#define KEYSET_BIT_FIRST    128
#define KEYSET_BIT_LAST     135

void
GFX_inject_button(unsigned code, int down)
{
    if (code < KEYSET_BIT_FIRST || code > KEYSET_BIT_LAST)
        return;
    if (down)
        button_state |= 1 << (int) (code - KEYSET_BIT_FIRST);
    else
        button_state &= ~(1 << (int) (code - KEYSET_BIT_FIRST));
    queue_transition(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF, code);
}

void
GFX_inject_key(unsigned code, int down)
{
    if (code > KEY_CODE_MAX)
        return;
    queue_transition(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF, code);
}

/*
 *  ----------  The wheel  ----------
 *
 *  1983 had three buttons and no wheel, so there is no event word for one
 *  and no bit of `bitState' that means it.  Inventing either would put a
 *  transition in the ring for something that is not a transition: a notch is
 *  an amount, and two notches are twice as much rather than a second edge.
 *
 *  So it is a counter the window fills and the image drains -- read once,
 *  taken away, and any fraction kept for next time.  A notched mouse reports
 *  a whole 1.0 and a trackpad reports a tenth of one at a time; both add up
 *  here, and neither is rounded away.
 */
static float    wheel_pending;

void
GFX_inject_wheel(int notches)
{
    wheel_pending += (float) notches;
}

int
GFX_wheel_take(void)
{
    int whole = (int) wheel_pending;        /*  toward zero, either sign  */

    wheel_pending -= (float) whole;
    return whole;
}

#ifndef ST_HAVE_SDL3

/*  ----------  Headless build  ----------  */

int
GFX_open(const char *title, int width, int height, char *errbuf, size_t errlen)
{
    (void) title;
    (void) width;
    (void) height;
    if (errbuf && errlen)
        snprintf(errbuf, errlen, "built without SDL3");
    return -1;
}

void    GFX_close(void)     { }
int     GFX_pump(void)      { return 1; }
int     GFX_is_open(void)   { return 0; }
void    GFX_set_cursor(st_oop form) { (void) form; }
void    GFX_inject_expose(void) { }
void    GFX_note_blit(const gfx_blit *b) { (void) b; }
void    GFX_write_coverage(const char *p) { (void) p; }
void    GFX_warp_pointer(int x, int y) { warp_locally(x, y); }
void    GFX_present_if_undoing(const gfx_blit *b) { (void) b; }
char   *GFX_clipboard_text(void) { return NULL; }
int     GFX_clipboard_set(const char *text) { (void) text; return -1; }

/*
 *  Nothing to adopt.  The other half of this file rebuilds the texture and
 *  tells the image its screen changed size; here there is no texture and no
 *  window that could have changed, so a new display Form is simply the
 *  display Form.  GFX_set_display calls it unconditionally -- it is above
 *  the #ifdef and belongs to both halves -- so the stub has to exist, and
 *  its absence is what used to keep a headless build from linking at all.
 */
static void
adopt_display_extent(void)
{
}

/*
 *  main.c prints these after GFX_open succeeds, which here it never does,
 *  so nothing calls them in this build.  They are still declared in gfx.h
 *  and referenced from a translation unit that does not know which half was
 *  compiled, and the linker resolves references, not calls.  The answers
 *  are the ones gfx.h documents for "before there is a window".
 */
int
GFX_scale(void)
{
    return 1;
}

void
GFX_window_size(int *width, int *height)
{
    if (width)
        *width = 0;
    if (height)
        *height = 0;
}

const char *
GFX_presentation(void)
{
    return "none -- built without SDL3";
}

void
GFX_geometry(char *out, size_t len)
{
    if (out && len)
        snprintf(out, len, "no window -- built without SDL3");
}

#else   /*  ST_HAVE_SDL3  */

#include <SDL3/SDL.h>

static SDL_Window      *window;
static SDL_Renderer    *renderer;
static SDL_Texture     *texture;
static int              texture_w;
static int              texture_h;

static uint8_t *coverage;
static int      coverage_w;
static int      coverage_h;

static void
coverage_resize(int width, int height)
{
    if (width == coverage_w && height == coverage_h)
        return;
    free(coverage);
    coverage   = (uint8_t *) calloc((size_t) width * (size_t) height, 1);
    coverage_w = coverage ? width : 0;
    coverage_h = coverage ? height : 0;
}

/*
 *  The title, kept so ST_DISPLAY_TRACE can rebuild it after a resize.
 *
 *  A screenshot is what anyone sends when the screen looks wrong, and a
 *  screenshot of a window cannot say how many physical pixels its logical
 *  presentation was spread over -- which is the only number that decides
 *  whether a one-bit halftone comes out exact.  Three rounds of images went
 *  by before that was obvious.  So under ST_DISPLAY_TRACE the numbers go in
 *  the title bar, where the screenshot already is.
 */
static char             window_title[96];

/*  The pointer's current shape, kept so an unchanged one is not rebuilt.  */
static SDL_Cursor      *sdl_cursor;
static uint16_t         cursor_bits[16];
static int              cursor_hot_x = -1;
static int              cursor_hot_y = -1;

int
GFX_is_open(void)
{
    return window != NULL;
}

/*
 *  The system's clipboard.  What the text editor copies or cuts goes here
 *  as well as into the image's own CurrentSelection, and what it pastes
 *  comes from here when there is anything, so text moves between a
 *  workspace and the rest of the desktop in both directions.
 *
 *  SDL_GetClipboardText answers an SDL allocation, and an empty string
 *  rather than NULL when there is nothing; the copy handed back is a
 *  plain malloc so that prim.c need know nothing about SDL, and an empty
 *  clipboard is NULL, which is what `nothing there' means to the image.
 */
char *
GFX_clipboard_text(void)
{
    char   *text;
    char   *copy;
    size_t  n;

    if (!window)
        return NULL;
    text = SDL_GetClipboardText();
    if (!text)
        return NULL;
    n = strlen(text);
    if (n == 0) {
        SDL_free(text);
        return NULL;
    }
    copy = (char *) malloc(n + 1);
    if (copy)
        memcpy(copy, text, n + 1);
    SDL_free(text);
    return copy;
}

int
GFX_clipboard_set(const char *text)
{
    if (!window || !text)
        return -1;
    return SDL_SetClipboardText(text) ? 0 : -1;
}
/*
 *  Smalltalk-80 is one bit per pixel and a set bit is ink, so the whole of
 *  the palette is two colours.  1983 had no choice about which two; we do.
 *
 *  Pure #000 on pure #FFF is what the Dorado showed, and on a modern panel
 *  at three times the size it is a glaring white slab -- the same image is
 *  markedly easier to look at against warm paper, for the cost of two
 *  constants.  `classic' is there for anyone who wants the original, and
 *  `dark' inverts the pair, which a one-bit display can do exactly and a
 *  colour one cannot.
 */
static int      open_scale  = 1;
static int      base_scale  = 1;
static int      scale_forced;
static uint32_t pixel_ink   = 0xFF1B1815u;      /*  near-black, faintly warm */
static uint32_t pixel_paper = 0xFFF6F2E9u;      /*  paper rather than snow   */

static void
choose_theme(void)
{
    const char *name = getenv("ST_DISPLAY_THEME");

    if (!name || !name[0] || strcmp(name, "paper") == 0)
        return;                                 /*  the default, set above  */
    if (strcmp(name, "classic") == 0) {
        pixel_ink   = 0xFF000000u;
        pixel_paper = 0xFFFFFFFFu;
    }  else if (strcmp(name, "dark") == 0) {
        pixel_ink   = 0xFFD7D3C8u;
        pixel_paper = 0xFF1B1D22u;
    }  else {
        fprintf(stderr, "st80: unknown ST_DISPLAY_THEME '%s'; "
                        "known are paper, classic, dark\n", name);
    }
}

/*
 *  How many screen pixels to a display pixel.
 *
 *  The window used to open at the image's own size, which on the machines
 *  anybody now owns is a postage stamp: a 640x480 Smalltalk screen is under
 *  a tenth of a 4K panel, and the 1983 fonts are unreadable at that size.
 *  Resizing it did not help, because LETTERBOX presentation scales by
 *  whatever fraction the window happens to be -- at 1.7x a nearest-neighbour
 *  filter makes some pixels two wide and some three, which is the shimmer
 *  that makes the screen look broken rather than old.
 *
 *  So: an integer scale, chosen to fill about four fifths of the usable
 *  desktop, and INTEGER_SCALE presentation so it stays exact when resized.
 *  Every display pixel is then the same size as every other, which is what
 *  a one-bit screen needs to look deliberate.
 *
 *  What it decides is now narrower than that reads, because fitting arrived
 *  afterwards and answers the same question better.  With fitting on -- the
 *  default -- this picks HOW BIG THE WINDOW OPENS and nothing else: the
 *  desktop then fills that window at 1:1, and a postage stamp was only ever
 *  the complaint that the WINDOW was small.  It is still a magnification
 *  under ST_DISPLAY_FIT=off, where the screen stays 640x480 and something
 *  has to make it fill the window, and under an ST_DISPLAY_SCALE the user
 *  set.  fit_display_to_window says why the automatic one stops there.
 */
/*
 *  The window's size in PHYSICAL pixels, and the display's pixels-per-point.
 *
 *  Everything below counts in pixels, and it used to count in points.
 *  SDL_GetWindowSize answers screen coordinates, and on a desktop with no
 *  scaling set a point IS a pixel -- which is why this was invisible on
 *  Linux for as long as it was, and why the first machine to show it was a
 *  Windows desktop at other than 100%.
 *
 *  There the two diverge, and sizing the Form in points leaves one last hop
 *  to the panel that nobody here controls: a one-bit image resampled by a
 *  fraction.  Every display pixel is then NOT the same size as every other,
 *  which is the single thing choose_presentation below exists to guarantee.
 *  A 50% stipple is the worst thing to hand such a resample and the best at
 *  showing it: the desktop background came out evenly dithered down one side
 *  and washed out down the other.
 *
 *  So: one unit in this file, and it is the panel's.
 */
static void
window_pixels(int *w, int *h)
{
    int ww = 0, wh = 0;

    /*
     *  ASK THE RENDERER, not the window.
     *
     *  The number wanted here is the denominator of the scale: how many
     *  physical pixels the logical presentation is about to be spread over.
     *  Only the renderer knows it.  SDL_GetWindowSizeInPixels answers how
     *  big the window is, and the two are the same on most machines and not
     *  on all of them -- a swapchain can be point-sized under a window that
     *  measures in pixels, and then the Form is grown to a size the surface
     *  cannot hold and MINIFIED back down.
     *
     *  That is not a theory.  Sizing from the window turned the desktop
     *  halftone from a checkerboard resampled up by 1.5 -- alternating
     *  92 and 176 with a drifting amplitude -- into a flat 133 with a
     *  ripple of plus or minus 3: a 50% stipple averaged away, which is
     *  what minification does to it and nothing else does.
     *
     *  SDL_GetRenderOutputSize is the denominator by definition, whatever
     *  the platform thinks a window coordinate is.  Fall back to the window
     *  only before there is a renderer to ask.
     */
    if (renderer)
        SDL_GetRenderOutputSize(renderer, &ww, &wh);
    else if (window)
        SDL_GetWindowSizeInPixels(window, &ww, &wh);
    if (w)
        *w = ww;
    if (h)
        *h = wh;
}

/*  Pixels per point, before there is a window to ask.  1.0 unscaled.  */
static float
display_density(void)
{
    SDL_DisplayID   id = SDL_GetPrimaryDisplay();
    float           d  = id ? SDL_GetDisplayContentScale(id) : 0.0f;

    return d > 0.0f ? d : 1.0f;
}

static int
choose_scale(int width, int height)
{
    const char     *forced = getenv("ST_DISPLAY_SCALE");
    SDL_DisplayID   id;
    SDL_Rect        usable;
    int             scale = 1;

    if (forced && forced[0]) {
        int n = atoi(forced);

        scale_forced = 1;
        return n > 0 ? n : 1;
    }
    id = SDL_GetPrimaryDisplay();
    if (id != 0 && SDL_GetDisplayUsableBounds(id, &usable)) {
        /*
         *  The bounds come back in points and the scale is counted in
         *  pixels, so convert once here.  This is what keeps the desktop the
         *  same PHYSICAL size on a scaled display instead of shrinking it:
         *  at 150% the answer becomes 3 where it used to be 2, and the extra
         *  factor is spent on exact pixels rather than on a resample.
         *
         *  The ceiling is 8 and was 4.  Four was four points per display
         *  pixel; in pixels it would be two of them on a 200% desktop, which
         *  is the cap becoming twice as tight in the units anyone sees.
         *  Eight restores the reach it had.  The bounds test is what
         *  actually decides -- no ordinary screen goes near either number.
         */
        float   density = display_density();
        int     uw = (int) (usable.w * density);
        int     uh = (int) (usable.h * density);

        while (scale < 8
            && (scale + 1) * width  <= uw * 4 / 5
            && (scale + 1) * height <= uh * 4 / 5)
            ++scale;
    }
    return scale;
}


/*
 *  Integer scaling is the right default and it is not always available.
 *
 *  Integer presentation keeps every display pixel the same size as every
 *  other, which is what a one-bit screen needs to look deliberate rather
 *  than damaged -- a fractional scale with a nearest-neighbour filter makes
 *  some pixels two screen pixels wide and some three, and that shimmer is
 *  most of what reads as "awful".
 *
 *  But it can only use whole multiples, so it depends on getting a window
 *  of roughly the size asked for, and a TILING window manager does not give
 *  one.  Under i3 the request for 1280x960 came back as 956x1557: 640 does
 *  not go into 956 twice, so the scale collapses to 1 and a 640x480 screen
 *  sits in the middle of a tall tile with a border around three quarters of
 *  the area.  That is the right answer to the wrong question.
 *
 *  So the choice is made after seeing the window we actually got.  If whole
 *  multiples would leave most of it empty, fill the tile instead and accept
 *  the uneven pixels -- a screen that fills its window beats a crisp one
 *  adrift in a frame.  ST_DISPLAY_PRESENTATION=integer|letterbox|stretch
 *  overrides, for anyone who would rather have the other trade.
 */
static const char *presentation_note = "integer";

static SDL_RendererLogicalPresentation
choose_presentation(int width, int height)
{
    const char *forced = getenv("ST_DISPLAY_PRESENTATION");
    int         ww = 0, wh = 0;
    int         k;

    if (forced && forced[0]) {
        if (strcmp(forced, "letterbox") == 0) {
            presentation_note = "letterbox (forced)";
            return SDL_LOGICAL_PRESENTATION_LETTERBOX;
        }
        if (strcmp(forced, "stretch") == 0) {
            presentation_note = "stretch (forced)";
            return SDL_LOGICAL_PRESENTATION_STRETCH;
        }
        if (strcmp(forced, "integer") != 0)
            fprintf(stderr, "st80: unknown ST_DISPLAY_PRESENTATION '%s'; "
                            "known are integer, letterbox, stretch\n", forced);
        presentation_note = "integer (forced)";
        return SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
    }
    window_pixels(&ww, &wh);
    if (ww <= 0 || wh <= 0)
        return SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
    k = ww / width;
    if (wh / height < k)
        k = wh / height;
    if (k < 1)
        k = 1;
    /*
     *  Half the window is the line.  Below it the border dominates what you
     *  see, and filling matters more than the pixel grid.
     */
    if ((double) (k * width) * (k * height) < 0.5 * (double) ww * wh) {
        presentation_note = "letterbox (the window is not a whole multiple)";
        return SDL_LOGICAL_PRESENTATION_LETTERBOX;
    }
    presentation_note = "integer";
    return SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
}

/*
 *  Make the window and the image's own idea of the screen agree with the
 *  display Form as it now is.  Called when the Form is replaced or resized,
 *  from whichever side did it.
 */
static void
adopt_display_extent(void)
{
    gfx_form    form;

    if (!window || !renderer || !GFX_form_from_oop(display_form, &form))
        return;
    if (form.width == texture_w && form.height == texture_h)
        return;                         /*  already what we are showing  */
    if (texture)
        SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                form.width, form.height);
    if (!texture) {
        texture_w = texture_h = 0;
        return;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    texture_w = form.width;
    texture_h = form.height;
    SDL_SetRenderLogicalPresentation(renderer, form.width, form.height,
                                     choose_presentation(form.width,
                                                         form.height));
    coverage_resize(form.width, form.height);
    if (screen_hook)
        screen_hook(form.width, form.height);
}

/*
 *  Put the geometry in the title bar, when asked.  Called after every fit,
 *  because a resize is exactly when these numbers change.
 */
static void
retitle_if_tracing(void)
{
    char        geom[256];
    char        title[512];
    gfx_form    form;

    if (!window || !getenv("ST_DISPLAY_TRACE"))
        return;
    if (!GFX_form_from_oop(display_form, &form))
        return;
    GFX_geometry(geom, sizeof geom);
    snprintf(title, sizeof title, "%s -- form %dx%d at %dx, %s -- %s",
             window_title[0] ? window_title : "Smalltalk-2026",
             form.width, form.height, open_scale, presentation_note, geom);
    SDL_SetWindowTitle(window, title);
}

/*
 *  ----------  The screen is the window  ----------
 *
 *  Scaling and letterboxing both answer "how do I show a 640x480 screen in a
 *  window that is not 640x480", and on a tiling window manager neither
 *  answers it well: i3 hands back a 956x1557 tile, 4:3 does not fit a 0.61
 *  aspect at any scale, and better than half the tile stays border however
 *  the fitting is done.  The border is not the problem; the fixed screen is.
 *
 *  So resize the screen.  A Smalltalk display is a Form, a Form is three
 *  fields and a word array, and nothing in the image caches its extent -- the
 *  1983 code asks `Display extent' every time it wants to know.  Give the
 *  Form a bigger bitmap and the desktop simply becomes bigger, which is what
 *  every Smalltalk since has done and what the user asked for.
 *
 *  The Form's IDENTITY is kept.  `Display' is reachable from ScheduledControllers,
 *  from every controller and view, from Cursor, and from the VM state word a
 *  snapshot carries; replacing the object would strand all of them.  Only the
 *  three fields change.
 *
 *  The old pixels are copied in and the new area filled with the desktop
 *  halftone, so the screen is correct the instant it changes -- the image is
 *  never asked to redraw, and never learns that anything happened.  Windows
 *  stay where they were, and the new space is desktop.
 *
 *  Never SMALLER than the image already is in either axis: shrinking would
 *  put scheduled windows off the screen where no mouse can reach them.
 *  ST_DISPLAY_FIT=off keeps the old fixed-screen behaviour.
 */
static int
resize_display(int width, int height)
{
    gfx_form    form;
    st_oop      new_bits;
    st_oop      old_bits;
    uint32_t    raster = (uint32_t) ((width + 15) / 16);
    int         y;

    if (width <= 0 || height <= 0 || !GFX_form_from_oop(display_form, &form))
        return 0;
    if (form.width == width && form.height == height)
        return 1;
#ifdef ST_OM_BB
    /*
     *  A Blue Book object is addressed by a 16-bit length field, so a bitmap
     *  bigger than that cannot exist.  Say so and keep the screen we have.
     */
    if ((uint64_t) raster * (uint64_t) height > 65000u)
        return 0;
#endif
    /*
     *  The image first: if it cannot be told, do not change the screen.  A
     *  letterboxed 640x480 desktop is a much smaller disappointment than a
     *  desktop whose mouse does nothing.
     */
    if (!screen_hook || !screen_hook(width, height))
        return 0;
    old_bits = OM_fetch_pointer(ST_FORM_BITS, display_form);
    new_bits = OM_instantiate_words(OM_fetch_class(old_bits),
                                    raster * (uint32_t) height);
    if (!OM_is_present(new_bits))
        return 0;
    /*
     *  Re-read the form: allocating may have collected, and gfx_form holds a
     *  raw pointer into the old bitmap rather than an oop.
     */
    if (!GFX_form_from_oop(display_form, &form))
        return 0;
    {
        uint16_t   *dst  = OM_word_base(new_bits);
        uint32_t    keep = raster < (uint32_t) form.raster
                         ? raster : (uint32_t) form.raster;

        for (y = 0; y < height; ++y) {
            uint16_t   *row = dst + (size_t) y * raster;
            uint32_t    x;

            /*
             *  Form gray, in ITS phase.
             *
             *  Form class>>initializeMasks fills the odd rows of a 16x16
             *  mask with 21845 and the even ones with 43690, so row 0 is
             *  0x5555; and BitBlt indexes a halftone by the DESTINATION
             *  row, so Display row 0 takes halftone row 0.  Painting this
             *  the other way round is 50% grey either way and looks right
             *  on its own -- but every window erase fills with Form gray,
             *  so the rectangle a window used to occupy came back in the
             *  opposite phase and stood out as a patch of different
             *  texture.  Half the pixels differed and the ink COUNT was
             *  identical, which is why it read as debris rather than as a
             *  seam.
             */
            for (x = 0; x < raster; ++x)
                row[x] = (y & 1) ? 0xAAAAu : 0x5555u;
            if (y < form.height)
                memcpy(row, form.bits + (size_t) y * form.raster,
                       (size_t) keep * sizeof *row);
        }
    }
    OM_store_pointer(ST_FORM_BITS,   display_form, new_bits);
    OM_store_pointer(ST_FORM_WIDTH,  display_form, OM_int_oop((st_int) width));
    OM_store_pointer(ST_FORM_HEIGHT, display_form, OM_int_oop((st_int) height));

    if (texture)
        SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        texture_w = texture_h = 0;
        return 0;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    texture_w = width;
    texture_h = height;
    SDL_SetRenderLogicalPresentation(renderer, width, height,
                                     choose_presentation(width, height));
    coverage_resize(width, height);
    GFX_damage_all();
    return 1;
}

/*
 *  Choose the screen the window can hold.  The desktop fills the window, and
 *  the magnification is 1 unless somebody asked for one.
 */
static void
fit_display_to_window(void)
{
    const char *how = getenv("ST_DISPLAY_FIT");
    gfx_form    form;
    int         ww = 0, wh = 0;
    int         scale;

    if (how && (strcmp(how, "off") == 0 || strcmp(how, "0") == 0))
        return;
    if (!window || !renderer || !GFX_form_from_oop(display_form, &form))
        return;
    window_pixels(&ww, &wh);
    if (ww <= 0 || wh <= 0)
        return;
    /*
     *  ONE SCALE, AND IT DOES NOT DEPEND ON HOW THE WINDOW GOT HERE.
     *
     *  This used to hold the opening scale for as long as the window could
     *  still show the image's own screen at it, and drop a step when it
     *  could not:
     *
     *      for (scale = base_scale; scale > 1; --scale)
     *          if (ww / scale >= form.width && wh / scale >= form.height)
     *              break;
     *
     *  Two things are wrong with that, and together they made the size of
     *  everything on the screen a coin toss at startup.
     *
     *  The first is that the test is against the FORM, and the form only
     *  ever grows -- so the scale only ever falls, and can never climb back.
     *  One window size therefore has two answers: 2x if it opened there, 1x
     *  if it ever passed through a size that would not hold the screen
     *  doubled.  The scale was a function of the window's history and not of
     *  the window, which is why resizing once fixed it and fixed it for good.
     *
     *  The second is that the threshold lands in the middle of the sizes a
     *  tile actually takes.  base_scale is 2 on a 3840x1600 desktop, so 2x
     *  survived exactly when the tile was at least 1280 wide: i3's
     *  two-column tile is 1916 and kept it, its three-column tile is 1276 --
     *  638 a side, two pixels under the image's 640 -- and lost it.  Same
     *  machine, same image, same binary, and every window and every glyph
     *  twice the size it was the run before, decided by what else happened
     *  to be on the workspace.
     *
     *  So: 1:1, which is what "the screen is the window" says it should be.
     *  A bigger window is MORE DESKTOP, not the same desktop under a
     *  magnifier -- the Form is grown to fill the window a few lines below,
     *  and dividing here only undoes that.  The automatic scale still sizes
     *  the window at open, which is the job it is actually good at.
     *
     *  A scale the user ASKED for is still theirs, because they said what
     *  they wanted: ST_DISPLAY_SCALE=2 doubles as before, and
     *  ST_DISPLAY_FIT=off keeps the fixed 640x480 screen magnified to fill
     *  the window, which is where an automatic scale still means something.
     */
    scale = scale_forced ? base_scale : 1;
    open_scale = scale;
    {
        int lw = ww / scale;
        int lh = wh / scale;

        if (lw < form.width)
            lw = form.width;
        if (lh < form.height)
            lh = form.height;
        resize_display(lw, lh);
    }
    retitle_if_tracing();
}

int
GFX_open(const char *title, int width, int height, char *errbuf, size_t errlen)
{
    int scale;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        if (errbuf)
            snprintf(errbuf, errlen, "SDL_Init: %s", SDL_GetError());
        return -1;
    }
    choose_theme();
    scale = choose_scale(width, height);
    {
        /*
         *  ST_DISPLAY_WINDOW=WxH opens at an explicit size.  A tiling window
         *  manager ignores what we ask for and hands back its tile, so this
         *  is how that case is reproduced anywhere else -- and how anyone who
         *  wants a particular desktop size can just say so.
         */
        const char *want    = getenv("ST_DISPLAY_WINDOW");
        float       density = display_density();
        int         ww = 0, wh = 0;

        if (!(want && sscanf(want, "%dx%d", &ww, &wh) == 2
              && ww > 0 && wh > 0)) {
            ww = width  * scale;
            wh = height * scale;
        }
        /*
         *  SDL_CreateWindow takes points; the numbers above are pixels, as
         *  everything in this file now is.  Divide once, here, and let
         *  SDL_GetWindowSizeInPixels answer for everything after.
         *  ST_DISPLAY_WINDOW is pixels too, for the same reason.
         *
         *  HIGH_PIXEL_DENSITY is what makes the drawable the panel's own
         *  size rather than a point-sized one the compositor stretches.
         *  Without it there is nothing this code can do about the last hop,
         *  because the last hop is not ours.
         */
        ww = (int) (ww / density);
        wh = (int) (wh / density);
        if (ww < 1) ww = 1;
        if (wh < 1) wh = 1;
        snprintf(window_title, sizeof window_title, "%s",
                 title ? title : "Smalltalk-2026");
        window = SDL_CreateWindow(title, ww, wh,
                                  SDL_WINDOW_RESIZABLE
                                      | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    }
    if (!window) {
        if (errbuf)
            snprintf(errbuf, errlen, "SDL_CreateWindow: %s", SDL_GetError());
        return -1;
    }
    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        if (errbuf)
            snprintf(errbuf, errlen, "SDL_CreateRenderer: %s", SDL_GetError());
        return -1;
    }
    SDL_SetRenderLogicalPresentation(renderer, width, height,
                                     choose_presentation(width, height));

    /*
     *  Streaming access is the documented fast path for pixels that change
     *  every frame, which is exactly what a Smalltalk display is.
     *  SDL_UpdateTexture is explicitly described as slow for this use.
     */
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        if (errbuf)
            snprintf(errbuf, errlen, "SDL_CreateTexture: %s", SDL_GetError());
        return -1;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    texture_w = width;
    texture_h = height;
    open_scale = scale;
    base_scale = scale;
    {
        gfx_form    form;

        if (GFX_form_from_oop(display_form, &form))
            coverage_resize(form.width, form.height);
    }
    /*
     *  The window manager has now told us what we actually got, which is the
     *  first moment the right screen size is knowable.
     */
    fit_display_to_window();
    retitle_if_tracing();
    GFX_damage_all();
    return 0;
}

void
GFX_close(void)
{
    free(coverage);
    coverage = NULL;
    coverage_w = coverage_h = 0;
    if (sdl_cursor)
        SDL_DestroyCursor(sdl_cursor);
    sdl_cursor = NULL;
    if (texture)
        SDL_DestroyTexture(texture);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    texture  = NULL;
    renderer = NULL;
    window   = NULL;
    SDL_Quit();
}

int
GFX_scale(void)
{
    return open_scale;
}

const char *
GFX_presentation(void)
{
    return presentation_note;
}

void
GFX_window_size(int *width, int *height)
{
    /*  Pixels, so the startup line reports the unit the scale is in.  */
    window_pixels(width, height);
}

/*
 *  Every number the scale arithmetic depends on, in one line.
 *
 *  Three quantities that are equal on an unscaled desktop and need not be
 *  anywhere else -- the window as the window manager counts it, the window
 *  in physical pixels, and the surface the renderer will actually present
 *  onto.  When the halftone comes out wrong it is because two of these
 *  disagree and the code believed the wrong one, so the fastest way to a
 *  diagnosis is to print all three and stop guessing which.
 */
void
GFX_geometry(char *out, size_t len)
{
    int     pw = 0, ph = 0;         /*  window, as SDL counts windows  */
    int     xw = 0, xh = 0;         /*  window, in physical pixels     */
    int     rw = 0, rh = 0;         /*  what the renderer draws onto   */
    float   density = 0.0f;

    if (!out || !len)
        return;
    if (!window) {
        snprintf(out, len, "no window");
        return;
    }
    SDL_GetWindowSize(window, &pw, &ph);
    SDL_GetWindowSizeInPixels(window, &xw, &xh);
    if (renderer)
        SDL_GetRenderOutputSize(renderer, &rw, &rh);
    density = SDL_GetWindowPixelDensity(window);
    snprintf(out, len,
             "window %dx%d, in pixels %dx%d, render target %dx%d, "
             "density %.2f, display scale %.2f",
             pw, ph, xw, xh, rw, rh, (double) density,
             (double) display_density());
}

/*
 *  ----------  The pointer  ----------
 *
 *  A Smalltalk Cursor is a 16 by 16 Form whose `offset' is the hot spot,
 *  negated -- Cursor origin offsets by 0@0 and points at its own top left,
 *  Cursor corner offsets by -15@-15 and points at its bottom right.  SDL
 *  wants two MSB-first bit planes, so the Form's words go across unchanged
 *  and the mask is the shape itself: ink where a bit is set, transparent
 *  everywhere else, which is exactly what a one-plane cursor meant in 1983.
 *
 *  Only ever called with a window open, so it is only ever called from the
 *  thread that pumps SDL.  A headless run -- which is every test -- reaches
 *  the stub above and touches nothing.
 */
/*
 *  The window system's pointer follows the image's, so the two never
 *  disagree about where it is.  SDL reports the warp back as a motion event;
 *  warp_locally has already recorded the same position, so handle_mouse_motion
 *  sees no change and queues nothing twice.
 */
void
GFX_inject_expose(void)
{
    SDL_Event   e;

    if (!window)
        return;
    SDL_zero(e);
    e.type = SDL_EVENT_WINDOW_EXPOSED;
    e.window.windowID = SDL_GetWindowID(window);
    SDL_PushEvent(&e);
}

/*
 *  The position the image last warped to, while its echo is outstanding.
 *
 *  SDL_WarpMouseInWindow does not move the pointer silently: it posts a
 *  motion event, and that event arrives back here through
 *  SDL_ConvertEventToRenderCoordinates.  Display to window and back is the
 *  identity only while the scale is 1 -- letterboxed at a fractional scale
 *  a warp to 140,148 came back as 139,147 -- so the echo overwrote the
 *  position the image had just chosen, and queued a motion word nobody
 *  asked for.  The note on GFX_inject_expose says this cannot happen
 *  because warp_locally recorded the same position first; that holds only
 *  at scale 1.
 *
 *  Sensor cursorPoint: is how the image puts the pointer on a grid point,
 *  or on the hot spot of a cursor it is about to show -- Cursor>>show goes
 *  through it whenever the new shape's offset differs, which is every time
 *  a BinaryChoice switch swaps thumbs up for thumbs down.  Landing a pixel
 *  away from the asked-for point each time is a drift the image cannot see
 *  and cannot correct.
 *
 *  So a warp's own echo is answered with the position that was asked for.
 *  Only the first motion after a warp, and only when it lands within a
 *  pixel of the target: anything further away is the user's own hand and is
 *  reported as it arrived.
 */
static int  warp_echo_pending;
static int  warp_echo_x;
static int  warp_echo_y;

void
GFX_warp_pointer(int x, int y)
{
    warp_locally(x, y);
    if (window && renderer) {
        float   wx = 0.0f, wy = 0.0f;

        SDL_RenderCoordinatesToWindow(renderer, (float) x, (float) y,
                                      &wx, &wy);
        /*
         *  Recorded from mouse_x rather than from x: warp_locally clamps to
         *  the display, and the echo will arrive clamped too.
         */
        warp_echo_pending = 1;
        warp_echo_x       = mouse_x;
        warp_echo_y       = mouse_y;
        SDL_WarpMouseInWindow(window, wx, wy);
    }
}

void
GFX_set_cursor(st_oop form)
{
    gfx_form    shape;
    Uint8       plane[32];
    SDL_Cursor *made;
    st_oop      offset;
    int         hot_x = 0, hot_y = 0;
    int         y;

    if (!window || !GFX_form_from_oop(form, &shape))
        return;
    if (shape.width != 16 || shape.height != 16 || shape.raster != 1)
        return;                         /*  not a cursor we can hand over  */

    offset = OM_fetch_pointer(ST_FORM_OFFSET, form);
    if (OM_is_present(offset) && OM_pointer_bit(offset)
     && OM_fetch_word_length(offset) >= 2) {
        st_oop  x = OM_fetch_pointer(0, offset);
        st_oop  y_oop = OM_fetch_pointer(1, offset);

        if (OM_is_int(x))
            hot_x = -(int) OM_int_value(x);
        if (OM_is_int(y_oop))
            hot_y = -(int) OM_int_value(y_oop);
    }
    if (hot_x < 0)  hot_x = 0;
    if (hot_y < 0)  hot_y = 0;
    if (hot_x > 15) hot_x = 15;
    if (hot_y > 15) hot_y = 15;

    /*  The same shape twice running is the common case: Cursor normal show
     *  goes past here after every menu.  Rebuilding it would be a syscall
     *  and a flicker for nothing.  */
    if (hot_x == cursor_hot_x && hot_y == cursor_hot_y
     && memcmp(cursor_bits, shape.bits, sizeof cursor_bits) == 0)
        return;

    for (y = 0; y < 16; ++y) {
        uint16_t    row = shape.bits[y];

        plane[y * 2]     = (Uint8) (row >> 8);
        plane[y * 2 + 1] = (Uint8) (row & 0xFF);
    }
    made = SDL_CreateCursor(plane, plane, 16, 16, hot_x, hot_y);
    if (!made)
        return;
    SDL_SetCursor(made);
    if (sdl_cursor)
        SDL_DestroyCursor(sdl_cursor);
    sdl_cursor = made;
    if (getenv("ST_DISPLAY_TRACE"))
        fprintf(stderr, "st80: cursor shape changed, hot spot %d,%d\n",
                hot_x, hot_y);
    memcpy(cursor_bits, shape.bits, sizeof cursor_bits);
    cursor_hot_x = hot_x;
    cursor_hot_y = hot_y;
}

/*
 *  ----------  The antialiased shadow  ----------
 *
 *  The Smalltalk display is one bit a pixel and must stay that way: BitBlt
 *  is the Blue Book's, byte for byte against the Xerox traces, and every
 *  rule the image draws with -- over, under, reverse, the gray halftones --
 *  is defined on single bits.  Giving Form a depth would be a different
 *  system.  So the smooth text is not IN the image and the image cannot see
 *  it: this is a second plane, ink coverage from 0 to 255, that the window
 *  is painted from where it has anything to say.
 *
 *  It is filled by recognising text.  A blit whose source is exactly the
 *  strike -- ST_FONT_STRIKE_WIDTH by ST_FONT_HEIGHT, which nothing else in
 *  the system is -- is a character being drawn, and the source x says which
 *  one, because that is precisely what the xTable means.  So the same glyph
 *  is stamped from ST_FONT_COVERAGE at the same advance, and the two agree
 *  by construction: the image lays out from the one-bit strike, and the
 *  screen shows the eight-bit one.
 *
 *  Anything else that lands on the display DROPS the coverage under it.
 *  That is the conservative half and it is what keeps this honest -- scroll
 *  a pane, clear a window, invert a selection, and the shadow gives up and
 *  the one-bit pixels show through.  Text comes back smooth the moment it is
 *  drawn again.  The alternative, trying to model what every rule does to
 *  coverage, is how you end up with a second graphics system that disagrees
 *  with the first.
 */
/*
 *  The coverage plane beside a screenshot, so what the window would have
 *  shown can be looked at without a window.  Same discipline as the .pbm.
 */
void
GFX_write_coverage(const char *shot_path)
{
    char    path[512];
    FILE   *f;
    size_t  n;

    if (!shot_path || !coverage)
        return;
    snprintf(path, sizeof path, "%s", shot_path);
    n = strlen(path);
    if (n > 4 && strcmp(path + n - 4, ".pbm") == 0)
        snprintf(path + n - 4, 5, ".cov");
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(coverage, 1, (size_t) coverage_w * (size_t) coverage_h, f);
    fclose(f);
}

void
GFX_note_blit(const gfx_blit *b)
{
    int x, y;

    if (!coverage || b->damage_w <= 0 || b->damage_h <= 0)
        return;
    if (b->damage_x < 0 || b->damage_y < 0
     || b->damage_x + b->damage_w > coverage_w
     || b->damage_y + b->damage_h > coverage_h)
        return;

    if (b->has_source
     && b->source.width == ST_FONT_STRIKE_WIDTH
     && b->source.height == ST_FONT_HEIGHT) {
        /*
         *  A character.  The clipped rectangle may be a part of it, so the
         *  source corner is taken from how far the destination moved rather
         *  than from the blit's own source fields, which the copy advanced.
         */
        int sx = b->source_x + (b->damage_x - b->dest_x);
        int sy = b->source_y + (b->damage_y - b->dest_y);

        for (y = 0; y < b->damage_h; ++y) {
            int         gy = sy + y;
            uint8_t    *row = coverage + (size_t) (b->damage_y + y)
                                       * coverage_w + b->damage_x;

            if (gy < 0 || gy >= ST_FONT_HEIGHT) {
                memset(row, 0, (size_t) b->damage_w);
                continue;
            }
            for (x = 0; x < b->damage_w; ++x) {
                int gx = sx + x;

                row[x] = (gx >= 0 && gx < ST_FONT_STRIKE_WIDTH)
                             ? ST_FONT_COVERAGE[gy][gx] : 0;
            }
        }
        return;
    }
    for (y = 0; y < b->damage_h; ++y)
        memset(coverage + (size_t) (b->damage_y + y) * coverage_w
                        + b->damage_x, 0, (size_t) b->damage_w);
}

/*
 *  Ink over paper at the given coverage, per channel.  The palette is the
 *  theme's, so this stays right in dark mode, where "more coverage" is a
 *  lighter pixel and the arithmetic must not assume otherwise.
 */
static uint32_t
blend_ink(unsigned c)
{
    uint32_t    out = 0xFF000000u;
    int         shift;

    for (shift = 16; shift >= 0; shift -= 8) {
        unsigned    ink   = (pixel_ink   >> shift) & 0xFF;
        unsigned    paper = (pixel_paper >> shift) & 0xFF;

        out |= (uint32_t) ((ink * c + paper * (255 - c)) / 255) << shift;
    }
    return out;
}

static void
present(void)
{
    gfx_form    form;
    SDL_Rect    rect;
    void       *pixels;
    int         pitch;
    int         y;

    if (!damage_valid || !texture)
        return;
    ++present_calls;
    if (!GFX_form_from_oop(display_form, &form))
        return;

    if (damage_x1 < 0)
        damage_x1 = 0;
    if (damage_y1 < 0)
        damage_y1 = 0;
    if (damage_x2 > form.width)
        damage_x2 = form.width;
    if (damage_y2 > form.height)
        damage_y2 = form.height;
    if (damage_x2 <= damage_x1 || damage_y2 <= damage_y1) {
        damage_valid = 0;
        return;
    }
    /*  The texture was made for the form we opened with.  */
    if (form.width != texture_w || form.height != texture_h) {
        damage_valid = 0;
        return;
    }

    rect.x = damage_x1;
    rect.y = damage_y1;
    rect.w = damage_x2 - damage_x1;
    rect.h = damage_y2 - damage_y1;

    if (!SDL_LockTexture(texture, &rect, &pixels, &pitch)) {
        damage_valid = 0;
        return;
    }
    for (y = 0; y < rect.h; ++y) {
        const uint16_t *row = form.bits + (size_t) (rect.y + y) * form.raster;
        uint32_t       *out = (uint32_t *) ((unsigned char *) pixels
                                            + (size_t) y * (size_t) pitch);
        int             x;

        /*
         *  Bit 15 of a word is its leftmost pixel, so the shift counts down
         *  from the top of each word.
         */
        const uint8_t *cov = coverage
                           ? coverage + (size_t) (rect.y + y) * coverage_w
                           : NULL;

        for (x = 0; x < rect.w; ++x) {
            int         bit  = rect.x + x;
            uint16_t    word = row[bit >> 4];
            unsigned    c    = cov ? cov[bit] : 0;

            /*
             *  Coverage speaks where it has something to say; elsewhere the
             *  one bit does.  A pixel a glyph did not touch is 0 either way,
             *  so the two never argue about the space between letters.
             */
            if (c == 0)
                out[x] = ((word >> (15 - (bit & 15))) & 1) ? pixel_ink
                                                           : pixel_paper;
            else if (c == 255)
                out[x] = pixel_ink;
            else
                out[x] = blend_ink(c);
        }
    }
    SDL_UnlockTexture(texture);
    damage_valid = 0;

    /*
     *  The bars are PAPER, not black.
     *
     *  Integer scaling means the display fills the window only when the
     *  window is an exact multiple of it, and a window manager will hand
     *  back whatever size it likes -- so there is usually a border.  Cleared
     *  to the renderer's default black it reads as a heavy frame around a
     *  small screen, which is the first thing anyone notices and dislikes.
     *  In the paper colour the border is simply where the paper runs out.
     */
    SDL_SetRenderDrawColor(renderer,
                           (Uint8) ((pixel_paper >> 16) & 0xFF),
                           (Uint8) ((pixel_paper >> 8) & 0xFF),
                           (Uint8) (pixel_paper & 0xFF),
                           0xFF);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/*
 *  ----------  The 1983 flash  ----------
 *
 *  StandardSystemView>>getFrame draws the rubber band you drag a new window
 *  out with, and it does this, once per turn round its tracking loop:
 *
 *      Display fill: frame rule: Form reverse mask: Form gray.
 *      Display fill: frame rule: Form reverse mask: Form gray.
 *
 *  Twice, with the same rectangle and the same reversing rule -- so the two
 *  cancel and the rectangle is NEVER left on the Form.  It is not a drawing,
 *  it is a flash, and on the Alto that was enough: the CRT scanned the
 *  display memory continuously, so whatever was there during the gap between
 *  the two fills was on the glass.
 *
 *  Here the Form is the truth and the window is a copy of it taken between
 *  bytecode slices.  Measured over one framing drag: 3474 draws to the
 *  display, 67 of them presented.  Two per cent -- and the state worth
 *  seeing lasts a handful of bytecodes out of thousands, so what reaches the
 *  screen is the rectangle appearing at random and not while it tracks the
 *  pointer.  That is the blinking.
 *
 *  So present at the right moment rather than more often: a reversing blit
 *  that exactly repeats the one before it, on the display, is by
 *  construction an undo, and the frame worth showing is the one before it
 *  lands.  Recognising that idiom is not a trick played on the image -- it
 *  is what a continuously scanned display did for free, done on a display
 *  that has to be told.  It costs one present per flash, which is bounded by
 *  the drag loop and far below presenting everything.
 */
#define RULE_REVERSE    6       /*  Form reverse: source XOR dest  */

/*
 *  Keyed on what the image ASKED for, not on what was touched: the clipped
 *  rectangle is computed inside GFX_copy_bits, and this has to decide before
 *  the copy runs.  The request is the better key anyway -- it is the thing
 *  the image repeats verbatim.
 */
static struct {
    st_oop  dest;
    st_oop  source;
    st_oop  halftone;
    int     valid;
    int     x, y, w, h;
    int     cx, cy, cw, ch;
    int64_t when;
} last_blit;

/*
 *  Two clocks, because the flash and the pump want the screen at once.
 *
 *  Unbounded, a flash present fires as fast as the drag loop turns -- 1351
 *  of them in one drag here -- and each one converts its damaged rectangle a
 *  pixel at a time, which on a full-screen frame is milliseconds.  So it is
 *  held to the refresh rate.
 *
 *  And while a drag is running the pump's own present must stay out of the
 *  way: it would land on the UNDONE state and take the rectangle straight
 *  back off the screen, which is the blinking again by another route.  A
 *  recent flash therefore owns the display, and the pump resumes as soon as
 *  the flashing stops.
 */
#define PRESENT_INTERVAL_NS     INT64_C(16000000)    /*  ~60 Hz  */
#define FLASH_OWNS_SCREEN_NS    INT64_C(50000000)    /*  50 ms   */

/*
 *  How long after a reverse its twin still counts as the undo half of a pair.
 *
 *  Shape cannot say what a pair of identical reverses MEANS, because two
 *  unrelated places draw one and they want opposite things.
 *
 *  StandardSystemView>>getFrame rubber-bands a window's frame with two
 *  statements and nothing between them:
 *
 *      Display fill: frame rule: Form reverse mask: Form gray.
 *      Display fill: frame rule: Form reverse mask: Form gray.
 *
 *  The band exists only for the gap between those two sends -- measured at
 *  0.017 ms, with 1494 of one drag's 1495 pairs inside 0.029 ms and the
 *  slowest at 3.3 ms.  Nothing would ever upload that, so the drawn half has
 *  to be forced out before the undo lands, and the pump held off it.
 *
 *  DisplayScreen>>flash: draws the same shape and means the opposite: two
 *  reverses of one rectangle with `(Delay forMilliseconds: 60) wait' between
 *  them, measured at 60 to 61 ms every time, in every caller.  Both halves
 *  are meant to be seen.  Forcing one out and then pinning the pump off the
 *  other for FLASH_OWNS_SCREEN_NS is what left the confirm dialog's yes/no
 *  switches in reverse video until the pointer came back.
 *
 *  So the rule is about sampling rather than intent: a state that came and
 *  went inside a frame would never be uploaded and must be forced; a state
 *  the image held for longer is one the pump will sample on its own, and
 *  anything forced there only pins half a flash to the screen.
 *
 *  Eight milliseconds is twice the slowest band and a seventh of the fastest
 *  flash, and stays under PRESENT_INTERVAL_NS so a band that straddles a
 *  frame still counts as one pair.
 */
#define UNDO_PAIR_WINDOW_NS     INT64_C(8000000)     /*  8 ms    */

static int64_t  last_flash_ns;

static int
flash_owns_screen(void)
{
    return last_flash_ns != 0
        && ST_time_monotonic_ns() - last_flash_ns < FLASH_OWNS_SCREEN_NS;
}

void
GFX_present_if_undoing(const gfx_blit *b)
{
    int     same;
    int64_t now;

    /*
     *  The window check comes FIRST and writes nothing.  copyBits runs on
     *  every worker, and a headless run -- which is every test -- must not
     *  touch this state at all; with a window there is one thread, the same
     *  one that pumps SDL.  Guarding the writes behind "a window is open" is
     *  what keeps that true, and is the same rule GFX_set_cursor follows.
     */
    if (!window)
        return;
    if (b->dest.oop != display_form || b->rule != RULE_REVERSE) {
        last_blit.valid = 0;
        return;
    }
    now  = ST_time_monotonic_ns();
    same = last_blit.valid
        && now - last_blit.when < UNDO_PAIR_WINDOW_NS
        && last_blit.dest     == b->dest.oop
        && last_blit.source   == (b->has_source ? b->source.oop : ST_NIL)
        && last_blit.halftone == (b->has_halftone ? b->halftone.oop : ST_NIL)
        && last_blit.x  == b->dest_x && last_blit.y  == b->dest_y
        && last_blit.w  == b->width  && last_blit.h  == b->height
        && last_blit.cx == b->clip_x && last_blit.cy == b->clip_y
        && last_blit.cw == b->clip_w && last_blit.ch == b->clip_h;

    if (same) {
        if (now - last_flash_ns >= PRESENT_INTERVAL_NS) {
            present();
            last_flash_ns = now;
        }
        last_blit.valid = 0;    /*  a pair is two, not a run  */
        return;
    }
    last_blit.dest     = b->dest.oop;
    last_blit.source   = b->has_source ? b->source.oop : ST_NIL;
    last_blit.halftone = b->has_halftone ? b->halftone.oop : ST_NIL;
    last_blit.x  = b->dest_x;
    last_blit.y  = b->dest_y;
    last_blit.w  = b->width;
    last_blit.h  = b->height;
    last_blit.cx = b->clip_x;
    last_blit.cy = b->clip_y;
    last_blit.cw = b->clip_w;
    last_blit.ch = b->clip_h;
    last_blit.when  = now;
    last_blit.valid = 1;
}

/*
 *  Mouse buttons carry the Smalltalk-80 bi-state codes.  The image reads
 *  them as bits 0 to 2 of its bitState, and its own accessors name those
 *  bits blue, yellow and red -- so the left button, which selects, is red
 *  and therefore code 130.
 */
#define BUTTON_BLUE     128
#define BUTTON_YELLOW   129
#define BUTTON_RED      130

static unsigned
sdl_button_code(int button)
{
    if (button == SDL_BUTTON_LEFT)
        return BUTTON_RED;
    if (button == SDL_BUTTON_MIDDLE)
        return BUTTON_YELLOW;
    return BUTTON_BLUE;
}

static void
handle_mouse_motion(float x, float y)
{
    gfx_form    form;
    int         nx = (int) x;
    int         ny = (int) y;

    if (!GFX_form_from_oop(display_form, &form))
        return;
    if (nx < 0)
        nx = 0;
    if (ny < 0)
        ny = 0;
    if (nx >= form.width)
        nx = form.width - 1;
    if (ny >= form.height)
        ny = form.height - 1;
    if (warp_echo_pending) {
        int dx = nx - warp_echo_x;
        int dy = ny - warp_echo_y;

        warp_echo_pending = 0;
        if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
            nx = warp_echo_x;
            ny = warp_echo_y;
        }
    }
    queue_motion(nx, ny);
}

int
GFX_pump(void)
{
    SDL_Event   event;

    if (!window)
        return 1;
    while (SDL_PollEvent(&event)) {
        /*
         *  Put the event's coordinates into the display's own space.
         *
         *  The renderer presents a 640 by 480 logical surface letterboxed
         *  into whatever size the window happens to be, and the window is
         *  resizable -- so SDL reports a pointer position in WINDOW pixels
         *  while everything above here counts in the Form's.  The two agree
         *  only when the window is exactly the size of the display and the
         *  screen is not scaled, which is to say on this machine and not on
         *  the next one.
         *
         *  Unconverted, the pointer the image believes in drifts further
         *  from the real one the further from the origin it goes, and by a
         *  factor rather than an offset.  It shows up as a menu that opens
         *  somewhere other than under the cursor, which is a hard thing to
         *  read as a coordinate-space mistake.
         */
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return 0;

        /*
         *  Safe here and nowhere else: GFX_pump runs on the same thread as
         *  the interpreter and only between bytecode slices, so no Smalltalk
         *  code is part-way through reading the Form we are about to change.
         */
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            fit_display_to_window();
            break;

        /*
         *  The window came back and has to be told to repaint itself.
         *
         *  present() uploads the DAMAGED region and then clears the damage,
         *  which is right while somebody is looking.  Switch to another
         *  desktop and the image goes on drawing, so every frame is
         *  presented into a window nobody can see and the damage is dropped
         *  with it.  Come back and there is nothing outstanding to present,
         *  so the window keeps whatever the compositor kept -- until the
         *  image happens to draw again, which is why moving the mouse inside
         *  it fixes the symptom.  Any damage at all repaints the whole
         *  window, because present() renders the entire texture.
         *
         *  So say the whole screen is damaged whenever the window is shown,
         *  restored or exposed.  It costs one full upload, and only when the
         *  window manager says the window has reappeared.
         */
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
            GFX_damage_all();
            break;

        case SDL_EVENT_MOUSE_MOTION:
            handle_mouse_motion(event.motion.x, event.motion.y);
            break;

        /*
         *  Away from the user is up, and FLIPPED means the window system has
         *  already reversed it for the user's preference -- so put it back
         *  and let one sign mean one direction everywhere.
         */
        case SDL_EVENT_MOUSE_WHEEL:
            wheel_pending += (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                           ? -event.wheel.y : event.wheel.y;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            unsigned    code = sdl_button_code(event.button.button);
            int         down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);

            handle_mouse_motion(event.button.x, event.button.y);
            if (down)
                button_state |= 1 << (int) (code - BUTTON_BLUE);
            else
                button_state &= ~(1 << (int) (code - BUTTON_BLUE));
            queue_transition(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF,
                        code);
            break;
        }

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            int         down = (event.type == SDL_EVENT_KEY_DOWN);
            SDL_Keycode key  = event.key.key;
            unsigned    code;

            /*
             *  Only the ASCII range maps directly; the image's keyboard map
             *  handles the rest and the modifier keys have their own codes.
             */
            if (key == SDLK_LSHIFT)
                code = 136;
            else if (key == SDLK_RSHIFT)
                code = 137;
            else if (key == SDLK_LCTRL || key == SDLK_RCTRL)
                code = 138;
            else if (key == SDLK_CAPSLOCK)
                code = 139;
            else if (key == SDLK_RETURN)
                code = 13;
            else if (key == SDLK_BACKSPACE)
                code = 8;
            else if (key == SDLK_TAB)
                code = 9;
            else if (key == SDLK_ESCAPE)
                code = 27;
            /*
             *  The four keys the Alto keyboard did not have.  Their SDL
             *  keycodes are all far above 255, so they are given codes of
             *  their own inside the range InputState>>keyAt:put: passes
             *  through to the keyboard queue: above the mouse and keyset bits
             *  at 128-135 and the modifier keys at 136-139, and clear of
             *  140-145, which the 1983 keyboard map already spends on the
             *  Alto's own special keys.
             *
             *  lib/Keyboard-Map is the other half and has to agree with this
             *  list: InputSensor>>initMap makes these four decode to
             *  themselves instead of to `unassigned', and
             *  ParagraphEditor>>normalCharacter: moves the caret rather than
             *  typing them.  InputSensor class>>cursorKeyCodes is where the
             *  image says the same four numbers.
             */
            else if (key == SDLK_LEFT)
                code = 152;
            else if (key == SDLK_RIGHT)
                code = 153;
            else if (key == SDLK_UP)
                code = 154;
            else if (key == SDLK_DOWN)
                code = 155;
            else if (key < 128)
                code = (unsigned) key;
            else
                break;
            queue_transition(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF,
                        code);
            break;
        }

        default:
            break;
        }
    }
    if (!flash_owns_screen())
        present();
    return 1;
}

#endif  /*  ST_HAVE_SDL3  */

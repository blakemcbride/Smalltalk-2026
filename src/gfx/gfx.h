/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Graphics: BitBlt, the display, and the SDL3 front end.
 */

#ifndef ST_GFX_H
#define ST_GFX_H

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

/*  ----------  Image-side layouts  ----------
 *
 *  Confirmed against the version 2 sources and the image's own format words:
 *  Form has 4 named instance variables, BitBlt has 14.
 *
 *      DisplayObject -> DisplayMedium -> Form   'bits width height offset'
 *      Object -> BitBlt                         'destForm sourceForm
 *          halftoneForm combinationRule destX destY width height sourceX
 *          sourceY clipX clipY clipWidth clipHeight'
 */
#define ST_FORM_BITS                0
#define ST_FORM_WIDTH               1
#define ST_FORM_HEIGHT              2
#define ST_FORM_OFFSET              3

#define ST_BITBLT_DEST_FORM         0
#define ST_BITBLT_SOURCE_FORM       1
#define ST_BITBLT_HALFTONE_FORM     2
#define ST_BITBLT_RULE              3
#define ST_BITBLT_DEST_X            4
#define ST_BITBLT_DEST_Y            5
#define ST_BITBLT_WIDTH             6
#define ST_BITBLT_HEIGHT            7
#define ST_BITBLT_SOURCE_X          8
#define ST_BITBLT_SOURCE_Y          9
#define ST_BITBLT_CLIP_X            10
#define ST_BITBLT_CLIP_Y            11
#define ST_BITBLT_CLIP_WIDTH        12
#define ST_BITBLT_CLIP_HEIGHT       13

/*  ----------  A form, resolved for bulk access  ----------  */

typedef struct {
    st_oop      oop;
    uint16_t   *bits;       /*  valid until the next allocation  */
    uint32_t    words;
    int         width;
    int         height;
    int         raster;     /*  words per scan line  */
} gfx_form;

typedef struct {
    gfx_form    dest;
    gfx_form    source;
    gfx_form    halftone;
    int         has_source;
    int         has_halftone;
    unsigned    rule;

    /*  As requested by the image.  */
    int         dest_x, dest_y, width, height;
    int         source_x, source_y;
    int         clip_x, clip_y, clip_w, clip_h;

    /*  As clipped, which is what the copy loop uses.  */
    int         dx, dy, w, h, sx, sy;

    /*
     *  The clipped destination rectangle, captured before the copy loop
     *  runs.  The loop advances dy as it walks scan lines, so the caller
     *  cannot read the damaged region back out of dx and dy afterwards.
     */
    int         damage_x, damage_y, damage_w, damage_h;
} gfx_blit;

int     GFX_form_from_oop(st_oop form, gfx_form *out);
int     GFX_blit_from_oop(st_oop bitblt, gfx_blit *b);
void    GFX_copy_bits(gfx_blit *b);

/*  ----------  The display  ----------  */

/*
 *  The Smalltalk Form the image draws into is the authoritative pixel store.
 *  The window is a view of it: thread 0 copies damaged regions out and
 *  presents them, and never writes back.
 */
/*
 *  Post input as though it came from the window.
 *
 *  Buttons use the Smalltalk codes: 128 is blue (the leftmost), 129 yellow,
 *  130 red.  These take the same path SDL's own events do, which is the point
 *  of them -- a test that drove a private queue would prove nothing about the
 *  one the image reads.
 */
void    GFX_inject_mouse(int x, int y);
void    GFX_inject_button(unsigned code, int down);
void    GFX_inject_key(unsigned code, int down);

void    GFX_set_display(st_oop form);

/*
 *  Consulted before the screen is grown to fill the window.  It must bring
 *  the image's own idea of the screen size up to date and answer non-zero;
 *  answering 0 -- or registering nothing -- leaves the screen as it is.
 */
void    GFX_set_screen_hook(int (*fn)(int width, int height));

/*
 *  Make the pointer look like this Form -- primitive 101, Cursor>>beCursor.
 *  Ignored when there is no window, which is what keeps it off every worker
 *  thread in a headless run.
 */
void    GFX_set_cursor(st_oop form);

/*
 *  Move the pointer -- primitive 91.  Coordinates are the display Form's, not
 *  the window's.  A headless run keeps the VM's own idea of the position in
 *  step and does nothing else, so scripted input warps exactly as a window
 *  would.
 */
void    GFX_warp_pointer(int x, int y);

/*  Input events the ring had no room for.  Never allowed to be silent.  */
unsigned GFX_events_dropped(void);
st_oop  GFX_display_form(void);
void    GFX_damage(int x, int y, int w, int h);
void    GFX_damage_all(void);

/*  ----------  SDL3 front end  ----------  */

/*
 *  Bring up the window.  Must be called on the thread that entered main():
 *  on macOS the Cocoa run loop is bound to it and cannot be moved.
 */
int     GFX_open(const char *title, int width, int height, char *errbuf,
                 size_t errlen);
void    GFX_close(void);

/*  Pump events and present any damage.  Main thread only.  Returns 0 to quit. */
int     GFX_pump(void);

/*  Is a window open?  Headless builds and tests run with none.  */
int     GFX_is_open(void);

/*  ----------  Input  ----------
 *
 *  The image reads events as a stream of 16-bit words through primitive 95,
 *  in the Smalltalk-80 encoding: a 4-bit type in the high bits and a 12-bit
 *  value below.
 */
#define ST_EVENT_TYPE_SHIFT     12
#define ST_EVENT_VALUE_MASK     0x0FFF

#define ST_EVENT_NONE           0
#define ST_EVENT_XLOCATION      1
#define ST_EVENT_YLOCATION      2
#define ST_EVENT_BISTATE_ON     3
#define ST_EVENT_BISTATE_OFF    4
#define ST_EVENT_ABSTIME        5
#define ST_EVENT_DELTATIME      6

int     GFX_event_pending(void);
int     GFX_next_event_word(uint16_t *word);
void    GFX_mouse_point(int *x, int *y);
int     GFX_button_state(void);

#ifdef __cplusplus
}
#endif

/*
 *  Screen pixels per display pixel, as chosen when the window opened.
 *  Answers 1 before there is a window.
 */
int         GFX_scale(void);

/*  The window's current size in screen pixels; zeroes if none.  */
void        GFX_window_size(int *width, int *height);

/*  How the display is fitted into the window, and why.  */
const char *GFX_presentation(void);

#endif  /*  ST_GFX_H  */

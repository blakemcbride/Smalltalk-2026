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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  ----------  Display form and damage tracking  ----------  */

static st_oop   display_form = ST_NIL;
static int      damage_valid;
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
    GFX_damage_all();
}

st_oop
GFX_display_form(void)
{
    return display_form;
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

static void
queue_event(unsigned type, unsigned value)
{
    unsigned    next = (event_tail + 1) % EVENT_QUEUE_SIZE;

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

int
GFX_event_pending(void)
{
    return event_head != event_tail;
}

int
GFX_next_event_word(uint16_t *word)
{
    if (event_head == event_tail)
        return 0;
    *word = event_queue[event_head];
    event_head = (event_head + 1) % EVENT_QUEUE_SIZE;
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
    if (x == mouse_x && y == mouse_y)
        return;
    mouse_x = x;
    mouse_y = y;
    queue_event(ST_EVENT_XLOCATION, (unsigned) x);
    queue_event(ST_EVENT_YLOCATION, (unsigned) y);
}

void
GFX_inject_button(unsigned code, int down)
{
    if (down)
        button_state |= 1 << (int) (code - 128);
    else
        button_state &= ~(1 << (int) (code - 128));
    queue_event(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF, code);
}

void
GFX_inject_key(unsigned code, int down)
{
    queue_event(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF, code);
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

#else   /*  ST_HAVE_SDL3  */

#include <SDL3/SDL.h>

static SDL_Window      *window;
static SDL_Renderer    *renderer;
static SDL_Texture     *texture;
static int              texture_w;
static int              texture_h;

int
GFX_is_open(void)
{
    return window != NULL;
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
 */
static int
choose_scale(int width, int height)
{
    const char     *forced = getenv("ST_DISPLAY_SCALE");
    SDL_DisplayID   id;
    SDL_Rect        usable;
    int             scale = 1;

    if (forced && forced[0]) {
        int n = atoi(forced);

        return n > 0 ? n : 1;
    }
    id = SDL_GetPrimaryDisplay();
    if (id != 0 && SDL_GetDisplayUsableBounds(id, &usable)) {
        while (scale < 4
            && (scale + 1) * width  <= usable.w * 4 / 5
            && (scale + 1) * height <= usable.h * 4 / 5)
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
    SDL_GetWindowSize(window, &ww, &wh);
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
    return SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
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
    window = SDL_CreateWindow(title, width * scale, height * scale,
                              SDL_WINDOW_RESIZABLE);
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
    GFX_damage_all();
    return 0;
}

void
GFX_close(void)
{
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
    int w = 0, h = 0;

    if (window)
        SDL_GetWindowSize(window, &w, &h);
    if (width)
        *width = w;
    if (height)
        *height = h;
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
        for (x = 0; x < rect.w; ++x) {
            int         bit  = rect.x + x;
            uint16_t    word = row[bit >> 4];

            out[x] = ((word >> (15 - (bit & 15))) & 1) ? pixel_ink
                                                       : pixel_paper;
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
    if (nx == mouse_x && ny == mouse_y)
        return;
    mouse_x = nx;
    mouse_y = ny;
    queue_event(ST_EVENT_XLOCATION, (unsigned) nx);
    queue_event(ST_EVENT_YLOCATION, (unsigned) ny);
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

        case SDL_EVENT_MOUSE_MOTION:
            handle_mouse_motion(event.motion.x, event.motion.y);
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
            queue_event(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF,
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
            else if (key < 128)
                code = (unsigned) key;
            else
                break;
            queue_event(down ? ST_EVENT_BISTATE_ON : ST_EVENT_BISTATE_OFF,
                        code);
            break;
        }

        default:
            break;
        }
    }
    present();
    return 1;
}

#endif  /*  ST_HAVE_SDL3  */

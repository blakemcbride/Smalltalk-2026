/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The desktop, driven.
 *
 *  Every other suite here asks the system about itself: the object memory
 *  checks its own invariants, the compiler is held against Chapter 28, the
 *  library against its own SUnit, the interpreter against Xerox's traces.
 *  All of that was green through a run of faults nobody could see from
 *  inside -- a screen whose buttons did nothing, a browser that would not
 *  open, a menu that vanished on press, a caret a character and a half from
 *  where it claimed to be, a window drawn and deaf.  Each was found by a
 *  person using the thing and none by a test, because nothing here drove
 *  the interface and nothing looked at the screen.
 *
 *  This does both.  It stands the real desktop up -- the same startup the
 *  -run path installs -- posts input through the queue SDL's own handlers
 *  post to, and then asserts on the pixels.
 *
 *  Two rules keep it from becoming a wall of brittle golden files:
 *
 *  1.  Assert PROPERTIES, not images.  "A menu appeared under the cursor",
 *      "a window covers the rectangle that was framed", "the caret is where
 *      the image says the next character goes".  A new face or a new size
 *      moves the pixels; it does not move any of those.
 *
 *  2.  Assert what a FAULT would break, not what a change would move.  Every
 *      check below is one a bug of this system's actual history would fail:
 *      that is the standard for adding another.
 *
 *  What it cannot see, said plainly rather than left to be discovered: it
 *  posts events itself, so it does not test SDL's own delivery, real
 *  compositor timing, cursor SHAPES (primitive 101 needs a window), or
 *  anything that only goes wrong at the speed of a hand.  The volume half
 *  of that -- an event ring overrun by a fast drag -- is checked in
 *  test_image.c, where the stream can be read directly.
 */

#include "st_test.h"
#include "om.h"
#include "interp.h"
#include "bootstrap.h"
#include "profile.h"
#include "gfx.h"
#include "st_sched.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PROFILE         "profiles/st2026.profile"
#define SCREEN_W        640
#define SCREEN_H        480
#define SLICE           20000
#define MENU_ROW        26      /*  one line of the face, plus its lead  */

/*  The startup -run installs.  The desktop is this loop and nothing else.  */
#define DESKTOP_STARTUP \
    "Display beDisplay. ScheduledControllers restore." \
    " [true] whileTrue: [ScheduledControllers searchForActiveController]"

static st_names     sources;
static int         *source_dialects;

/*  ----------  Standing the image up  ----------  */

static int
build_desktop(void)
{
    st_bootstrap_result res;
    st_boot_init_report init;
    char                err[256];

    if (!PROFILE_expand(PROFILE, &sources, &source_dialects, err, sizeof err)) {
        printf("skipped: %s\n", err);
        return 0;
    }
    if (BOOT_build_dialects((const char *const *) sources.items,
                            source_dialects, sources.count, &res) != 0) {
        printf("  bootstrap failed: %s\n", res.error);
        return 0;
    }
    if (!BOOT_install_display(SCREEN_W, SCREEN_H)) {
        printf("  no display\n");
        return 0;
    }
    BOOT_run_initializers(&init);
    if (!BOOT_install_scheduler(DESKTOP_STARTUP)) {
        printf("  no startup process\n");
        return 0;
    }
    ST_interp_register();
    if (ST_interp_init(err, sizeof err) != 0) {
        printf("  interpreter: %s\n", err);
        return 0;
    }
    printf("  %u classes, %u methods; desktop %dx%d\n",
           res.classes_created, res.methods_compiled, SCREEN_W, SCREEN_H);
    return 1;
}

/*  Let the image run.  Everything here is posted and then waited out.  */
static void
settle(unsigned slices)
{
    while (slices-- && st_vm.running)
        ST_interp_run(SLICE);
}

/*  ----------  Reading the screen  ----------  */

typedef struct { int left, top, right, bottom; long ink; } region;

static region
scan(int x0, int y0, int x1, int y1)
{
    region      r = { -1, -1, -1, -1, 0 };
    gfx_form    form;
    int         x, y;

    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return r;
    if (x1 > form.width)  x1 = form.width;
    if (y1 > form.height) y1 = form.height;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            uint16_t    word = form.bits[(size_t) y * form.raster + (x >> 4)];

            if ((word >> (15 - (x & 15))) & 1) {
                ++r.ink;
                if (r.left < 0 || x < r.left)   r.left = x;
                if (x > r.right)                r.right = x;
                if (r.top < 0 || y < r.top)     r.top = y;
                if (y > r.bottom)               r.bottom = y;
            }
        }
    }
    return r;
}

static region
whole_screen(void)
{
    return scan(0, 0, SCREEN_W, SCREEN_H);
}

/*
 *  A window body is a run of WHITE inside a screen whose background is a
 *  halftone, so it is found by looking for a row with a long unbroken run of
 *  no ink.  Menus are white too, which is why callers say where to look.
 */
static int
longest_clear_run(int y, int x0, int x1, int *start)
{
    gfx_form    form;
    int         x, run = 0, best = 0, best_start = -1;

    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return 0;
    for (x = x0; x < x1 && x < form.width; ++x) {
        uint16_t    word = form.bits[(size_t) y * form.raster + (x >> 4)];

        if (((word >> (15 - (x & 15))) & 1) == 0) {
            if (run++ == 0)
                best_start = x;
            if (run > best) { best = run; *start = best_start; }
        }  else  {
            run = 0;
        }
    }
    return best;
}

/*
 *  A copy of the screen, and what changed since.
 *
 *  This is how a menu is FOUND rather than assumed.  PopUpMenu opens centred
 *  on its current item -- the first one until something has been chosen, and
 *  the chosen one ever after -- so an item's position on screen depends on
 *  the history of the session.  Reasoning about that is how a test comes to
 *  select `close' while believing it selected `frame', which is a mistake
 *  this author made by hand before writing any of this down.
 *
 *  So: photograph the screen, press, and the rectangle that changed is the
 *  menu.  Nothing about its position is predicted.
 */
static uint16_t *
photograph(void)
{
    gfx_form            form;
    static uint16_t    *shot;
    static uint32_t     words;

    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return NULL;
    if (!shot || words != form.words) {
        free(shot);
        shot  = (uint16_t *) malloc((size_t) form.words * sizeof *shot);
        words = form.words;
    }
    if (shot)
        memcpy(shot, form.bits, (size_t) form.words * sizeof *shot);
    return shot;
}

/*
 *  What changed, inside a box.  The box matters: opening a menu also makes
 *  the window under it deemphasize its label and the desktop repaint, and a
 *  whole-screen diff picks all of that up -- which put the release outside
 *  the menu and chose nothing.  A menu opens centred on the cursor, so a
 *  neighbourhood of the press is where to look and nowhere else is.
 */
static region
changed_in(const uint16_t *shot, int x0, int y0, int x1, int y1)
{
    region      r = { -1, -1, -1, -1, 0 };
    gfx_form    form;
    int         x, y;

    if (!shot || !GFX_form_from_oop(GFX_display_form(), &form))
        return r;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > form.width)  x1 = form.width;
    if (y1 > form.height) y1 = form.height;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            size_t      i = (size_t) y * form.raster + (x >> 4);
            unsigned    bit = 15 - (unsigned) (x & 15);

            if (((form.bits[i] >> bit) & 1) != ((shot[i] >> bit) & 1)) {
                ++r.ink;
                if (r.left < 0 || x < r.left)   r.left = x;
                if (x > r.right)                r.right = x;
                if (r.top < 0 || y < r.top)     r.top = y;
                if (y > r.bottom)               r.bottom = y;
            }
        }
    }
    return r;
}

/*
 *  The y of the index'th item in a menu, found by looking at it.
 *
 *  Items cannot be counted off at a fixed row height alone, because a
 *  PopUpMenu draws a rule wherever its `lines:' argument says and that rule
 *  takes vertical space.  Guessing the row height chose `project' while
 *  believing it had chosen `browser'.
 *
 *  Nor can a rule be told from an item by width: the CURRENT item is drawn
 *  inverted, so it too is a band of full-width ink -- and which item that is
 *  depends on what was chosen last, since a menu reopens on its previous
 *  choice.  Height is what separates them.  A rule is two or three rows; an
 *  item is a whole line of the face.
 */
static int
wide_row(int y, int left, int right, int width)
{
    region  row = scan(left, y, right, y + 1);

    return row.left >= 0 && (row.right - row.left + 1) * 10 >= width * 8;
}

static int
menu_item_y(region menu, int index)
{
    int inner_l = menu.left + 3;
    int inner_r = menu.right - 2;
    int inner_w = inner_r - inner_l;
    int y       = menu.top + 2;
    int n       = 0;

    if (inner_w < 8)
        return -1;
    while (y < menu.bottom - 2) {
        int yy = y;

        while (yy < menu.bottom && wide_row(yy, inner_l, inner_r, inner_w))
            ++yy;
        if (yy - y > 0 && yy - y <= 3) {        /*  a rule: no item here  */
            y = yy;
            continue;
        }
        if (n++ == index)
            return y + MENU_ROW / 2;
        y += MENU_ROW;
    }
    return -1;
}

/*
 *  Write the screen out, so a failure here can be looked at instead of
 *  guessed at.  Only called when something has already gone wrong.
 */
static void
dump_screen(const char *why)
{
    gfx_form    form;
    char        path[256];
    FILE       *f;
    int         x, y;

    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return;
    snprintf(path, sizeof path, "/tmp/st80-desktop-%s.pbm", why);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n%d %d\n", form.width, form.height);
    for (y = 0; y < form.height; ++y) {
        for (x = 0; x < form.width; ++x) {
            uint16_t    word = form.bits[(size_t) y * form.raster + (x >> 4)];
            fprintf(f, "%d ", (word >> (15 - (x & 15))) & 1);
        }
        fputc('\n', f);
    }
    fclose(f);
    printf("       screen written to %s\n", path);
}

static region
changed_since(const uint16_t *shot)
{
    return changed_in(shot, 0, 0, SCREEN_W, SCREEN_H);
}

/*  ----------  Posting input  ----------  */

#define BLUE    128
#define YELLOW  129
#define RED     130

static void
move_to(int x, int y)
{
    GFX_inject_mouse(x, y);
    settle(2);
}

static void
press(unsigned button, int x, int y)
{
    move_to(x, y);
    GFX_inject_button(button, 1);
    settle(4);
}

static void
release(unsigned button, int x, int y)
{
    move_to(x, y);
    GFX_inject_button(button, 0);
    settle(8);
}

/*
 *  Press, drag onto an item, release -- the one gesture this interface has.
 *
 *  The item is named by its index from the top of the menu, and the menu is
 *  located by what its appearance changed.  Answers 0 if no menu appeared,
 *  which is itself a finding worth failing on.
 */
static int
choose(unsigned button, int x, int y, int item)
{
    const uint16_t *before = photograph();
    region          menu;
    int             item_y;

    press(button, x, y);
    settle(20);
    /*
     *  A menu is at most a few hundred pixels either way and is centred on
     *  the cursor, so this box contains it and excludes everything else
     *  that repainted.
     */
    menu = changed_in(before, x - 220, y - 260, x + 220, y + 260);
    if (getenv("ST_DESKTOP_DUMP"))
        dump_screen("menu");
    if (menu.top < 0 || menu.bottom - menu.top < MENU_ROW) {
        GFX_inject_button(button, 0);
        settle(20);
        return 0;
    }
    item_y = menu_item_y(menu, item);
    if (getenv("ST_DESKTOP_DUMP"))
        printf("       menu %d..%d x %d..%d, item %d at y=%d\n",
               menu.left, menu.right, menu.top, menu.bottom, item, item_y);
    if (item_y < 0) {
        GFX_inject_button(button, 0);
        settle(20);
        return 0;
    }
    release(button, (menu.left + menu.right) / 2, item_y);
    settle(30);
    return 1;
}

/*  Drag out a rectangle with the red button, which is how a window is placed. */
static void
frame(int x0, int y0, int x1, int y1)
{
    press(RED, x0, y0);
    move_to((x0 + x1) / 2, (y0 + y1) / 2);
    release(RED, x1, y1);
    settle(20);
}

/*  ----------  The scenarios  ----------  */

/*
 *  The menu item offsets are measured from the menu's own geometry rather
 *  than guessed: PopUpMenu opens centred on its CURRENT item, which is the
 *  first one until something has been chosen and the chosen one afterwards.
 *  That is a genuine 1983 behaviour and it caught the author out once, so
 *  the desktop menu is always opened fresh here and the window menu is only
 *  ever used for its first item.
 */
/*
 *  The desktop menu, in order.  Item 1 sits under the cursor when it opens.
 */
enum { M_RESTORE = 0, M_EXIT, M_PROJECT, M_FILELIST, M_BROWSER,
       M_WORKSPACE, M_TRANSCRIPT, M_SYSWORKSPACE, M_SAVE, M_QUIT };

static void
test_the_desktop_comes_up(void)
{
    region  r;

    printf("---- the desktop ----\n");

    settle(60);
    r = whole_screen();
    /*
     *  A bare desktop is the halftone and nothing else: ink everywhere, and
     *  about half of it.  Zero would mean nothing was drawn at all, which is
     *  what a screen the image never learned about looks like.
     */
    ++st_test_checks;
    if (r.ink < (long) SCREEN_W * SCREEN_H / 4) {
        ++st_test_failures;
        printf("  FAIL the desktop drew %ld pixels of %d\n",
               r.ink, SCREEN_W * SCREEN_H);
    }
}

static int open_workspace(int x0, int y0, int x1, int y1);

/*
 *  A window that is opened and closed leaves the screen exactly as it was.
 *
 *  Exactly: not "about right".  The desktop background and Form gray are two
 *  different pieces of code painting the same 50% halftone, and they used to
 *  disagree about its PHASE -- the VM put 0xAAAA on row 0 and
 *  Form class>>initializeMasks puts 0x5555 there.  Both are grey, both have
 *  the same ink count, and the rectangle a window had occupied came back in
 *  the opposite phase and sat there as a patch of visibly different texture.
 *  A count-based check cannot see it; a bit-for-bit one cannot miss it.
 */
static void
test_closing_a_window_restores_the_desktop(void)
{
    const uint16_t *bare;
    region          left_behind;

    printf("---- a closed window leaves no trace ----\n");

    settle(40);
    bare = photograph();
    if (!bare)
        return;
    if (!open_workspace(150, 180, 520, 380))
        return;
    settle(60);
    if (!choose(BLUE, 300, 250, 4)) {           /*  under move frame collapse close  */
        ++st_test_checks; ++st_test_failures;
        printf("  FAIL the window menu did not open\n");
        return;
    }
    settle(120);

    left_behind = changed_since(bare);
    ++st_test_checks;
    if (left_behind.ink != 0) {
        ++st_test_failures;
        printf("  FAIL %ld pixels differ from the bare desktop, x %d..%d "
               "y %d..%d\n", left_behind.ink, left_behind.left,
               left_behind.right, left_behind.top, left_behind.bottom);
        dump_screen("erase");
    }
}

static void
test_the_menu_opens_and_holds(void)
{
    region  before, during, after;

    printf("---- the yellow button raises the system menu ----\n");

    settle(20);
    before = whole_screen();

    /*
     *  Press and HOLD.  The menu must be up while the button is down: a
     *  click puts it up and takes it straight back, which is the single
     *  most common way to conclude this interface is broken.
     */
    press(YELLOW, 300, 120);
    settle(20);
    during = whole_screen();
    ++st_test_checks;
    if (during.ink == before.ink) {
        ++st_test_failures;
        printf("  FAIL no menu appeared while the yellow button was held\n");
    }

    /*  And it comes down again when the button does, choosing nothing.  */
    GFX_inject_button(YELLOW, 0);
    settle(40);
    after = whole_screen();
    ++st_test_checks;
    if (after.ink != before.ink) {
        ++st_test_failures;
        printf("  FAIL the menu did not come down: %ld pixels of ink, "
               "was %ld\n", after.ink, before.ink);
    }
}

/*
 *  Open a window from the menu and place it.  This is two gestures, and the
 *  second one is the part everybody misses: choosing `workspace' ARMS a
 *  window, and the red button then drags out where it goes.
 */
static int
open_workspace(int x0, int y0, int x1, int y1)
{
    if (!choose(YELLOW, 300, 120, M_WORKSPACE)) {
        ++st_test_checks; ++st_test_failures;
        printf("  FAIL the system menu did not open\n");
        return 0;
    }
    settle(40);
    frame(x0, y0, x1, y1);
    settle(60);
    return 1;
}

static void
test_a_workspace_opens_where_it_was_framed(void)
{
    int     start = -1, run;

    printf("---- a window is placed by dragging out its rectangle ----\n");

    open_workspace(120, 200, 480, 400);

    /*
     *  A row through the middle of the frame must be white for most of its
     *  width: that is the window's body, and the halftone it replaced was
     *  not.  Checking the RUN rather than a count is what makes this a
     *  statement about position.
     */
    run = longest_clear_run(300, 0, SCREEN_W, &start);
    ++st_test_checks;
    if (run < 300 || start < 115 || start > 130) {
        ++st_test_failures;
        printf("  FAIL framed 120..480 but the widest clear run on that row "
               "is %d wide starting at %d\n", run, start);
    }
}

static void
test_the_view_answers_the_yellow_button(void)
{
    region  before, during;

    printf("---- a window that is drawn is a window that responds ----\n");

    settle(20);
    before = whole_screen();

    /*
     *  The fault this is written for: the window drawn, perfectly painted,
     *  and every click ignored -- which is what a lost button-release
     *  leaves behind, and what a stale controller rectangle leaves behind.
     *  Both look identical from outside and both fail here.
     */
    press(YELLOW, 300, 300);
    settle(30);
    during = whole_screen();
    ++st_test_checks;
    if (during.ink == before.ink) {
        ++st_test_failures;
        printf("  FAIL the yellow button inside the window did nothing\n");
    }
    GFX_inject_button(YELLOW, 0);
    settle(40);
}

static void
test_typing_reaches_the_window(void)
{
    region  before, after;

    printf("---- the keyboard reaches the window under the pointer ----\n");

    press(RED, 300, 300);
    release(RED, 300, 300);
    settle(30);
    before = scan(120, 200, 480, 400);

    GFX_inject_key('A', 1); GFX_inject_key('A', 0); settle(20);
    GFX_inject_key('B', 1); GFX_inject_key('B', 0); settle(20);
    GFX_inject_key('C', 1); GFX_inject_key('C', 0); settle(30);

    after = scan(120, 200, 480, 400);
    ++st_test_checks;
    if (after.ink <= before.ink) {
        ++st_test_failures;
        printf("  FAIL three keystrokes added %ld pixels to the window\n",
               after.ink - before.ink);
    }
}

/*
 *  The cursor keys, which the Alto keyboard did not have.
 *
 *  Two failures to catch, and they are opposite ones.  Before lib/Keyboard-Map
 *  the arrows never reached the image at all: every SDL keycode above 127
 *  fell off the end of the key branch in display.c.  And a code that DOES
 *  reach the image without a place in the keyboard map decodes to 255,
 *  `unassigned', which is not inert -- ParagraphEditor types it, so the
 *  fix applied by halves would put a box in your text.
 *
 *  Both show as ink, and both are checked without needing to know where
 *  anything is on the screen, by ending each gesture with the caret back
 *  where it started.  Three lefts and three rights type nothing and leave
 *  the caret after the C, so the window must be pixel for pixel what it
 *  was.  Then three lefts, a backspace and three rights: if the arrows
 *  moved the caret it is in front of the A when the backspace arrives,
 *  there is nothing there to delete, and the window is again unchanged.  If
 *  they had been ignored the caret would still have been after the C and
 *  the backspace would have eaten it.
 *
 *  This runs on the workspace the earlier tests opened and typed ABC into.
 */
static void
test_the_cursor_keys_move_the_caret(void)
{
    /*  What display.c sends for them.  lib/Keyboard-Map agrees.  */
    enum { LEFT_KEY = 152, RIGHT_KEY = 153, BACKSPACE_KEY = 8 };
    region  typed, after;
    int     i;

    printf("---- the cursor keys move the caret and type nothing ----\n");

    typed = scan(120, 200, 480, 400);

    for (i = 0; i < 3; ++i) {
        GFX_inject_key(LEFT_KEY, 1); GFX_inject_key(LEFT_KEY, 0); settle(20);
    }
    for (i = 0; i < 3; ++i) {
        GFX_inject_key(RIGHT_KEY, 1); GFX_inject_key(RIGHT_KEY, 0); settle(20);
    }
    settle(30);
    after = scan(120, 200, 480, 400);
    ++st_test_checks;
    if (after.ink != typed.ink) {
        ++st_test_failures;
        printf("  FAIL six cursor keys changed the window by %ld pixels\n",
               after.ink - typed.ink);
    }

    for (i = 0; i < 3; ++i) {
        GFX_inject_key(LEFT_KEY, 1); GFX_inject_key(LEFT_KEY, 0); settle(20);
    }
    GFX_inject_key(BACKSPACE_KEY, 1); GFX_inject_key(BACKSPACE_KEY, 0);
    settle(30);
    for (i = 0; i < 3; ++i) {
        GFX_inject_key(RIGHT_KEY, 1); GFX_inject_key(RIGHT_KEY, 0); settle(20);
    }
    settle(30);
    after = scan(120, 200, 480, 400);
    ++st_test_checks;
    if (after.ink != typed.ink) {
        ++st_test_failures;
        printf("  FAIL a backspace in front of the first character took %ld "
               "pixels\n", typed.ink - after.ink);
    }
}

/*
 *  The rest of the keys a keyboard made since the Alto has: home, end, the
 *  delete that goes forward, and control to widen a step into a word.
 *
 *  Checked the way the arrows above are, by pixels rather than by position:
 *  every gesture here ends with the same three characters and the caret back
 *  where it was, so the window must be bit for bit what it was before.  That
 *  catches both halves of the failure at once -- a key that does nothing and
 *  a key that types a box -- without this test having to know where anything
 *  is on the screen.
 *
 *  A caret is the same shape wherever it stands, so counting ink cannot tell
 *  a caret at the front of the text from one at the end.  These compare the
 *  actual bits, through photograph(), which can.  And a backspace is the
 *  probe that says where the caret really went: at the front of the text it
 *  takes nothing, anywhere else it takes a character, and the difference is
 *  loud.
 *
 *  This runs on the workspace the earlier tests opened, typed ABC into, and
 *  left with the caret after the C.
 */
static void
check_window(const uint16_t *shot, int changed, const char *what)
{
    region  r = changed_in(shot, 120, 200, 480, 400);

    ++st_test_checks;
    if ((r.ink != 0) == (changed != 0))
        return;
    ++st_test_failures;
    printf("  FAIL %s %s the window (%ld pixels)\n", what,
           changed ? "did not change" : "changed", r.ink);
    dump_screen(what);
}

static void
tap(unsigned code)
{
    GFX_inject_key(code, 1);
    GFX_inject_key(code, 0);
    settle(20);
}

/*  The same, with the control key held across it, as a hand would.  */
static void
tap_with_control(unsigned code)
{
    enum { CTRL_KEY = 138 };

    GFX_inject_key(CTRL_KEY, 1);
    settle(5);
    GFX_inject_key(code, 1);
    GFX_inject_key(code, 0);
    settle(20);
    GFX_inject_key(CTRL_KEY, 0);
    settle(20);
}

static void
test_the_editing_keys(void)
{
    /*  What display.c sends for them.  lib/Keyboard-Map agrees.  */
    enum { LEFT_KEY = 152, RIGHT_KEY = 153, HOME_KEY = 156, END_KEY = 157,
           DELETE_KEY = 146, BACKSPACE_KEY = 8, A_KEY = 'A' };
    const uint16_t *typed;
    long            ink;

    printf("---- home, end, control-arrow and delete forward ----\n");

    settle(30);
    ink = scan(120, 200, 480, 400).ink;
    if (!(typed = photograph()))
        return;

    /*
     *  Home and back again types nothing and leaves the caret where it was.
     */
    tap(HOME_KEY);
    tap(END_KEY);
    settle(30);
    check_window(typed, 0, "home-and-end");

    /*
     *  And home really went to the front: a backspace there takes nothing.
     *  Had home been ignored the backspace would have eaten the C.
     */
    tap(HOME_KEY);
    tap(BACKSPACE_KEY);
    tap(END_KEY);
    settle(30);
    check_window(typed, 0, "backspace-after-home");

    /*
     *  Delete at the end of the text has nothing in front of it to take.
     */
    tap(DELETE_KEY);
    settle(30);
    check_window(typed, 0, "delete-past-the-end");

    /*
     *  Control-left is a word and not a character, so from after the C it
     *  lands on the A rather than on the B -- and the backspace takes
     *  nothing again.  Plain left would have left it in the middle.
     */
    tap_with_control(LEFT_KEY);
    tap(BACKSPACE_KEY);
    tap_with_control(RIGHT_KEY);
    settle(30);
    check_window(typed, 0, "backspace-after-control-left");

    /*
     *  Control-home and control-end address the whole text, which on one
     *  line is the same place -- what this checks is that they are not
     *  ignored for having a modifier held, which is how the map used to
     *  answer 255 for anything it did not know.
     */
    tap_with_control(HOME_KEY);
    tap(BACKSPACE_KEY);
    tap_with_control(END_KEY);
    settle(30);
    check_window(typed, 0, "backspace-after-control-home");

    /*
     *  Now the one gesture that must change something: delete forward at the
     *  front takes the A, and the window loses that letter's ink.  Ink and
     *  not pixels, because home moved the caret and that is a change all by
     *  itself -- a key that reached the image and did nothing would pass a
     *  bare change test here.  A caret is the same shape wherever it stands,
     *  so it adds the same ink at either end and cancels out of this.
     */
    tap(HOME_KEY);
    tap(DELETE_KEY);
    settle(30);
    {
        long    after = scan(120, 200, 480, 400).ink;

        ++st_test_checks;
        if (after >= ink) {
            ++st_test_failures;
            printf("  FAIL delete forward left the window %ld pixels of ink, "
                   "was %ld\n", after, ink);
            dump_screen("delete-forward");
        }
    }

    /*
     *  And typing it back and returning to the end restores the window
     *  exactly, which is how we know delete took ONE character and put the
     *  caret where the A had been.
     */
    tap(A_KEY);
    tap(END_KEY);
    settle(30);
    check_window(typed, 0, "retyping-what-delete-took");
}

/*
 *  The Browser is the largest thing the interface builds, and the one whose
 *  failure was reported as "I tried to bring a browser up and nothing
 *  happened for a long time" -- which was not slowness but a missing
 *  signal.  Opening it exercises the class list, the four panes and the
 *  whole View tree at once, so it is the broadest single gesture available.
 */
static void
test_a_browser_opens(void)
{
    const uint16_t *before;
    region          drawn;

    printf("---- the browser opens, and it is not empty ----\n");

    before = photograph();
    if (!choose(YELLOW, 320, 60, M_BROWSER)) {
        ++st_test_checks; ++st_test_failures;
        printf("  FAIL the system menu did not open\n");
        return;
    }
    frame(40, 40, 600, 300);
    settle(120);

    drawn = changed_since(before);
    ++st_test_checks;
    if (drawn.left < 0) {
        ++st_test_failures;
        printf("  FAIL choosing browser and framing it changed nothing\n");
        return;
    }
    /*
     *  Not merely changed: FILLED.  A browser is four list panes and a code
     *  pane, so most of the rectangle it was given is now white with text in
     *  it.  An empty frame -- a window that opened and drew nothing -- is a
     *  real failure of this system's history and passes a bare change test.
     */
    {
        int start = -1, run = longest_clear_run(150, 0, SCREEN_W, &start);

        ++st_test_checks;
        if (run < 100) {
            ++st_test_failures;
            printf("  FAIL the browser's widest clear run is only %d\n", run);
        }
    }
    /*  And there is text in the class list: ink inside the top-left pane.  */
    {
        region  pane = scan(45, 60, 200, 140);

        ++st_test_checks;
        if (pane.ink < 100) {
            ++st_test_failures;
            printf("  FAIL the browser's first pane has %ld pixels of text\n",
                   pane.ink);
            dump_screen("browser");
        }
    }
}

/*
 *  The wheel scrolls whatever the pointer is over.
 *
 *  1983 had three buttons and no wheel, so nothing in the image was waiting
 *  to be told about one: the notch is a counter the window fills, and the
 *  controller already running under the pointer reads it at the top of the
 *  activity it was going to do anyway.  Which makes this the test that
 *  matters -- not that a primitive answers a number, but that turning the
 *  wheel over a pane moves that pane.
 *
 *  The browser's category list is the pane with the most in it and the least
 *  room to show it, so it is the one that must move.
 */
static void
test_the_wheel_scrolls_the_view_under_the_pointer(void)
{
    const uint16_t *before;
    region          moved;
    region          returned;

    printf("---- the wheel scrolls the view under the pointer ----\n");

    /*
     *  Into the pane FIRST, and settle before the photograph: taking control
     *  draws the scroll bar, and that is a change this must not mistake for
     *  scrolling.
     */
    move_to(120, 100);
    settle(60);
    before = photograph();
    if (!before)
        return;

    GFX_inject_wheel(-1);               /*  toward the user: further down  */
    settle(60);
    moved = changed_in(before, 45, 60, 200, 140);
    ++st_test_checks;
    if (moved.ink == 0) {
        ++st_test_failures;
        printf("  FAIL a wheel notch changed nothing in the list pane\n");
        dump_screen("wheel");
        return;
    }

    /*
     *  And back.  One notch each way is the same distance in both, so the
     *  pane must return to the pixels it started with -- bit for bit, which
     *  is what tells a scroll that went the right distance from one that
     *  went some distance and came back wrong.
     */
    GFX_inject_wheel(1);
    settle(60);
    returned = changed_in(before, 45, 60, 200, 140);
    ++st_test_checks;
    if (returned.ink != 0) {
        ++st_test_failures;
        printf("  FAIL a notch each way left %ld pixels changed\n",
               returned.ink);
        dump_screen("wheel-back");
    }
}

/*
 *  The window menu, and the item that has to work or nothing can be undone:
 *  close.  The blue button raises it anywhere over a window.
 */
static void
test_the_window_menu_closes_a_window(void)
{
    const uint16_t *before;
    region          gone;

    printf("---- the blue button closes a window ----\n");

    before = photograph();
    /*  under move frame collapse close -- close is the fifth.  */
    if (!choose(BLUE, 300, 150, 4)) {
        ++st_test_checks; ++st_test_failures;
        printf("  FAIL the window menu did not open\n");
        return;
    }
    settle(120);
    gone = changed_since(before);
    ++st_test_checks;
    if (gone.ink < 1000) {
        ++st_test_failures;
        printf("  FAIL close changed only %ld pixels\n", gone.ink);
        dump_screen("close");
    }
}

/*
 *  And the invariant that outlives every gesture above: nothing may be left
 *  believing a button is held.  That is the state a dropped release leaves,
 *  and from it the whole interface is dead while looking perfectly well.
 */
static void
test_no_button_is_left_held(void)
{
    printf("---- no button is left held ----\n");

    ++st_test_checks;
    if (GFX_button_state() != 0) {
        ++st_test_failures;
        printf("  FAIL button state is %d after every gesture completed\n",
               GFX_button_state());
    }
    ++st_test_checks;
    if (GFX_events_dropped() != 0) {
        ++st_test_failures;
        printf("  FAIL %u input events were dropped driving the desktop\n",
               GFX_events_dropped());
    }
}

int
main(void)
{
    ST_TEST_BEGIN("desktop");

    /*
     *  The clipboard with no window, which is this test's condition under
     *  both builds: `nothing there' -- NULL to read, -1 to set -- and the
     *  primitive above them answers nil and false.  The real clipboard
     *  needs a window and a person.
     */
    CHECK(GFX_clipboard_text() == NULL);
    CHECK_EQ_INT(GFX_clipboard_set("anything"), -1);

    if (!build_desktop())
        return ST_TEST_END();

    test_the_desktop_comes_up();
    test_the_menu_opens_and_holds();
    test_a_workspace_opens_where_it_was_framed();
    test_the_view_answers_the_yellow_button();
    test_typing_reaches_the_window();
    test_the_cursor_keys_move_the_caret();
    test_the_editing_keys();
    test_a_browser_opens();
    test_the_wheel_scrolls_the_view_under_the_pointer();
    test_the_window_menu_closes_a_window();
    test_closing_a_window_restores_the_desktop();
    test_no_button_is_left_held();

    return ST_TEST_END();
}

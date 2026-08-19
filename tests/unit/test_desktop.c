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

    if (!build_desktop())
        return ST_TEST_END();

    test_the_desktop_comes_up();
    test_the_menu_opens_and_holds();
    test_a_workspace_opens_where_it_was_framed();
    test_the_view_answers_the_yellow_button();
    test_typing_reaches_the_window();
    test_a_browser_opens();
    test_the_window_menu_closes_a_window();
    test_closing_a_window_restores_the_desktop();
    test_no_button_is_left_held();

    return ST_TEST_END();
}

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  BitBlt: Blue Book Chapter 18.
 *
 *  Essentially all Smalltalk-80 graphics goes through this one operation.
 *  Text, lines, cursors, menus, scrolling and window damage repair are all
 *  copyBits with different arguments, which is why the Blue Book can say the
 *  system "requires implementation of only the one primitive operation to
 *  provide full functionality".  Get this right and the display works; get
 *  it subtly wrong and everything is subtly wrong.
 *
 *  The algorithm is a direct transcription of the book's BitBltSimulation,
 *  which is a Smalltalk *simulation* rather than VM pseudo-code -- it is not
 *  layered on the object memory the way Chapters 28 to 30 are, so the
 *  translation is to plain C over raw words.
 *
 *  Bitmaps are 16-bit words, most significant bit leftmost on screen.  The
 *  three parts that repay careful reading are the partial-word masks at each
 *  end of a scan line, the skew that realigns a source not on a word
 *  boundary, and the direction reversal when source and destination overlap.
 */

#include "gfx.h"
#include <stdlib.h>
#include <stdio.h>
#include "interp.h"

#include <string.h>

#define ALL_ONES        0xFFFFu

/*  RightMasks at: n+1 is n low bits set.  */
static const uint16_t right_masks[17] = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F,
    0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF,
    0xFFFF
};

/*
 *  The sixteen combination rules, verbatim from the book's merge:with:.
 *  Rule 3 (store) and rule 6 (reverse, used for cursors and rubber banding)
 *  carry most of the traffic.
 */
static uint16_t
merge(unsigned rule, uint16_t s, uint16_t d)
{
    switch (rule & 15) {
    case 0:  return 0;
    case 1:  return (uint16_t) (s & d);
    case 2:  return (uint16_t) (s & ~d);
    case 3:  return s;
    case 4:  return (uint16_t) (~s & d);
    case 5:  return d;
    case 6:  return (uint16_t) (s ^ d);
    case 7:  return (uint16_t) (s | d);
    case 8:  return (uint16_t) (~s & ~d);
    case 9:  return (uint16_t) (~(s ^ d));
    case 10: return (uint16_t) ~d;
    case 11: return (uint16_t) (s | ~d);
    case 12: return (uint16_t) ~s;
    case 13: return (uint16_t) (~s | d);
    case 14: return (uint16_t) (~(s & d));
    default: return ALL_ONES;
    }
}

/*  Rotate a 16-bit word left, which is what the book's double shift does.  */
static uint16_t
rotate16(uint16_t v, int by)
{
    by &= 15;
    if (by == 0)
        return v;
    return (uint16_t) ((v << by) | (v >> (16 - by)));
}

int
GFX_form_from_oop(st_oop form, gfx_form *out)
{
    st_oop  bits;
    st_oop  width;
    st_oop  height;

    if (!OM_is_object(form))
        return 0;
    bits   = OM_fetch_pointer(ST_FORM_BITS, form);
    width  = OM_fetch_pointer(ST_FORM_WIDTH, form);
    height = OM_fetch_pointer(ST_FORM_HEIGHT, form);
    if (!OM_is_object(bits) || !OM_is_int(width) || !OM_is_int(height))
        return 0;
    out->oop    = form;
    out->bits   = OM_word_base(bits);
    out->words  = OM_fetch_word_length(bits);
    out->width  = (int) OM_int_value(width);
    out->height = (int) OM_int_value(height);
    /*  Scan lines are word aligned, so a 17-pixel form is two words wide.  */
    out->raster = (out->width + 15) / 16;
    if (out->width <= 0 || out->height <= 0)
        return 0;
    if ((uint32_t) (out->raster * out->height) > out->words)
        return 0;               /*  the bitmap is too small for its extent  */
    return 1;
}

/*
 *  Whether every word the copy loop will touch lies inside the bitmap.
 *
 *  The loop walks `rows` rows; each visits `n_words` words stepping by h_dir
 *  from `start`, and then start advances by `delta`.  Rather than reproduce
 *  that arithmetic and risk disagreeing with it, this walks the same shape
 *  and checks the two extremes of every row.
 */
/*  Looked up once: a getenv per blit would be felt when drawing.  */
static int
blit_logging(void)
{
    static int  state = -1;

    if (state < 0)
        state = getenv("ST_BLIT_LOG") != NULL;
    return state;
}

static int
indices_in_range(int start, int delta, int n_words, int rows,
                 uint32_t words, int h_dir)
{
    int     row;
    int     index = start;

    if (n_words <= 0 || rows <= 0)
        return 1;
    for (row = 0; row < rows; ++row) {
        int last = index + (n_words - 1) * h_dir;

        if (index < 0 || (uint32_t) index >= words)
            return 0;
        if (last < 0 || (uint32_t) last >= words)
            return 0;
        index = last + h_dir + delta;
    }
    return 1;
}

/*
 *  Clip the requested rectangle against the clipping rectangle and then
 *  against the source form's own bounds, adjusting both origins so the two
 *  stay in step.
 */
static void
clip_range(gfx_blit *b)
{
    int clip_right  = b->clip_x + b->clip_w;
    int clip_bottom = b->clip_y + b->clip_h;

    if (b->dest_x >= b->clip_x) {
        b->sx = b->source_x;
        b->dx = b->dest_x;
        b->w  = b->width;
    }  else  {
        b->sx = b->source_x + (b->clip_x - b->dest_x);
        b->w  = b->width - (b->clip_x - b->dest_x);
        b->dx = b->clip_x;
    }
    if (b->dx + b->w > clip_right)
        b->w -= (b->dx + b->w) - clip_right;

    if (b->dest_y >= b->clip_y) {
        b->sy = b->source_y;
        b->dy = b->dest_y;
        b->h  = b->height;
    }  else  {
        b->sy = b->source_y + (b->clip_y - b->dest_y);
        b->h  = b->height - (b->clip_y - b->dest_y);
        b->dy = b->clip_y;
    }
    if (b->dy + b->h > clip_bottom)
        b->h -= (b->dy + b->h) - clip_bottom;

    if (!b->has_source)
        return;
    if (b->sx < 0) {
        b->dx -= b->sx;
        b->w  += b->sx;
        b->sx  = 0;
    }
    if (b->sx + b->w > b->source.width)
        b->w -= b->sx + b->w - b->source.width;
    if (b->sy < 0) {
        b->dy -= b->sy;
        b->h  += b->sy;
        b->sy  = 0;
    }
    if (b->sy + b->h > b->source.height)
        b->h -= b->sy + b->h - b->source.height;
}

void
GFX_copy_bits(gfx_blit *b)
{
    int         skew;
    int         start_bits;
    int         end_bits;
    uint16_t    mask1;
    uint16_t    mask2;
    uint16_t    skew_mask;
    int         n_words;
    int         h_dir = 1;
    int         v_dir = 1;
    int         preload;
    int         source_index = 0;
    int         dest_index;
    int         source_delta = 0;
    int         dest_delta;
    int         i;

    clip_range(b);
    if (b->w <= 0 || b->h <= 0)
        return;

    /*  Record what will be touched before the loop starts moving dy.  */
    b->damage_x = b->dx;
    b->damage_y = b->dy;
    b->damage_w = b->w;
    b->damage_h = b->h;

    /*  ----- computeMasks -----  */
    skew       = (b->sx - b->dx) & 15;
    start_bits = 16 - (b->dx & 15);
    mask1      = right_masks[start_bits];
    end_bits   = 15 - ((b->dx + b->w - 1) & 15);
    mask2      = (uint16_t) ~right_masks[end_bits];
    skew_mask  = (skew == 0) ? 0 : right_masks[16 - skew];

    /*
     *  Chapter 18 tests "bbW <= startBits", not "<".  A run that exactly
     *  fills the remainder of one word lives inside that word and needs one,
     *  not two.  With "<" a 16-pixel-wide blit at x = 0 asked for two words
     *  of a one-word raster, which reads and writes a word past the end of
     *  every row.  It survived the Xerox image because a form there is
     *  almost always wider than the blit, so the extra word landed on the
     *  next row's storage and was masked to nothing; it only becomes fatal
     *  when the blit is as wide as the form, which is exactly what filling a
     *  form does.
     */
    if (b->w <= start_bits) {
        /*  The whole run lives inside one destination word.  */
        mask1   = (uint16_t) (mask1 & mask2);
        mask2   = 0;
        n_words = 1;
    }  else  {
        n_words = (b->w - start_bits - 1) / 16 + 2;
    }

    /*
     *  ----- checkOverlap -----
     *
     *  Copying a form onto itself downward or rightward would overwrite
     *  source words before reading them, so the loop runs backwards instead.
     */
    if (b->has_source && b->source.oop == b->dest.oop && b->dy >= b->sy) {
        if (b->dy > b->sy) {
            v_dir  = -1;
            b->sy += b->h - 1;
            b->dy += b->h - 1;
        }  else if (b->dx > b->sx) {
            uint16_t    t;

            h_dir     = -1;
            b->sx    += b->w - 1;
            b->dx    += b->w - 1;
            skew_mask = (uint16_t) ~skew_mask;
            t         = mask1;
            mask1     = mask2;
            mask2     = t;
        }
    }

    /*  ----- calculateOffsets -----  */
    preload = b->has_source && skew != 0 && skew <= (b->sx & 15);
    if (h_dir < 0)
        preload = !preload;

    if (b->has_source) {
        source_index = b->sy * b->source.raster + (b->sx / 16);
        source_delta = (b->source.raster * v_dir)
                     - ((n_words + (preload ? 1 : 0)) * h_dir);
    }
    dest_index = b->dy * b->dest.raster + (b->dx / 16);
    dest_delta = (b->dest.raster * v_dir) - (n_words * h_dir);

    /*
     *  Refuse a blit whose indices leave the bitmaps.
     *
     *  Chapter 18's copyLoop indexes without checking, which is fine for the
     *  simulation because Smalltalk arrays are bounds-checked underneath it.
     *  Here the words are raw memory, so a Form whose geometry does not
     *  survive clipping walks off the end of the heap and corrupts something
     *  unrelated.  Failing is also the specified behaviour: the primitive
     *  falls back to BitBlt's own Smalltalk simulation, which will raise a
     *  proper error rather than take the machine with it.
     */
    if (blit_logging())
        fprintf(stderr, "  blit: rule=%u dest %dx%d raster=%d words=%u"
                        " dx=%d dy=%d w=%d h=%d n=%d di=%d dd=%d hdir=%d"
                        " halftone=%d source=%d\n",
                b->rule, b->dest.width, b->dest.height, b->dest.raster,
                b->dest.words, b->dx, b->dy, b->w, b->h, n_words,
                dest_index, dest_delta, h_dir, b->has_halftone,
                b->has_source);
    if (!indices_in_range(dest_index, dest_delta, n_words, b->h,
                          b->dest.words, h_dir)) {
        if (blit_logging())
            fprintf(stderr, "  blit: refused, dest out of range\n");
        return;
    }
    if (b->has_source
     && !indices_in_range(source_index, source_delta,
                          n_words + (preload ? 1 : 0), b->h,
                          b->source.words, h_dir))
        return;

    /*  ----- copyLoop -----  */
    for (i = 0; i < b->h; ++i) {
        uint16_t    halftone_word;
        uint16_t    skew_word;
        uint16_t    prev_word;
        uint16_t    merge_mask;
        int         word;

        if (b->has_halftone) {
            /*  The halftone is a 16 word pattern indexed by the screen row. */
            halftone_word = b->halftone.bits[b->dy & 15];
            b->dy += v_dir;
        }  else  {
            halftone_word = ALL_ONES;
        }
        skew_word = halftone_word;

        if (preload && b->has_source) {
            prev_word = b->source.bits[source_index];
            source_index += h_dir;
        }  else  {
            prev_word = 0;
        }

        merge_mask = mask1;
        for (word = 0; word < n_words; ++word) {
            uint16_t    merged;
            uint16_t    dest_word;

            if (b->has_source) {
                uint16_t    this_word;

                prev_word = (uint16_t) (prev_word & skew_mask);
                this_word = b->source.bits[source_index];
                skew_word = (uint16_t) (prev_word
                                | (this_word & (uint16_t) ~skew_mask));
                prev_word = this_word;
                skew_word = rotate16(skew_word, skew);
            }
            dest_word = b->dest.bits[dest_index];
            merged    = merge(b->rule,
                              (uint16_t) (skew_word & halftone_word),
                              dest_word);
            b->dest.bits[dest_index] =
                (uint16_t) ((merge_mask & merged)
                          | (dest_word & (uint16_t) ~merge_mask));

            if (b->has_source)
                source_index += h_dir;
            dest_index += h_dir;
            merge_mask = (word == n_words - 2) ? mask2 : ALL_ONES;
        }
        if (b->has_source)
            source_index += source_delta;
        dest_index += dest_delta;
    }
}

/*
 *  Set up a blit from a BitBlt instance in the image.  Returns 0 if any
 *  instance variable has the wrong type, which is exactly when the primitive
 *  must fail and let the Smalltalk simulation take over.
 */
int
GFX_blit_from_oop(st_oop bitblt, gfx_blit *b)
{
    st_oop      dest;
    st_oop      source;
    st_oop      halftone;
    st_oop      rule;
    static const uint32_t   integer_fields[10] = {
        ST_BITBLT_DEST_X, ST_BITBLT_DEST_Y, ST_BITBLT_WIDTH, ST_BITBLT_HEIGHT,
        ST_BITBLT_SOURCE_X, ST_BITBLT_SOURCE_Y,
        ST_BITBLT_CLIP_X, ST_BITBLT_CLIP_Y,
        ST_BITBLT_CLIP_WIDTH, ST_BITBLT_CLIP_HEIGHT
    };
    int        *targets[10];
    int         i;

    memset(b, 0, sizeof *b);
    if (!OM_is_object(bitblt))
        return 0;

    dest     = OM_fetch_pointer(ST_BITBLT_DEST_FORM, bitblt);
    source   = OM_fetch_pointer(ST_BITBLT_SOURCE_FORM, bitblt);
    halftone = OM_fetch_pointer(ST_BITBLT_HALFTONE_FORM, bitblt);
    rule     = OM_fetch_pointer(ST_BITBLT_RULE, bitblt);

    if (!OM_is_int(rule))
        return 0;
    if (OM_int_value(rule) < 0 || OM_int_value(rule) > 15)
        return 0;
    b->rule = (unsigned) OM_int_value(rule);

    if (!GFX_form_from_oop(dest, &b->dest))
        return 0;
    /*  A nil source means "store the halftone", which is how areas fill.  */
    if (source != ST_NIL) {
        if (!GFX_form_from_oop(source, &b->source))
            return 0;
        b->has_source = 1;
    }
    if (halftone != ST_NIL) {
        if (!GFX_form_from_oop(halftone, &b->halftone))
            return 0;
        if (b->halftone.words < 16)
            return 0;
        b->has_halftone = 1;
    }

    targets[0] = &b->dest_x;   targets[1] = &b->dest_y;
    targets[2] = &b->width;    targets[3] = &b->height;
    targets[4] = &b->source_x; targets[5] = &b->source_y;
    targets[6] = &b->clip_x;   targets[7] = &b->clip_y;
    targets[8] = &b->clip_w;   targets[9] = &b->clip_h;
    for (i = 0; i < 10; ++i) {
        st_oop  value = OM_fetch_pointer(integer_fields[i], bitblt);

        if (!OM_is_int(value))
            return 0;
        *targets[i] = (int) OM_int_value(value);
    }
    return 1;
}

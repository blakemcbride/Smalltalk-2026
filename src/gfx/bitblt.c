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
#include <limits.h>

#define ALL_ONES        0xFFFFu

/*  The largest coordinate a blit may name; see GFX_blit_from_oop below.  */
#define GFX_COORD_LIMIT 1073741824      /*  2^30  */

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

/*
 *  How many 16-bit words a bitmap object really holds.
 *
 *  A Form's bits are addressed here as an array of 16-bit words, and every
 *  bound in the blit loop is a word index, so this number is the only thing
 *  standing between a wrong extent and the rest of the heap.  It is not
 *  OM_fetch_word_length: in the 64-bit memory that accessor answers the
 *  object's `size' field, which counts WORDS for a words-format object and
 *  BYTES for a byte-format one -- the two are the same accessor because a
 *  byte object and a word object are told apart by the format flags, not by
 *  the field.  So a String of sixteen bytes reported sixteen words, twice
 *  what it has, and `Form new extent: 16@16 offset: 0@0 bits: (String new:
 *  16)' then passed the extent check below with room for half the blit.
 *  `f black' wrote sixteen bytes past the allocation and the next malloc or
 *  collection died with `free(): invalid size', taking every worker with it
 *  (Bugs4 GRAPHICS-1).
 *
 *  A String is not a hostile argument here.  `Form class>>stringScanLineOfWidth:'
 *  is 1983's own, it builds `String new: width+15//16*2', and Form>>storeOn:
 *  and Form class>>extent:fromCompactArray:offset: both blit through such a
 *  form -- a String is how a scan line is run-length encoded for storage.
 *  So the answer is to count the bytes correctly, not to refuse them: an odd
 *  final byte is simply not addressable as a word and is dropped.
 *
 *  The Blue Book memory needs none of this.  There `size' is the chunk
 *  length in 16-bit words for every format, which is exactly what a bitmap
 *  wants, and a byte object's odd bit is the only correction -- one that
 *  rounds the same way this does.
 */
static uint32_t
bitmap_word_length(st_oop bits)
{
#if defined(ST_OM_MT)
    if (OM_head(bits)->flags & ST_FMT_BYTES)
        return OM_fetch_byte_length(bits) / 2;
#endif
    return OM_fetch_word_length(bits);
}

int
GFX_form_from_oop(st_oop form, gfx_form *out)
{
    st_oop  bits;
    st_oop  width;
    st_oop  height;

    if (!OM_is_object(form))
        return 0;
    /*
     *  A Form is four named fields -- bits, width, height, offset -- and
     *  three of them are read below.  Nothing between the image and here
     *  guarantees that the object in destForm actually is one: `BitBlt
     *  destForm: 3@4 ...' puts a two-field Point there, and fetching field 2
     *  of it read eight bytes past the end of the object (Bugs4 GRAPHICS-2).
     *  On the shipping binary the word past a Point usually failed to
     *  validate as an oop and the primitive failed safe, which is why this
     *  went unnoticed; under ASAN it is a plain heap-buffer-overflow, and
     *  the same fetch on nil -- a pointer object with no fields at all --
     *  reads past the smallest object in the image.
     *
     *  Refusing anything that is not shaped like a Form is the whole fix,
     *  and it costs nothing real: the primitive fails and the Smalltalk
     *  simulation in BitBlt takes over, which is what a wrong argument is
     *  supposed to get.
     */
    if (!OM_pointer_bit(form) || OM_fetch_word_length(form) < ST_FORM_OFFSET + 1)
        return 0;
    bits   = OM_fetch_pointer(ST_FORM_BITS, form);
    width  = OM_fetch_pointer(ST_FORM_WIDTH, form);
    height = OM_fetch_pointer(ST_FORM_HEIGHT, form);
    if (!OM_is_object(bits) || !OM_is_int(width) || !OM_is_int(height))
        return 0;
    /*
     *  And the bits have to be raw storage.  A pointer object here would be
     *  read -- and, as a destination, WRITTEN -- as 16-bit words over the
     *  top of its object pointers, which stays inside the allocation and so
     *  is invisible until the collector walks the half-overwritten oops.
     *  There is no such Form in the image; there is nothing stopping anyone
     *  writing `bits: (Array new: 64)' either.
     */
    if (OM_pointer_bit(bits))
        return 0;
    /*
     *  The extent is a pair of SmallIntegers, and a SmallInteger here is
     *  sixty-two bits wide -- so `width' is not an int until it has been
     *  looked at.  Truncating first was wrong twice over: `Form new extent:
     *  4294967301@4' became a form four pixels wide, and `extent:
     *  2147483647@100' overflowed `width + 15' into a NEGATIVE raster that
     *  the size check below then compared as unsigned and let through, and
     *  `extent: 1048576@65536' overflowed the check's own multiply.  Both
     *  are signed overflow, which is undefined behaviour and which the
     *  UBSan build reports; neither wrote out of bounds, because word_at and
     *  word_put hold the line, but a form is nonsense long before that and
     *  should be refused where it is described.
     *
     *  The product is taken in 64 bits so it cannot wrap, and a raster too
     *  wide to be an int is refused rather than truncated.
     */
    {
        st_int      w = OM_int_value(width);
        st_int      h = OM_int_value(height);
        int64_t     raster;

        if (w <= 0 || h <= 0 || w > INT_MAX || h > INT_MAX)
            return 0;
        /*  Scan lines are word aligned, so a 17-pixel form is two words
         *  wide.  */
        raster = ((int64_t) w + 15) / 16;
        if (raster > INT_MAX)
            return 0;
        if (raster * (int64_t) h > (int64_t) bitmap_word_length(bits))
            return 0;           /*  the bitmap is too small for its extent  */
        out->width  = (int) w;
        out->height = (int) h;
        out->raster = (int) raster;
    }
    out->oop    = form;
    out->bits   = OM_word_base(bits);
    out->words  = bitmap_word_length(bits);
    return 1;
}

/*
 *  Reading and writing a bitmap word, bounds checked.
 *
 *  Chapter 18's copyLoop indexes without checking, which is safe in the
 *  simulation because Smalltalk arrays are checked underneath it.  Here the
 *  words are raw memory, and the loop genuinely does run off the end: a span
 *  of eight pixels starting four bits before a word boundary needs two
 *  destination words, so it reads two source words even when the source is
 *  only one word wide.  In the Blue Book those reads land in the next row of
 *  the same bitmap and are masked away; on the last row there is no next row.
 *
 *  Refusing the whole blit is wrong -- it is a legitimate and extremely
 *  common shape, which is every character drawn at an odd position -- so
 *  each access is guarded instead.  A read outside is zero, a write outside
 *  is dropped, and everything inside is exactly what Chapter 18 says.
 */
static uint16_t
word_at(const uint16_t *bits, int index, uint32_t words)
{
    if (index < 0 || (uint32_t) index >= words)
        return 0;
    return bits[index];
}

static void
word_put(uint16_t *bits, int index, uint32_t words, uint16_t value)
{
    if (index < 0 || (uint32_t) index >= words)
        return;
    bits[index] = value;
}

/*  Looked up once: a getenv per blit would be felt when drawing.  */
static int
blit_logging(void)
{
    static int  state = -1;

    if (state < 0)
        state = getenv("ST_BLIT_LOG") != NULL;
    return state;
}


/*
 *  Clip the requested rectangle against the clipping rectangle and then
 *  against the source form's own bounds, adjusting both origins so the two
 *  stay in step.
 */
static void
clip_range(gfx_blit *b)
{
    int clip_right;
    int clip_bottom;

    /*
     *  First the clipping rectangle itself, against the destination form.
     *
     *  Chapter 18 does not do this, because in the Xerox system a clipRect
     *  always came from a view and was always inside its form; the book's
     *  clipRange only intersects the blit with the clipRect and trusts the
     *  rest.  Here the clipRect is an instance variable anyone can set, and
     *  a clipRect wider than the form does not run off the end of the
     *  bitmap -- word_put below sees to that -- it WRAPS: a dest x past the
     *  form's width still computes `dy * raster + dx / 16', which is a word
     *  belonging to some later scan line of the same bitmap.  So the writes
     *  land inside the form, on the wrong rows, and the picture is quietly
     *  garbled with nothing out of bounds to catch (Bugs4, section 11).
     *
     *  Squeak's BitBlt clips the clipRect the same way and for the same
     *  reason.  Nothing the image itself does is affected: a clipRect
     *  already inside the form is unchanged by all four of these.
     */
    if (b->clip_x < 0) {
        b->clip_w += b->clip_x;
        b->clip_x  = 0;
    }
    if (b->clip_y < 0) {
        b->clip_h += b->clip_y;
        b->clip_y  = 0;
    }
    if (b->clip_x + b->clip_w > b->dest.width)
        b->clip_w = b->dest.width - b->clip_x;
    if (b->clip_y + b->clip_h > b->dest.height)
        b->clip_h = b->dest.height - b->clip_y;
    if (b->clip_w < 0)
        b->clip_w = 0;
    if (b->clip_h < 0)
        b->clip_h = 0;

    clip_right  = b->clip_x + b->clip_w;
    clip_bottom = b->clip_y + b->clip_h;

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

    if (blit_logging())
        fprintf(stderr, "  blit: rule=%u dest %dx%d raster=%d words=%u"
                        " dx=%d dy=%d w=%d h=%d n=%d di=%d dd=%d hdir=%d\n",
                b->rule, b->dest.width, b->dest.height, b->dest.raster,
                b->dest.words, b->dx, b->dy, b->w, b->h, n_words,
                dest_index, dest_delta, h_dir);

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
            prev_word = word_at(b->source.bits, source_index,
                                b->source.words);
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
                this_word = word_at(b->source.bits, source_index,
                                    b->source.words);
                skew_word = (uint16_t) (prev_word
                                | (this_word & (uint16_t) ~skew_mask));
                prev_word = this_word;
                skew_word = rotate16(skew_word, skew);
            }
            dest_word = word_at(b->dest.bits, dest_index,
                                b->dest.words);
            merged    = merge(b->rule,
                              (uint16_t) (skew_word & halftone_word),
                              dest_word);
            word_put(b->dest.bits, dest_index, b->dest.words,
                     (uint16_t) ((merge_mask & merged)
                               | (dest_word & (uint16_t) ~merge_mask)));

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
        st_int  v;

        if (!OM_is_int(value))
            return 0;
        v = OM_int_value(value);
        /*
         *  A SmallInteger is sixty-two bits and these are ints, so the cast
         *  alone is not a check: `destOrigin: 4294967296@0' truncated to
         *  zero and blitted somewhere the caller did not ask for, and
         *  `destOrigin: 2147483647@0 extent: 2147483647@1' overflowed
         *  `dx + w' in clip_range -- signed overflow, which the UBSan build
         *  reports, and after which the clipping arithmetic is meaningless.
         *
         *  A billion is the bound because clip_range adds and subtracts
         *  these in pairs and nothing else, so two of them must still be an
         *  int; and because a blit whose coordinates are past a billion
         *  pixels is not a picture of anything.  Refusing it fails the
         *  primitive, and copyBits' own fallback then raises only if the
         *  rectangle it was asked for meets the clipping rectangle -- which,
         *  at these coordinates, it cannot.
         */
        if (v < -GFX_COORD_LIMIT || v > GFX_COORD_LIMIT)
            return 0;
        *targets[i] = (int) v;
    }
    return 1;
}

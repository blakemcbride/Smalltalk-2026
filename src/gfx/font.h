/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The built-in bitmap font.  See font8x12.c, which carries the design.
 */

#ifndef ST_FONT_H
#define ST_FONT_H

#include <stdint.h>

#define ST_FONT_FIRST   32
#define ST_FONT_LAST    126
#define ST_FONT_WIDTH   8
#define ST_FONT_HEIGHT  12

/*
 *  Where the baseline sits in the cell, and how far below it the tails go.
 *
 *  These are the face's, not the bootstrap's, because the image derives its
 *  whole text layout from them: TextStyle>>gridForFont:withLead: sets the
 *  line grid to `font height + lead' and the baseline to `font ascent', so
 *  getting them wrong squashes every line in the system.  ascent + descent
 *  must equal ST_FONT_HEIGHT.
 */
#define ST_FONT_ASCENT  9
#define ST_FONT_DESCENT 3

/*  Row 0 is the top; bit 7 of each byte is the leftmost pixel.  */
extern const uint8_t    ST_FONT_GLYPHS[ST_FONT_LAST - ST_FONT_FIRST + 1]
                                      [ST_FONT_HEIGHT];

#endif  /*  ST_FONT_H  */

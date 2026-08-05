/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The built-in bitmap font.  See font8x8.c.
 */

#ifndef ST_FONT_H
#define ST_FONT_H

#include <stdint.h>

#define ST_FONT_FIRST   32
#define ST_FONT_LAST    126
#define ST_FONT_WIDTH   8
#define ST_FONT_HEIGHT  8

/*  Row 0 is the top; bit 7 of each byte is the leftmost pixel.  */
extern const uint8_t    ST_FONT_GLYPHS[ST_FONT_LAST - ST_FONT_FIRST + 1]
                                      [ST_FONT_HEIGHT];

#endif  /*  ST_FONT_H  */

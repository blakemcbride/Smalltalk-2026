/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The built-in face.
 *
 *  The 1983 sources are code and carry no font data -- fonts lived in the
 *  image, and the image is the one thing here that carries no licence -- so
 *  a system bootstrapped from those sources has nothing to draw text with
 *  until it is given a face.  This is that face, rasterised from an outline
 *  font by tools/make_font.py, whose header says which one and records the
 *  licence it is derived under.
 *
 *  It is proportional.  Every character used to be eight pixels wide, which
 *  is most of why the interface read as a terminal from 1980; the advances
 *  here are the face's own, and the image reads them out of ST_FONT_XTABLE
 *  through StrikeFont>>widthOf: exactly as it would any other strike font.
 *
 *  The face is an input to state the image COMPUTES and keeps -- TextList
 *  fixes its line grid from `font height' at class-initialisation time, and
 *  PopUpMenu composes its labels into a Form once -- so an image built with
 *  one face cannot be shown another.  Changing anything here means
 *  rebuilding the image; src/main.c says so at startup when they disagree.
 */

#ifndef ST_FONT_H
#define ST_FONT_H

#include <stdint.h>
#include "font_face.h"

/*  Where each code begins in the strike; entry ST_FONT_CODES is the end.  */
extern const uint16_t   ST_FONT_XTABLE[ST_FONT_CODES + 1];

/*  Row 0 is the top; bit 7 of each byte is its leftmost pixel.  */
extern const uint8_t    ST_FONT_STRIKE[ST_FONT_HEIGHT][ST_FONT_STRIKE_BYTES];

/*  The same glyphs as ink coverage, 0 to 255, for the screen.  */
extern const uint8_t    ST_FONT_COVERAGE[ST_FONT_HEIGHT][ST_FONT_STRIKE_WIDTH];

#endif  /*  ST_FONT_H  */

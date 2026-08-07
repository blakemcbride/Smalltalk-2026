/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Pharo's Tonel package format, read as source events.  See source.h for
 *  the events and tonel.c for the format itself.
 */

#ifndef ST_TONEL_H
#define ST_TONEL_H

#include "source.h"

#ifdef __cplusplus
extern "C" {
#endif

int TONEL_read(const char *path, const st_source_sink *sink, void *user,
               char *error, size_t error_len);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_TONEL_H  */

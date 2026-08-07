/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Pharo's Tonel format.  Implemented in Phase C4.
 */

#include "tonel.h"

#include <stdio.h>

int
TONEL_read(const char *path, const st_source_sink *sink, void *user,
           char *error, size_t error_len)
{
    (void) sink;
    (void) user;
    snprintf(error, error_len, "%s: the Tonel reader is not built yet", path);
    return 0;
}

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  A profile: which packages and files make an image.
 *
 *  MANIFEST was a list of paths, which is all one library needs.  Once an
 *  image is composed from packages -- the 1983 library, our own additions,
 *  and imported Pharo ones -- the question becomes which of them, in what
 *  order, minus what.  A profile answers that, in the same STON the Tonel
 *  headers are written in:
 *
 *      Profile {
 *          #name      : 'st2026',
 *          #requires  : [ 'bluebook' ],
 *          #manifests : [ 'sources/MANIFEST' ],
 *          #packages  : [ 'lib/Kernel-Exceptions' ],
 *          #files     : [ 'kernel/Bootstrap.st' ],
 *          #exclude   : [ 'FormMenuView' ]
 *      }
 *
 *  #requires names other profiles, loaded first and only once.  #packages
 *  names directories, every source file in them taken in sorted order so a
 *  build is reproducible.  #manifests keeps a plain path-per-line list
 *  expressible, which is how the 1983 library stays exactly what it was.
 *  #exclude drops a class by name wherever it came from.
 */

#ifndef ST_PROFILE_H
#define ST_PROFILE_H

#include "source.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Expand a profile into the list of source files to load, in order.
 *  Answers 0 and fills `error` on failure.  The caller frees `out`.
 */
int PROFILE_expand(const char *path, st_names *out, char *error,
                   size_t error_len);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_PROFILE_H  */

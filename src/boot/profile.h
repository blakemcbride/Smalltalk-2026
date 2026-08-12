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
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Expand a profile into the list of source files to load, in order, and
 *  the dialect each is written in.
 *
 *  A profile's #dialect applies to everything IT names; a required profile
 *  keeps its own.  That is what lets one image hold the 1983 library as
 *  Blue Book and lib/ as closures, which is the whole point of having the
 *  key: on:do: and ensure: take blocks that have to be real closures.
 *
 *  `dialects` is filled with one ST_DIALECT_* per file and must be freed by
 *  the caller; pass NULL if they are not wanted.  Answers 0 and fills
 *  `error` on failure.
 */
/*
 *  Every source file under a directory, recursively and in sorted order.
 *  What `st80 -bootstrap sources/' needs, and what Phase 5's exit
 *  criterion is written in terms of.
 */
int PROFILE_expand_tree(const char *dir, st_names *out, char *error,
                        size_t error_len);

int PROFILE_expand(const char *path, st_names *out, int **dialects,
                   char *error, size_t error_len);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_PROFILE_H  */

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The chunk format, also called fileIn or bang format.
 *
 *  This is how Smalltalk-80 source has been interchanged since 1983, and it
 *  is the format of the 1983 sources file.  The rules are few:
 *
 *      1.  A file is a sequence of chunks, each terminated by a single "!".
 *      2.  A "!" inside a chunk is doubled.  On reading, "!!" becomes "!".
 *      3.  A chunk of nothing but whitespace is empty, and an empty chunk
 *          ends whatever reader is consuming chunks.
 *      4.  A chunk preceded by "!" is a reader chunk: it evaluates to an
 *          object that then consumes the following chunks until an empty
 *          one.  That is how a method category is filed in.
 *
 *  Two details of the 1983 files that catch people out: lines end with CR,
 *  not LF, and assignment is written with an underscore rather than ":=".
 *  Both are handled here rather than left to the parser.
 */

#ifndef ST_CHUNK_H
#define ST_CHUNK_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char       *text;       /*  NUL-terminated, un-doubled  */
    size_t      length;
    int         is_reader;  /*  the chunk was introduced by "!"  */
    int         is_empty;   /*  whitespace only: ends a reader   */
} st_chunk;

typedef struct st_chunk_reader st_chunk_reader;

st_chunk_reader    *CHUNK_open(const char *path, char *errbuf, size_t errlen);
st_chunk_reader    *CHUNK_open_string(const char *text);
void                CHUNK_close(st_chunk_reader *r);

/*
 *  Read the next chunk.  Returns 0 at end of input.  The chunk's text is
 *  owned by the reader and is valid until the next call.
 */
int                 CHUNK_next(st_chunk_reader *r, st_chunk *out);

/*  Line number where the current chunk began, for diagnostics.  */
unsigned            CHUNK_line(const st_chunk_reader *r);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_CHUNK_H  */

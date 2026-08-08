/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Reading a source file, whatever format it is in.
 *
 *  The bootstrap used to drive the chunk reader directly and scrape class
 *  definitions out of chunk text as it went, which was exactly right while
 *  there was one format.  There are two now -- the 1983 bang format and
 *  Pharo's Tonel -- and there is no reason for the bootstrap to know which
 *  it is reading.  So a file is a source of EVENTS:
 *
 *      class_def       a class is defined
 *      class_side_def  its metaclass gains instance variables
 *      comment         a class comment
 *      method          one method, as text, for a named class
 *
 *  and the bootstrap supplies a sink.  The producer decides what the bytes
 *  mean; the sink decides what to do about it.
 *
 *  Two things fall out of this that are worth stating, because they are why
 *  the interface is shaped this way rather than as a parser returning a
 *  tree.  A method arrives as SOURCE TEXT with its file and line, so the
 *  compiler's diagnostics keep naming the place a human can look -- that
 *  file:line pair is how nearly every bug in this project was found, and a
 *  format that lost it would be a bad trade.  And a class extension, which
 *  Tonel has and the chunk format does not, needs no new machinery at all:
 *  it is a file that emits no class_def and several method events naming a
 *  class defined somewhere else.
 */

#ifndef ST_SOURCE_H
#define ST_SOURCE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  A growable list of names.
 *
 *  Shared with the bootstrap, which holds a class's instance variables this
 *  way, so that `items` can be handed to the compiler as
 *  `const char *const *` without copying anything.
 */
typedef struct {
    char      **items;
    unsigned    count;
    unsigned    capacity;
} st_names;

int         SRC_names_add(st_names *l, const char *text);
void        SRC_names_free(st_names *l);

/*  What a class definition says.  Absent lists are simply empty.  */
typedef struct {
    const char *name;
    const char *superclass;         /*  "" or "nil" for the root  */
    const char *category;

    const st_names *ivars;
    const st_names *class_ivars;
    const st_names *cvars;
    const st_names *pools;

    int         indexable;
    int         bytes;              /*  byte-indexable rather than pointer  */
    int         words;
    int         weak;               /*  the indexed fields are weak  */

    /*
     *  Set by formats that can express them, so the sink can refuse with a
     *  name rather than mis-build the class.  NULL when absent.
     */
    const char *traits;
    const char *unsupported_shape;  /*  "immediate", "weak", "ephemeron" ... */
} st_source_class_def;

/*
 *  Every callback answers non-zero to continue and zero to stop the file.
 *  diagnostic never stops anything; it reports something the producer could
 *  not act on, and the sink decides whether that is fatal.
 */
typedef struct {
    int  (*class_def)(const st_source_class_def *def, void *user);
    int  (*class_side_def)(const char *name, const st_names *ivars,
                           void *user);
    int  (*comment)(const char *class_name, int class_side, const char *text,
                    void *user);
    int  (*method)(const char *class_name, int class_side,
                   const char *category, const char *source,
                   const char *file, unsigned line, void *user);
    void (*diagnostic)(const char *file, unsigned line, const char *message,
                       void *user);
} st_source_sink;

/*
 *  ----------  A very small STON  ----------
 *
 *  Tonel headers are written in it, and so are the profiles that say which
 *  packages make an image, so there is one reader rather than two.  Only
 *  what those two need: an object of `#key : value` pairs, where a value is
 *  a symbol, a string, a list of them, a number, or a constant.  Values
 *  arrive as text and the caller knows which keys mean what.
 */

typedef struct {
    const char *text;
    size_t      length;
    size_t      pos;
    unsigned    line;
} st_cursor;

typedef struct {
    char        key[64];
    char        value[256];         /*  a scalar  */
    st_names    list;               /*  a [ ... ] */
    int         is_list;
    int         is_nil;
} st_ston_pair;

#define ST_STON_MAX_PAIRS   32

/*  Whitespace and "comments"; the last comment seen is kept if asked for.  */
void        SRC_skip_separators(st_cursor *c, char *comment,
                                size_t comment_len);

/*
 *  Read "{ #a : b, #c : [ d, e ] }" at the cursor.  Answers 0 and fills
 *  `error` on a malformed header.  The caller frees each pair's list.
 */
int         SRC_ston_object(st_cursor *c, st_ston_pair *pairs,
                            unsigned *count, unsigned max,
                            char *error, size_t error_len);
void        SRC_ston_free(st_ston_pair *pairs, unsigned count);

/*  A scalar value by key, or NULL when absent or nil.  */
const char *SRC_ston_value(const st_ston_pair *pairs, unsigned count,
                           const char *key);
/*  A list value by key, or NULL.  */
const st_names *SRC_ston_list(const st_ston_pair *pairs, unsigned count,
                              const char *key);

/*  Slurp a whole file.  The caller frees the result.  */
char       *SRC_slurp(const char *path, size_t *length, char *error,
                      size_t error_len);

/*
 *  Read one file, dispatching on its suffix:
 *
 *      .st  .stClass                the 1983 bang/chunk format
 *      .class.st  .extension.st  .trait.st  package.st     Tonel
 *
 *  Answers 1 on success, 0 if the file could not be read or a callback
 *  stopped it.  `error` is filled on failure.
 */
int SRC_read(const char *path, const st_source_sink *sink, void *user,
             char *error, size_t error_len);

/*  Which producer a path selects, for reporting.  */
const char *SRC_format_of(const char *path);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_SOURCE_H  */

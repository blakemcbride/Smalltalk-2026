/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Pharo's method comments, read as tests.
 *
 *  Pharo documents a method by putting examples in its comment, in a form
 *  that is meant to be machine-readable:
 *
 *      "(#(10 20 30) indexOf: 20) >>> 2"
 *
 *  There are about 1,500 of them in the sources, they were written by the
 *  people who wrote the methods, and they say what the method is FOR rather
 *  than what it happens to do.  That makes them the cheapest correctness
 *  oracle this port will ever get: nobody has to write them, they cannot
 *  drift from the code they sit in, and running them answers the question
 *  the port actually cares about -- not "does Pharo's source parse here"
 *  but "does it MEAN here what it means there".
 *
 *  Extracting them is a lexical job and is all this file does.  Deciding
 *  what a pass means is the caller's.
 */

#ifndef ST_DOCTEST_H
#define ST_DOCTEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char       *expression;
    char       *expected;
    char       *where;              /*  Class>>selector, for the report  */
    char       *file;
    unsigned    line;
} st_doctest;

typedef struct {
    st_doctest *items;
    unsigned    count;
    unsigned    capacity;
    unsigned    files;
    unsigned    methods;
} st_doctest_list;

/*
 *  Collect every doctest in one source file, in either format.  Answers 0
 *  and fills `error` if the file could not be read; a file with no doctests
 *  in it is a success that adds nothing.
 */
int     DOCTEST_scan(const char *path, st_doctest_list *out,
                     char *error, size_t error_len);

void    DOCTEST_free(st_doctest_list *l);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_DOCTEST_H  */

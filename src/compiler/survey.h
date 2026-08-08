/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Compile every method in a set of fileIn chunks and report what failed.
 *
 *  This answers one question with numbers rather than impressions: when this
 *  compiler meets the real 1983 class library, what does it not understand?
 *
 *  It builds no image and needs none.  Literals are fabricated and every
 *  identifier resolves, so what is left is grammar -- which is the thing to
 *  measure first, because a scope error inside a method that will not parse
 *  tells you nothing about either.
 */

#ifndef ST_SURVEY_H
#define ST_SURVEY_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ST_SURVEY_MAX_KINDS 64
#define ST_SURVEY_MAX_PRIMITIVES 512

/*
 *  One primitive a body of source asks for.
 *
 *  A named primitive gets one row per (module, function) rather than one
 *  row for all of them, because "117" is not an answer to the question the
 *  report exists to ask.
 */
typedef struct {
    unsigned    number;
    char        name[160];      /*  "'fn' module: 'Mod'", or ""  */
    unsigned    methods;
    char        example[320];   /*  the first method that asked  */
} st_survey_primitive;

typedef struct {
    char        text[256];      /*  the message, with quoted parts folded  */
    char        example[320];   /*  the first method that hit it           */
    unsigned    count;
} st_survey_failure;

typedef struct {
    unsigned            methods;
    unsigned            failed;
    unsigned            files;
    unsigned            unreadable;
    unsigned            kind_count;
    st_survey_failure   kinds[ST_SURVEY_MAX_KINDS];

    unsigned            primitive_count;
    unsigned            primitives_overflowed;
    st_survey_primitive primitives[ST_SURVEY_MAX_PRIMITIVES];
} st_survey;

void    SURVEY_init(st_survey *s);

/*  Compile every method in one fileIn.  Accumulates into s.  */
void    SURVEY_file(st_survey *s, const char *path);

/*  A summary, most common failure first.  */
void    SURVEY_report(st_survey *s, FILE *out);

/*
 *  Every primitive the surveyed source asks the VM for, in number order,
 *  against what this VM does with it.
 *
 *  This is the checklist that turns "will Pharo's kernel run" from an
 *  unbounded question into a finite one: source names primitives by number,
 *  the numbers are enumerable, and what is left after the ones this VM
 *  answers is the work.  Answers the number of primitives that are absent.
 */
unsigned SURVEY_primitive_report(st_survey *s, FILE *out);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_SURVEY_H  */

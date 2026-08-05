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
} st_survey;

void    SURVEY_init(st_survey *s);

/*  Compile every method in one fileIn.  Accumulates into s.  */
void    SURVEY_file(st_survey *s, const char *path);

/*  A summary, most common failure first.  */
void    SURVEY_report(st_survey *s, FILE *out);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_SURVEY_H  */

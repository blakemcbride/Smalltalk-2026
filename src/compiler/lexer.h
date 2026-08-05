/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Smalltalk-80 lexer.
 *
 *  The grammar is famously small.  The parts that need care are all 1983
 *  details rather than difficulties:
 *
 *      _       assignment, written as a left arrow on the Xerox screen.
 *              Modern code writes ":=" and both are accepted.
 *      ^       return, written as an up arrow.
 *      "..."   a comment, with doubled quotes for a literal one.
 *      '...'   a string, with doubled quotes for a literal one.
 *      $x      a character, and x may be any character at all including a
 *              space or a quote.
 *      #foo    a symbol; #+ and #at:put: are symbols too.
 *      #(...)  a literal array, which may nest and whose bare words are
 *              symbols rather than variables.
 *      2r1010  a radix number; 16rFF is 255.  Also 1e3 and 1.5e-3.
 *      -       a minus sign binds to a numeric literal that follows it
 *              directly, so "3-4" is a send but "3 -4" is two literals.
 */

#ifndef ST_LEXER_H
#define ST_LEXER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ST_TOK_END = 0,
    ST_TOK_IDENTIFIER,      /*  foo, Foo          */
    ST_TOK_KEYWORD,         /*  foo:              */
    ST_TOK_BINARY,          /*  + - // ,          */
    ST_TOK_INTEGER,
    ST_TOK_FLOAT,
    ST_TOK_CHARACTER,       /*  $a                */
    ST_TOK_STRING,          /*  'text'            */
    ST_TOK_SYMBOL,          /*  #foo #+ #at:put:  */
    ST_TOK_ARRAY_OPEN,      /*  #(                */
    ST_TOK_ASSIGN,          /*  _ or :=           */
    ST_TOK_RETURN,          /*  ^                 */
    ST_TOK_SEMICOLON,       /*  ;                 */
    ST_TOK_PERIOD,          /*  .                 */
    ST_TOK_BAR,             /*  |                 */
    ST_TOK_COLON,           /*  : as a block argument marker  */
    ST_TOK_LPAREN,
    ST_TOK_RPAREN,
    ST_TOK_LBRACKET,
    ST_TOK_RBRACKET,
    ST_TOK_PRIMITIVE,       /*  <primitive: 60>   */
    ST_TOK_ERROR
} st_token_kind;

typedef struct {
    st_token_kind   kind;
    char            text[256];
    int64_t         integer;
    double          real;
    unsigned        line;
    unsigned        primitive;      /*  for ST_TOK_PRIMITIVE  */
    /*
     *  Whether anything -- space, tab, newline or a comment -- stood between
     *  this token and the one before it.
     *
     *  Only a literal array needs it, and it needs it badly.  Inside one,
     *  "#(ifTrue:ifFalse:)" is a single symbol and "#(ifTrue: ifFalse:)" is
     *  two, and the lexer hands back two keyword tokens either way; nothing
     *  else in the token distinguishes them.
     */
    int             after_space;
} st_token;

typedef struct st_lexer st_lexer;

/*
 *  A saved position, so the compiler can reconsider.
 *
 *  Two constructs need it.  The receiver of whileTrue: is a block whose body
 *  belongs at the loop head rather than in a BlockContext, and that is only
 *  known after the block has been read.  And a cascade's first message needs
 *  a duplicated receiver, which is only known when the semicolon appears.
 *  Both rewind and compile again.
 */
typedef struct {
    size_t          pos;
    unsigned        line;
    int             has_peek;
    st_token        peeked;
    st_token_kind   last_kind;
} st_lexer_state;

void        LEX_save(st_lexer *lx, st_lexer_state *out);
void        LEX_restore(st_lexer *lx, const st_lexer_state *state);

st_lexer   *LEX_open(const char *source);
void        LEX_close(st_lexer *lx);

/*  Advance to the next token.  Returns 0 at end of input.  */
int         LEX_next(st_lexer *lx, st_token *out);

/*  Look at the next token without consuming it.  */
int         LEX_peek(st_lexer *lx, st_token *out);

const char *LEX_error(const st_lexer *lx);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_LEXER_H  */

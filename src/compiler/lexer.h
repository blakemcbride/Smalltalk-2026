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
 *
 *  Two forms below are post-Blue-Book, from the Squeak/Pharo lineage.  Both
 *  were hard lex errors here until now, so recognising them takes no meaning
 *  away from anything -- which is the whole reason they are cheap to add:
 *
 *      {...}   a dynamic array, whose elements are expressions evaluated at
 *              run time rather than the compile-time literals #(...) takes.
 *      #[...]  a literal ByteArray.
 *
 *  See doc/LanguageExtensions.md for the survey and for the one extension
 *  deliberately NOT added: 1.23s2 already parses as the unary send "1.23 s2",
 *  so it is the only candidate with an existing meaning to take away.
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
    ST_TOK_BYTE_ARRAY_OPEN, /*  #[                */
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
    ST_TOK_LBRACE,          /*  {                 */
    ST_TOK_RBRACE,          /*  }                 */
    ST_TOK_ERROR
} st_token_kind;

typedef struct {
    st_token_kind   kind;
    char            text[256];
    /*
     *  The WHOLE text, when it did not fit in `text'.  NULL when it did.
     *
     *  A string literal is data, and 255 characters is short for the SQL,
     *  the HTML, the JSON templates and the prompts this system was built
     *  to carry.  The scanner used to consume the rest of such a literal
     *  and store none of it, with no diagnostic of any kind, so a
     *  407-character quotation in Benchmark>>longishString came back 255
     *  characters long and stopped mid-word -- and four methods in the
     *  image as built were carrying truncated strings before anyone looked.
     *  A symbol had a second, smaller limit of its own at 127, so `#foo'
     *  and `'foo' asSymbol' disagreed about what a long enough foo means.
     *
     *  So the token carries the whole of it now.  `text' still holds a
     *  truncated copy, because every diagnostic in the compiler prints a
     *  token from it and an empty one would name nothing; anything that
     *  wants the literal's VALUE calls LEX_text, which answers this when
     *  it is set.  The storage belongs to the LEXER and lives until
     *  LEX_close, so a token may be copied, peeked at, saved and restored
     *  like any other -- which the compiler does constantly.
     */
    const char     *long_text;
    size_t          text_length;    /*  strlen of the whole text  */
    int64_t         integer;
    /*
     *  Set when the literal does not fit in `integer'.  The digits are then
     *  in `text' and the radix in `integer_radix', and it is the caller's
     *  business to build the number -- the lexer will not silently hand
     *  back a wrapped one, which is what it used to do: 2 raisedTo: 64
     *  written out as a literal answered 0.
     */
    int             integer_big;
    unsigned        integer_radix;
    double          real;
    unsigned        line;
    /*
     *  Where this token STARTS, as a byte offset into the source the lexer
     *  was opened on.
     *
     *  A line number is what a build log wants and a character offset is
     *  what an EDITOR wants: the image's Compiler>>notify:at: selects the
     *  text at a position, and a line cannot be turned back into one without
     *  the source and a second scan.  Recorded for every token, at the point
     *  skip_blanks has just finished, so it is the first character of the
     *  token itself and not of the whitespace before it.
     */
    size_t          offset;
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

/*
 *  The dialects, shared with the compiler.  Defined here rather than in
 *  compiler.h because the LEXER needs them: two characters changed meaning
 *  after 1983 and the choice has to be made before the first token.
 */
#define ST_DIALECT_BLUE_BOOK    0
#define ST_DIALECT_CLOSURES     1

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

/*
 *  Which dialect to read.  Two characters changed meaning after 1983 and
 *  the lexer cannot guess which is meant: the underscore, which was the
 *  assignment arrow and is now a letter, and the length of a binary
 *  selector, which was two and is now unbounded.  Defaults to Blue Book.
 */
void        LEX_set_dialect(st_lexer *lx, int dialect);

/*
 *  Tell the lexer a statement is about to begin.
 *
 *  The minus rule decides `-1000' from what came before, and after a method
 *  pattern what came before is an identifier -- indistinguishable, to the
 *  lexer, from `foo - 1000'.  So the parser, which does know, says so.
 *  `testAtLeast\n\t-1000 to: 1000 do: [...]' is the shape that needs it, and
 *  it is a shape only a method with no temporaries can have: after `| a b |'
 *  the predecessor is a bar, which the rule already accepts.
 */
void        LEX_begin_statement(st_lexer *lx);
void        LEX_close(st_lexer *lx);

/*  Advance to the next token.  Returns 0 at end of input.  */
int         LEX_next(st_lexer *lx, st_token *out);

/*  Look at the next token without consuming it.  */
int         LEX_peek(st_lexer *lx, st_token *out);

/*
 *  A token's whole text, however long it is.  Never NULL.
 *
 *  Use this wherever the text is the token's VALUE -- a string literal, a
 *  symbol, a variable name -- and `tok.text' only where a truncated copy is
 *  all that is wanted, which is diagnostics.
 */
const char *LEX_text(const st_token *tok);

const char *LEX_error(const st_lexer *lx);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_LEXER_H  */

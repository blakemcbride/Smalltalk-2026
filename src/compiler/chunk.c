/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The chunk format reader.  See chunk.h for the rules.
 */

#include "chunk.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct st_chunk_reader {
    char       *source;     /*  the whole input, owned  */
    size_t      length;
    size_t      pos;
    unsigned    line;
    unsigned    chunk_line;
    char       *buffer;     /*  the chunk being built   */
    size_t      buffer_size;
    int         previous_empty;
};

static st_chunk_reader *
reader_from_text(char *text, size_t length)
{
    st_chunk_reader    *r = (st_chunk_reader *) calloc(1, sizeof *r);

    if (!r) {
        free(text);
        return NULL;
    }
    r->source = text;
    r->length = length;
    r->line   = 1;
    return r;
}

st_chunk_reader *
CHUNK_open(const char *path, char *errbuf, size_t errlen)
{
    FILE       *f;
    long        size;
    char       *text;

    if (errbuf && errlen)
        errbuf[0] = '\0';
    f = fopen(path, "rb");
    if (!f) {
        if (errbuf)
            snprintf(errbuf, errlen, "cannot open %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        if (errbuf)
            snprintf(errbuf, errlen, "cannot size %s", path);
        fclose(f);
        return NULL;
    }
    rewind(f);
    text = (char *) malloc((size_t) size + 1);
    if (!text) {
        if (errbuf)
            snprintf(errbuf, errlen, "out of memory reading %s", path);
        fclose(f);
        return NULL;
    }
    if (fread(text, 1, (size_t) size, f) != (size_t) size) {
        if (errbuf)
            snprintf(errbuf, errlen, "short read on %s", path);
        free(text);
        fclose(f);
        return NULL;
    }
    fclose(f);
    text[size] = '\0';
    return reader_from_text(text, (size_t) size);
}

st_chunk_reader *
CHUNK_open_string(const char *text)
{
    size_t  n = strlen(text);
    char   *copy = (char *) malloc(n + 1);

    if (!copy)
        return NULL;
    memcpy(copy, text, n + 1);
    return reader_from_text(copy, n);
}

void
CHUNK_close(st_chunk_reader *r)
{
    if (!r)
        return;
    free(r->source);
    free(r->buffer);
    free(r);
}

unsigned
CHUNK_line(const st_chunk_reader *r)
{
    return r ? r->chunk_line : 0;
}

static int
append(st_chunk_reader *r, size_t *used, char c)
{
    if (*used + 2 > r->buffer_size) {
        size_t  want = r->buffer_size ? r->buffer_size * 2 : 256;
        char   *grown = (char *) realloc(r->buffer, want);

        if (!grown)
            return 0;
        r->buffer      = grown;
        r->buffer_size = want;
    }
    r->buffer[(*used)++] = c;
    return 1;
}

int
CHUNK_next(st_chunk_reader *r, st_chunk *out)
{
    size_t  used = 0;
    int     saw_content = 0;
    int     saw_code = 0;
    int     was_comment;
    /*  0 outside, 1 inside a 'string', 2 inside a "comment".  */
    int     state = 0;

    if (!r || r->pos >= r->length)
        return 0;

    /*
     *  A bang is purely a separator.  It is tempting to treat one at the
     *  start of a chunk as introducing a reader, since that is how the
     *  format reads on the page -- "!Foo methodsFor: 'x'!" -- but doing so
     *  swallows the bang that begins the NEXT group, because the idiom that
     *  ends a method category is "! !": the first bang closes the last
     *  method and the second closes an empty chunk.
     *
     *  Splitting on bangs alone, that same text falls out correctly: the
     *  method, then an empty chunk, then the reader expression.  Which is
     *  where the rule comes from -- a chunk that follows an empty one is a
     *  reader expression, because the empty chunk is the whitespace sitting
     *  between the previous terminator and the reader's own leading bang.
     */
    r->chunk_line = r->line;

    while (r->pos < r->length) {
        char    c = r->source[r->pos];

        if (c == '!') {
            /*
             *  A doubled bang is a literal one; the two must be adjacent.
             *  Anything else ends the chunk.
             */
            if (r->pos + 1 < r->length && r->source[r->pos + 1] == '!') {
                if (!append(r, &used, '!'))
                    return 0;
                r->pos += 2;
                saw_content = 1;
                if (state != 2)
                    saw_code = 1;      /*  a bang inside a comment is not code  */
                continue;
            }
            ++r->pos;
            break;
        }
        if (c == '\r' || c == '\n') {
            /*
             *  Every line ending becomes a carriage return, because that is
             *  what a line ending IS in Smalltalk-80.
             *
             *  Character cr is 13 and the text machinery knows no other
             *  separator: Paragraph, CharacterScanner and String>>lines all
             *  break on it, and the 1983 sources file is written with it.
             *  Normalizing to a linefeed instead -- the C convention, and
             *  what these files arrive in -- reads back as text with no line
             *  breaks at all.
             *
             *  It shows up as a menu.  The system menu's labels are one
             *  string with nine separators in it, and composed with those as
             *  linefeeds it came out 872 pixels wide and 8 high: ten items
             *  side by side on a single line, off the edge of the screen.
             *  Nothing reported anything, because a Paragraph with no line
             *  breaks is a perfectly good Paragraph.
             */
            if (!append(r, &used, '\r'))
                return 0;
            ++r->line;
            ++r->pos;
            /*  A CRLF pair is one ending, not two.  */
            if (c == '\r' && r->pos < r->length && r->source[r->pos] == '\n')
                ++r->pos;
            continue;
        }

        /*
         *  Track strings and comments, so that a chunk of nothing but a
         *  comment can be told from one with code in it.  A doubled quote is
         *  a literal one and does not leave the construct, and a quote of one
         *  kind inside the other is just a character -- "don't" and
         *  'say "hi"' both occur in the sources.
         *
         *  The state is advanced BEFORE deciding whether this character
         *  counted as code, and a character is discounted if it was in a
         *  comment either side of the move.  Otherwise the quote that opens a
         *  comment is judged before it has opened anything, and every comment
         *  reads as code.
         */
        was_comment = (state == 2);
        if (state == 0) {
            if (c == '\'')
                state = 1;
            else if (c == '"')
                state = 2;
        }  else  {
            char    close = (state == 1) ? '\'' : '"';

            if (c == close) {
                if (r->pos + 1 < r->length && r->source[r->pos + 1] == close) {
                    /*
                     *  Both characters are kept.  Only "!!" is un-doubled by
                     *  this reader; a doubled quote is the LEXER's escape and
                     *  collapsing it here would close the string one quote
                     *  early and swallow the rest of the method.
                     */
                    if (!append(r, &used, c) || !append(r, &used, c))
                        return 0;
                    r->pos += 2;
                    saw_content = 1;
                    if (!was_comment)
                        saw_code = 1;
                    continue;
                }
                state = 0;
            }
        }
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t' && c != '\f') {
            saw_content = 1;
            if (!was_comment && state != 2)
                saw_code = 1;
        }
        if (!append(r, &used, c))
            return 0;
        ++r->pos;
    }

    if (!append(r, &used, '\0'))
        return 0;
    out->text     = r->buffer;
    out->length   = used - 1;
    out->is_empty = !saw_content;
    out->has_code = saw_code;
    /*
     *  A reader expression is the chunk that follows an empty one -- see the
     *  note above on why the leading bang cannot be detected directly.
     */
    out->is_reader = !out->is_empty && r->previous_empty;
    r->previous_empty = out->is_empty;
    return 1;
}

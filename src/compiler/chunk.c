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
    int     is_reader = 0;
    int     saw_content = 0;

    if (!r || r->pos >= r->length)
        return 0;

    /*
     *  Skip whitespace between chunks so that the line number reported is
     *  the one the chunk really starts on.  A "!" found here introduces a
     *  reader chunk rather than terminating an empty one.
     */
    while (r->pos < r->length) {
        char    c = r->source[r->pos];

        if (c == '\n') {
            ++r->line;
            ++r->pos;
        }  else if (c == '\r') {
            /*  The 1983 files use CR alone; treat CRLF as one break.  */
            ++r->line;
            ++r->pos;
            if (r->pos < r->length && r->source[r->pos] == '\n')
                ++r->pos;
        }  else if (c == ' ' || c == '\t' || c == '\f') {
            ++r->pos;
        }  else {
            break;
        }
    }
    if (r->pos >= r->length)
        return 0;

    r->chunk_line = r->line;
    if (r->source[r->pos] == '!') {
        is_reader = 1;
        ++r->pos;
    }

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
                continue;
            }
            ++r->pos;
            break;
        }
        if (c == '\r') {
            /*  Normalize to newline so the parser sees one convention.  */
            if (!append(r, &used, '\n'))
                return 0;
            ++r->line;
            ++r->pos;
            if (r->pos < r->length && r->source[r->pos] == '\n')
                ++r->pos;
            continue;
        }
        if (c == '\n')
            ++r->line;
        else if (c != ' ' && c != '\t' && c != '\f')
            saw_content = 1;
        if (!append(r, &used, c))
            return 0;
        ++r->pos;
    }

    if (!append(r, &used, '\0'))
        return 0;
    out->text      = r->buffer;
    out->length    = used - 1;
    out->is_reader = is_reader;
    out->is_empty  = !saw_content;
    return 1;
}

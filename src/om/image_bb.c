/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Reader for the Xerox Smalltalk-80 version 2 snapshot format.
 *
 *  The Blue Book does not specify this format -- it is documented only in
 *  the Xerox "Smalltalk-80 Virtual Image Version 2" booklet.  The layout,
 *  confirmed against the 1983 image itself:
 *
 *      offset 0    object space length in words     big-endian uint32
 *      offset 4    object table length in words     big-endian uint32
 *      offset 512  object space                     big-endian uint16 words
 *                  padding to a 512-byte boundary
 *      tail        object table                     big-endian uint16 words
 *
 *  The object table occupies exactly the last (otWords * 2) bytes of the
 *  file.  Object space is a flat 20-bit word address space in which an
 *  object at segment s, location l sits at word (s << 16) + l.
 *
 *  Everything is big-endian.  We swap into native word order once, here, so
 *  that every accessor afterwards works on word VALUES.  That is what makes
 *  the Blue Book's byte order inside a word -- byte 0 is the high half --
 *  come out right on a little-endian host without special cases, and it is
 *  where implementations that memcpy raw bytes get Floats wrong.
 */

#include "om_bb.h"
#include "om.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*  Unused by the Xerox format, which has no place to keep it.  */
st_oop      st_om_vm_state[ST_VM_STATE_SLOTS];

uint32_t    st_om_image_object_words;
uint32_t    st_om_image_ot_words;

#define IMAGE_HEADER_PAD    512

static void
fail(char *errbuf, size_t errlen, const char *fmt, ...)
{
    va_list ap;

    if (!errbuf || errlen == 0)
        return;
    va_start(ap, fmt);
    vsnprintf(errbuf, errlen, fmt, ap);
    va_end(ap);
}

/*
 *  Read n big-endian 16-bit words into dst.  Done through a small stack
 *  buffer so we neither allocate nor rely on the host's byte order.
 */
static int
read_be_words(FILE *f, uint16_t *dst, uint32_t n)
{
    unsigned char   buf[4096];
    uint32_t        done = 0;

    while (done < n) {
        uint32_t    want = n - done;
        size_t      got;
        uint32_t    i;

        if (want > sizeof buf / 2)
            want = (uint32_t) (sizeof buf / 2);
        got = fread(buf, 2, want, f);
        if (got != want)
            return -1;
        for (i = 0; i < want; ++i)
            dst[done + i] = (uint16_t) ((buf[i * 2] << 8) | buf[i * 2 + 1]);
        done += want;
    }
    return 0;
}

static int
read_be_u32(FILE *f, uint32_t *out)
{
    unsigned char   b[4];

    if (fread(b, 1, 4, f) != 4)
        return -1;
    *out = ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16)
         | ((uint32_t) b[2] << 8)  | (uint32_t) b[3];
    return 0;
}

int
OM_image_load(const char *path, char *errbuf, size_t errlen)
{
    FILE       *f;
    uint32_t    object_words;
    uint32_t    ot_words;
    long        file_size;
    long        ot_offset;

    if (errbuf && errlen)
        errbuf[0] = '\0';
    if (!st_om_words || !st_om_ot) {
        fail(errbuf, errlen, "object memory not initialized");
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        fail(errbuf, errlen, "cannot open %s", path);
        return -1;
    }
    if (read_be_u32(f, &object_words) != 0 || read_be_u32(f, &ot_words) != 0) {
        fail(errbuf, errlen, "%s: truncated header", path);
        fclose(f);
        return -1;
    }
    if (object_words == 0 || object_words > ST_HEAP_WORDS) {
        fail(errbuf, errlen, "%s: object space of %u words does not fit %u",
             path, object_words, (unsigned) ST_HEAP_WORDS);
        fclose(f);
        return -1;
    }
    if (ot_words == 0 || ot_words > ST_OT_WORDS) {
        fail(errbuf, errlen, "%s: object table of %u words does not fit %u",
             path, ot_words, (unsigned) ST_OT_WORDS);
        fclose(f);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fail(errbuf, errlen, "%s: cannot seek", path);
        fclose(f);
        return -1;
    }
    file_size = ftell(f);
    ot_offset = file_size - (long) ot_words * 2;
    if (ot_offset < IMAGE_HEADER_PAD + (long) object_words * 2) {
        fail(errbuf, errlen,
             "%s: object table at %ld overlaps object space ending at %ld",
             path, ot_offset, IMAGE_HEADER_PAD + (long) object_words * 2);
        fclose(f);
        return -1;
    }

    if (fseek(f, IMAGE_HEADER_PAD, SEEK_SET) != 0
     || read_be_words(f, st_om_words, object_words) != 0) {
        fail(errbuf, errlen, "%s: truncated object space", path);
        fclose(f);
        return -1;
    }
    if (fseek(f, ot_offset, SEEK_SET) != 0
     || read_be_words(f, st_om_ot, ot_words) != 0) {
        fail(errbuf, errlen, "%s: truncated object table", path);
        fclose(f);
        return -1;
    }
    fclose(f);

    st_om_image_object_words = object_words;
    st_om_image_ot_words     = ot_words;

    /*
     *  The object table occupies a whole 64K-word segment, but an image only
     *  ships as many entries as it was using -- this one, 19,368 of a
     *  possible 32,767.  Everything past that is unused table, not absent
     *  table, so mark it free and make it available.  Without this the VM
     *  runs out of object pointers within a few hundred thousand bytecodes
     *  while megabytes of heap sit idle, because every message send needs an
     *  entry for its context.
     *
     *  calloc zeroed those words, and a zero entry has its free bit clear,
     *  which would read as a live object at location 0.  They have to be
     *  marked explicitly.
     */
    st_om_ot_limit = ST_OT_WORDS;
    {
        uint32_t    entry;

        for (entry = ot_words; entry + 1 < ST_OT_WORDS; entry += 2)
            OM_set_free_bit((st_oop) entry, 1);
    }

    /*
     *  A cheap structural check that catches a byte-order or offset mistake
     *  immediately rather than a thousand bytecodes later: nil, true and
     *  false must be live objects, and nil's class must itself be a live
     *  object whose class is in turn live.
     */
    if (!OM_is_object(ST_NIL) || !OM_is_object(ST_TRUE) || !OM_is_object(ST_FALSE)) {
        fail(errbuf, errlen, "%s: nil/true/false are not live objects", path);
        return -1;
    }
    if (!OM_is_object(OM_fetch_class(ST_NIL))) {
        fail(errbuf, errlen, "%s: class of nil is not a live object", path);
        return -1;
    }
    if (OM_rebuild_free_lists() != 0) {
        fail(errbuf, errlen, "%s: cannot build free lists", path);
        return -1;
    }
    return 0;
}

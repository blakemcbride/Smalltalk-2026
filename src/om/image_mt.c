/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The native snapshot format.
 *
 *  Unlike the Xerox format this one is ours, so it is versioned from the
 *  start, little-endian, and self-describing: each object carries its class,
 *  its size and its format, so a reader never has to infer a layout.
 *
 *  Reference counts are deliberately not stored.  They are derived after
 *  loading by running the collector, which rebuilds every count from
 *  reachability -- so a snapshot cannot carry corrupt counts into a fresh
 *  session, and the format does not have to be revised if counting changes.
 */

#include "om_mt.h"
#include "om.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

st_oop      st_om_vm_state[ST_VM_STATE_SLOTS];

uint32_t    st_om_image_object_words;
uint32_t    st_om_image_ot_words;

#define IMAGE_MAGIC     "ST26MT\0\0"
#define IMAGE_MAGIC_LEN 8
#define IMAGE_VERSION   2       /*  2 added the VM-state slots  */

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

static int
write_u32(FILE *f, uint32_t v)
{
    unsigned char   b[4];

    b[0] = (unsigned char) (v & 0xFF);
    b[1] = (unsigned char) ((v >> 8) & 0xFF);
    b[2] = (unsigned char) ((v >> 16) & 0xFF);
    b[3] = (unsigned char) ((v >> 24) & 0xFF);
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static int
read_u32(FILE *f, uint32_t *out)
{
    unsigned char   b[4];

    if (fread(b, 1, 4, f) != 4)
        return -1;
    *out = (uint32_t) b[0] | ((uint32_t) b[1] << 8)
         | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
    return 0;
}

static int
write_u64(FILE *f, uint64_t v)
{
    return write_u32(f, (uint32_t) (v & 0xFFFFFFFF)) != 0
        || write_u32(f, (uint32_t) (v >> 32)) != 0 ? -1 : 0;
}

static int
read_u64(FILE *f, uint64_t *out)
{
    uint32_t    lo;
    uint32_t    hi;

    if (read_u32(f, &lo) != 0 || read_u32(f, &hi) != 0)
        return -1;
    *out = (uint64_t) lo | ((uint64_t) hi << 32);
    return 0;
}

/*  Bytes occupied by an object's data, given its format.  */
static size_t
body_bytes(uint32_t flags, uint32_t size)
{
    if (flags & ST_FMT_POINTERS)
        return (size_t) size * sizeof(st_oop);
    if (flags & ST_FMT_WORDS)
        return (size_t) size * sizeof(uint16_t);
    return size;
}

int
OM_image_save(const char *path, char *errbuf, size_t errlen)
{
    FILE       *f;
    uint32_t    index;

    if (errbuf && errlen)
        errbuf[0] = '\0';
    f = fopen(path, "wb");
    if (!f) {
        fail(errbuf, errlen, "cannot write %s", path);
        return -1;
    }
    if (fwrite(IMAGE_MAGIC, 1, IMAGE_MAGIC_LEN, f) != IMAGE_MAGIC_LEN
     || write_u32(f, IMAGE_VERSION) != 0
     || write_u32(f, st_om_table_limit) != 0) {
        fail(errbuf, errlen, "%s: cannot write the header", path);
        fclose(f);
        return -1;
    }
    /*  The VM's own connections to the image -- see om.h.  */
    {
        unsigned    slot;

        for (slot = 0; slot < ST_VM_STATE_SLOTS; ++slot) {
            if (write_u64(f, (uint64_t) st_om_vm_state[slot]) != 0) {
                fail(errbuf, errlen, "%s: cannot write the header", path);
                fclose(f);
                return -1;
            }
        }
    }
    for (index = 1; index < st_om_table_limit; ++index) {
        om_header  *head = OM_table_get(index);
        unsigned char present;

        present = (head && (head->flags & ST_FMT_FREE) == 0) ? 1 : 0;
        if (fwrite(&present, 1, 1, f) != 1) {
            fail(errbuf, errlen, "%s: short write", path);
            fclose(f);
            return -1;
        }
        if (!present)
            continue;
        if (write_u64(f, head->class_oop) != 0
         || write_u32(f, head->size) != 0
         || write_u32(f, head->flags) != 0
         || write_u32(f, head->hash) != 0) {
            fail(errbuf, errlen, "%s: short write", path);
            fclose(f);
            return -1;
        }
        {
            size_t  bytes = body_bytes(head->flags, head->size);

            /*
             *  Pointer fields are written as 64-bit values one at a time so
             *  the file stays little-endian regardless of the host.
             */
            if (head->flags & ST_FMT_POINTERS) {
                uint32_t    i;

                for (i = 0; i < head->size; ++i) {
                    if (write_u64(f, ((st_oop *) (head + 1))[i]) != 0) {
                        fail(errbuf, errlen, "%s: short write", path);
                        fclose(f);
                        return -1;
                    }
                }
            }  else if (bytes && fwrite(head + 1, 1, bytes, f) != bytes) {
                fail(errbuf, errlen, "%s: short write", path);
                fclose(f);
                return -1;
            }
        }
    }
    fclose(f);
    return 0;
}

int
OM_image_load(const char *path, char *errbuf, size_t errlen)
{
    FILE       *f;
    char        magic[IMAGE_MAGIC_LEN];
    uint32_t    version;
    uint32_t    limit;
    uint32_t    index;

    if (errbuf && errlen)
        errbuf[0] = '\0';
    f = fopen(path, "rb");
    if (!f) {
        fail(errbuf, errlen, "cannot open %s", path);
        return -1;
    }
    if (fread(magic, 1, IMAGE_MAGIC_LEN, f) != IMAGE_MAGIC_LEN
     || memcmp(magic, IMAGE_MAGIC, IMAGE_MAGIC_LEN) != 0) {
        fail(errbuf, errlen,
             "%s: not a Smalltalk-2026 image (the Xerox format needs OM=bb)",
             path);
        fclose(f);
        return -1;
    }
    version = 0;
    if (read_u32(f, &version) != 0 || version != IMAGE_VERSION) {
        fail(errbuf, errlen, "%s: image version %u, expected %u", path,
             version, IMAGE_VERSION);
        fclose(f);
        return -1;
    }
    if (read_u32(f, &limit) != 0 || limit == 0) {
        fail(errbuf, errlen, "%s: bad object count", path);
        fclose(f);
        return -1;
    }
    {
        unsigned    slot;

        for (slot = 0; slot < ST_VM_STATE_SLOTS; ++slot) {
            uint64_t    value;

            if (read_u64(f, &value) != 0) {
                fail(errbuf, errlen, "%s: truncated header", path);
                fclose(f);
                return -1;
            }
            st_om_vm_state[slot] = (st_oop) value;
        }
    }

    if (OM_init() != 0) {
        fail(errbuf, errlen, "cannot allocate the object table");
        fclose(f);
        return -1;
    }
    /*
     *  Object pointers in the file refer to table indices, so the table has
     *  to regain exactly its old shape -- including the gaps, which keep
     *  every stored pointer valid.
     */
    while (st_om_table_size < limit) {
        st_atomic_ptr  *grown = (st_atomic_ptr *) realloc(st_om_table,
                                    (size_t) st_om_table_size * 2
                                        * sizeof *grown);

        if (!grown) {
            fail(errbuf, errlen, "cannot grow the object table to %u", limit);
            fclose(f);
            return -1;
        }
        memset(grown + st_om_table_size, 0,
               (size_t) st_om_table_size * sizeof *grown);
        st_om_table = grown;
        st_om_table_size *= 2;
    }
    st_om_table_limit = limit;

    for (index = 1; index < limit; ++index) {
        unsigned char   present;
        uint64_t        class_oop;
        uint32_t        size;
        uint32_t        flags;
        uint32_t        hash;
        om_header      *head;
        size_t          bytes;

        if (fread(&present, 1, 1, f) != 1) {
            fail(errbuf, errlen, "%s: truncated at object %u", path, index);
            fclose(f);
            return -1;
        }
        if (!present) {
            /*  A hole: keep a free header so the index stays reserved.  */
            head = (om_header *) calloc(1, sizeof *head);
            if (!head) {
                fail(errbuf, errlen, "out of memory");
                fclose(f);
                return -1;
            }
            head->flags = ST_FMT_FREE;
            OM_table_set(index, head);
            continue;
        }
        if (read_u64(f, &class_oop) != 0 || read_u32(f, &size) != 0
         || read_u32(f, &flags) != 0 || read_u32(f, &hash) != 0) {
            fail(errbuf, errlen, "%s: truncated at object %u", path, index);
            fclose(f);
            return -1;
        }
        bytes = body_bytes(flags, size);
        head  = (om_header *) calloc(1, sizeof *head + bytes);
        if (!head) {
            fail(errbuf, errlen, "out of memory at object %u", index);
            fclose(f);
            return -1;
        }
        head->class_oop = class_oop;
        head->size      = size;
        head->flags     = flags & ~ST_FMT_FREE;
        head->hash      = hash;
        head->refcount  = 0;
        if (flags & ST_FMT_POINTERS) {
            uint32_t    i;

            for (i = 0; i < size; ++i) {
                uint64_t    value;

                if (read_u64(f, &value) != 0) {
                    fail(errbuf, errlen, "%s: truncated in object %u", path,
                         index);
                    free(head);
                    fclose(f);
                    return -1;
                }
                ((st_oop *) (head + 1))[i] = value;
            }
        }  else if (bytes && fread(head + 1, 1, bytes, f) != bytes) {
            fail(errbuf, errlen, "%s: truncated in object %u", path, index);
            free(head);
            fclose(f);
            return -1;
        }
        OM_table_set(index, head);
    }
    fclose(f);

    st_om_image_ot_words     = limit;
    st_om_image_object_words = 0;

    /*
     *  Counts were not stored.  One collection rebuilds them all from
     *  reachability, and reclaims anything the writer left unreferenced.
     */
    OM_collect();
    return 0;
}

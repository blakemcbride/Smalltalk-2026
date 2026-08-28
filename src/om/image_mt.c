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
#include "st_port.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

st_oop      st_om_vm_state[ST_VM_STATE_SLOTS];

uint32_t    st_om_image_object_words;
uint32_t    st_om_image_ot_words;

#define IMAGE_MAGIC     "ST26MT\0\0"
#define IMAGE_MAGIC_LEN 8
/*  2 added the VM-state slots; 3 added BlockClosure to them.  */
/*
 *  3 -> 4: st_om_vm_state gained a fifth slot, the #outOfMemory selector, and
 *  the header carries one 64-bit word per slot.  An older image is refused by
 *  version rather than read with its slots one short, which would put the
 *  object count where a selector belongs.
 */
/*
 *  4 -> 5: a sixth VM-state slot, the #recursionDepthExceeded selector.
 *  Refused by version rather than read one slot short, for the reason the
 *  fifth was.
 */
/*
 *  5 -> 6: a seventh VM-state slot, the #corruptMethod selector (Bugs3
 *  B11).  Refused by version for the same reason as the two before it.
 */
#define IMAGE_VERSION   6

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

/*
 *  Write the whole image to `tmp'.  The messages name `path', the image
 *  the caller asked for, because that is the name the caller knows; the
 *  temporary is OM_image_save's business, below.
 */
static int
write_image(const char *tmp, const char *path, char *errbuf, size_t errlen)
{
    FILE       *f;
    uint32_t    index;

    if (errbuf && errlen)
        errbuf[0] = '\0';
    /*
     *  Collect first, always.
     *
     *  The writer takes every table entry not marked free, and the collector
     *  runs only at a safepoint -- so between two of them the table holds
     *  every context ever activated, and all of it would go to disk.  Blake's
     *  desktop, a few minutes old, snapshotted to 423 MB: 3,032,732 objects
     *  written, 2,348,775 of them contexts, and 1,957,320 of THOSE with a
     *  non-integer instruction pointer or a method field pointing nowhere.
     *  Dead frames, written out in full, for a reachable image of ten
     *  megabytes.
     *
     *  It belongs here rather than in primitive 97 because it is the
     *  writer's own invariant: a snapshot is the reachable image.  The
     *  loader has always collected on the way in -- see the comment at the
     *  end of OM_image_load, which says it is reclaiming what the writer
     *  left -- and that is exactly the asymmetry this removes.
     */
    OM_collect();
    f = fopen(tmp, "wb");
    if (!f) {
        fail(errbuf, errlen, "cannot write %s: %s", tmp, strerror(errno));
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
    /*
     *  Everything above went into a stdio buffer; the last of it reaches
     *  the kernel at fflush and the device at fsync, and either can be the
     *  step that fails -- a full disk says so at the flush, not at the
     *  fwrite that filled the buffer.  Checked, because a file this
     *  function reports as written is about to replace the only image.
     */
    if (ST_file_sync(f) != 0) {
        fail(errbuf, errlen, "%s: cannot flush the image to disk: %s", path,
             strerror(errno));
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        fail(errbuf, errlen, "%s: cannot close the image: %s", path,
             strerror(errno));
        return -1;
    }
    return 0;
}

/*
 *  Written beside the target and renamed over it, never written in place.
 *
 *  fopen(path, "wb") truncates the old image before the first byte of the
 *  new one is written, so anything that stopped the write part way -- a
 *  full disk, a file-size limit, a SIGINT, the process dying -- left the
 *  only copy truncated: a 4.3 MB image cut to 1,024,000 bytes by a
 *  `ulimit -f 1000', and refused at the next start with "truncated in
 *  object 15173" (Bugs3 B10).  snapshotAs:, -o and the desktop's save all
 *  come through here, so every one of them could destroy the image it was
 *  asked to preserve.
 *
 *  So the new image goes to `<path>.tmp' in the same directory, is flushed
 *  and synced to the device, closed, and only then renamed over the target
 *  -- one step on every platform we build for; see ST_file_replace.  A
 *  failure anywhere before the rename leaves the old file exactly as it
 *  was, and the temporary is removed so it does not sit beside the image
 *  pretending to be one.  The same directory, deliberately: a rename
 *  across file systems is a copy, and a copy is not one step.
 */
int
OM_image_save(const char *path, char *errbuf, size_t errlen)
{
    size_t  len = strlen(path);
    char   *tmp = (char *) malloc(len + sizeof ".tmp");

    if (errbuf && errlen)
        errbuf[0] = '\0';
    if (!tmp) {
        fail(errbuf, errlen, "out of memory");
        return -1;
    }
    memcpy(tmp, path, len);
    memcpy(tmp + len, ".tmp", sizeof ".tmp");
    if (write_image(tmp, path, errbuf, errlen) != 0) {
        (void) remove(tmp);
        free(tmp);
        return -1;
    }
    if (ST_file_replace(tmp, path) != 0) {
        fail(errbuf, errlen, "cannot replace %s with the new image: %s",
             path, strerror(errno));
        (void) remove(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

/*
 *  Every pointer the file gave us, checked before anything follows it.
 *
 *  The loader used to take the file's word for every oop: a pointer field
 *  went into the body as read, and the first thing to look at it was the
 *  collector's mark, which reads the header it names.  So a corrupt image
 *  was found out by a segfault in mark_visit -- three of twenty-four
 *  single-byte flips did that -- or not found out at all: a class pointer
 *  aimed at a free entry, a field aimed past the table, both loaded and
 *  ran until something reached the hole (Bugs3 B58).  OM_is_object guards
 *  the collector now, but the interpreter reads fields with OM_head and no
 *  guard at all, on purpose, because not asking is what an object table is
 *  for.  The interpreter's guard is here: nothing gets into the table that
 *  is not what the interpreter assumes about it.
 *
 *  What is assumed, and therefore checked, object by object: the format
 *  bits name one of the three formats; the class pointer names an object
 *  that is present; every field of a pointer object is a SmallInteger or
 *  names a present object; a CompiledMethod's header is a SmallInteger and
 *  each literal it counts is a SmallInteger or a present object, which is
 *  the walk the marker makes; the guaranteed objects -- nil, true, false,
 *  the kernel classes, the special selectors -- are all present, since the
 *  interpreter names them by number and never asks; and each VM-state slot
 *  is empty, a SmallInteger or a present object.  A file that fails any of
 *  these is refused with the object's index in the message and nothing of
 *  it runs: the caller reports and exits.
 *
 *  The walk also finds the highest identity hash in the image, for
 *  OM_continue_identity_hashes_after, since every object is being looked
 *  at anyway.
 */
static int
oop_present(st_oop p, uint32_t limit)
{
    om_header  *head;

    if (p == ST_OOP_INVALID || (p & 1))
        return 0;
    if ((p >> 1) >= (st_oop) limit)
        return 0;
    head = OM_table_get((uint32_t) (p >> 1));
    return head != NULL && (head->flags & ST_FMT_FREE) == 0;
}

static int
oop_valid(st_oop p, uint32_t limit)
{
    return (p & 1) != 0 || oop_present(p, limit);
}

static int
check_image(const char *path, uint32_t limit, char *errbuf, size_t errlen,
            uint32_t *max_hash)
{
    uint32_t    index;
    unsigned    slot;

    *max_hash = 0;
    /*
     *  The guaranteed objects first, on their own, because everything
     *  refers to them: with nil missing, the first fault the walk below
     *  would report is a VM-state slot or a class pointer that "is not an
     *  object", and the useful message is that the image has no nil.
     */
    for (index = 1; ((st_oop) index << 1) <= ST_LAST_IMMORTAL_OOP; ++index) {
        om_header  *head = index < limit ? OM_table_get(index) : NULL;

        if (!head || (head->flags & ST_FMT_FREE)) {
            fail(errbuf, errlen, "%s: guaranteed object %u is missing",
                 path, index);
            return -1;
        }
    }
    for (slot = 0; slot < ST_VM_STATE_SLOTS; ++slot) {
        st_oop  value = st_om_vm_state[slot];

        if (value != ST_OOP_INVALID && !oop_valid(value, limit)) {
            fail(errbuf, errlen,
                 "%s: VM state slot %u holds 0x%llx, which is not an object",
                 path, slot, (unsigned long long) value);
            return -1;
        }
    }
    for (index = 1; index < limit; ++index) {
        om_header  *head = OM_table_get(index);
        st_oop     *fields;
        uint32_t    format;
        uint32_t    i;

        if (!head || (head->flags & ST_FMT_FREE))
            continue;
        if (head->hash > *max_hash)
            *max_hash = head->hash;
        format = head->flags & (ST_FMT_POINTERS | ST_FMT_WORDS | ST_FMT_BYTES);
        if (format != ST_FMT_POINTERS && format != ST_FMT_WORDS
         && format != ST_FMT_BYTES) {
            fail(errbuf, errlen,
                 "%s: object %u has format flags 0x%x, which name no format",
                 path, index, (unsigned) head->flags);
            return -1;
        }
        if (!oop_present(head->class_oop, limit)) {
            fail(errbuf, errlen,
                 "%s: object %u: class pointer 0x%llx is not an object",
                 path, index, (unsigned long long) head->class_oop);
            return -1;
        }
        fields = (st_oop *) (head + 1);
        if (head->class_oop == ST_CLASS_COMPILED_METHOD) {
            uint32_t    slots = head->size / (uint32_t) sizeof(st_oop);
            uint32_t    literals;

            if (slots == 0)
                continue;
            if ((fields[0] & 1) == 0) {
                fail(errbuf, errlen,
                     "%s: object %u: a CompiledMethod whose header 0x%llx "
                     "is not a SmallInteger",
                     path, index, (unsigned long long) fields[0]);
                return -1;
            }
            literals = (uint32_t) ((fields[0] >> 1) & 63);
            for (i = 1; i <= literals && i < slots; ++i) {
                if (!oop_valid(fields[i], limit)) {
                    fail(errbuf, errlen,
                         "%s: object %u: literal %u is 0x%llx, which is "
                         "not an object",
                         path, index, i, (unsigned long long) fields[i]);
                    return -1;
                }
            }
        }
        if (format != ST_FMT_POINTERS)
            continue;
        for (i = 0; i < head->size; ++i) {
            if (!oop_valid(fields[i], limit)) {
                fail(errbuf, errlen,
                     "%s: object %u: field %u is 0x%llx, which is not an "
                     "object",
                     path, index, i, (unsigned long long) fields[i]);
                return -1;
            }
        }
    }
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
     *
     *  Through OM_grow_table_to, which is the object memory's own growth and
     *  grows the COUNTS beside the table.  This loop used to be written out
     *  here and doubled st_om_table alone, leaving st_om_refcounts at the
     *  size OM_init gave it -- so an image with more than four million
     *  objects would have written past the end of the counts a few lines
     *  below.  Nothing could build such an image while the table had a fixed
     *  ceiling; now that it grows, the two limits are one variable and there
     *  is one function that moves it.
     */
    if (!OM_grow_table_to(limit)) {
        fail(errbuf, errlen, "cannot grow the object table to %u", limit);
        fclose(f);
        return -1;
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
        /*  Counts live beside the table now; see om_mt.h.  */
        ST_store_relaxed(&st_om_refcounts[index], 0);
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

    /*
     *  Before the collector, which is the first thing that would follow a
     *  pointer -- see check_image.  A refused image leaves the table as it
     *  is; the caller exits, and a test that loads another calls OM_init
     *  through this function again.
     */
    {
        uint32_t    max_hash;

        if (check_image(path, limit, errbuf, errlen, &max_hash) != 0)
            return -1;
        OM_continue_identity_hashes_after(max_hash);
    }

    st_om_image_ot_words     = limit;
    st_om_image_object_words = 0;

    /*
     *  Counts were not stored.  One collection rebuilds them all from
     *  reachability, and reclaims anything the writer left unreferenced.
     */
    OM_collect();
    return 0;
}

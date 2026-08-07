/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Phase 2 gate: run the 1983 Xerox image and reproduce Xerox's own
 *  execution trace byte for byte.
 *
 *  This is the sharpest test in the project.  trace2 records 499 bytecodes,
 *  63 sends, 34 returns and 13 primitive invocations from the image's
 *  startup, and every line carries a decoded bytecode name, a cycle number
 *  and the receiver and arguments of each send.  Reproducing it exactly
 *  means the bytecode set, the context layout, method lookup, the three
 *  primitive dispatch paths, reference counting and the object memory all
 *  agree with the VM that produced the file in 1985.
 *
 *  The interpreter is compiled once and shares its source with the threaded
 *  build, so passing here validates the code the parallel system runs.
 */

#include "st_test.h"

/*
 *  These check the Blue Book object memory against the 1983 image, so they
 *  only apply to that build.  Under OM=mt they compile to a stub.
 */
#ifdef ST_OM_BB

#include "om.h"
#include "interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_PATH      "oracle/VirtualImage"
#define TRACE2_PATH     "oracle/trace2"
#define TRACE3_PATH     "oracle/trace3"

/*  trace2 covers this many bytecodes; running further would append lines.  */
#define TRACE2_CYCLES   499

/*
 *  Capture the trace.  A temporary file rather than open_memstream, which is
 *  POSIX and has no Windows counterpart.
 */
static char *
run_trace(st_trace_mode mode, uint64_t cycles, size_t *len_out)
{
    char       *buffer = NULL;
    size_t      len    = 0;
    FILE       *stream;
    char        err[256];

    stream = tmpfile();
    if (!stream)
        return NULL;
    if (OM_init() != 0 || OM_image_load(IMAGE_PATH, err, sizeof err) != 0) {
        fclose(stream);
        free(buffer);
        return NULL;
    }
    if (ST_interp_init(err, sizeof err) != 0) {
        fclose(stream);
        free(buffer);
        OM_shutdown();
        return NULL;
    }
    /*  The reference files open with the Xerox banner.  */
    fprintf(stream, "Copyright (c) 1983 Xerox Corp.  All rights reserved.\n\n");
    ST_trace_set(mode, stream);
    ST_interp_run(cycles);
    ST_trace_set(ST_TRACE_OFF, NULL);

    {
        long    size;

        fflush(stream);
        if (fseek(stream, 0, SEEK_END) != 0 || (size = ftell(stream)) < 0) {
            fclose(stream);
            OM_shutdown();
            return NULL;
        }
        rewind(stream);
        buffer = (char *) malloc((size_t) size + 1);
        if (!buffer) {
            fclose(stream);
            OM_shutdown();
            return NULL;
        }
        len = fread(buffer, 1, (size_t) size, stream);
        buffer[len] = '\0';
    }
    fclose(stream);
    OM_shutdown();
    *len_out = len;
    return buffer;
}

/*
 *  Compare against the reference file, which uses CR line endings in places
 *  and must be normalized.  Reports the first differing line, since that is
 *  the one worth debugging.
 */
static int
compare_to_reference(const char *path, const char *actual, int *lines_out)
{
    FILE       *f = fopen(path, "rb");
    const char *p = actual;
    int         line = 0;
    int         mismatches = 0;
    char        expected[4096];

    *lines_out = 0;
    if (!f)
        return -1;
    while (fgets(expected, sizeof expected, f)) {
        char        got[4096];
        size_t      n = 0;
        char       *nl;

        ++line;
        nl = strpbrk(expected, "\r\n");
        if (nl)
            *nl = '\0';

        while (p[n] && p[n] != '\n' && n < sizeof got - 1)
            ++n;
        memcpy(got, p, n);
        got[n] = '\0';
        p += n;
        if (*p == '\n')
            ++p;

        if (strcmp(got, expected) != 0) {
            if (mismatches < 5)
                printf("  line %d:\n    Xerox: \"%s\"\n    ours : \"%s\"\n",
                       line, expected, got);
            ++mismatches;
        }
    }
    fclose(f);
    *lines_out = line;
    return mismatches;
}

static int
file_exists(const char *path)
{
    FILE   *f = fopen(path, "rb");

    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static void
test_trace2(void)
{
    char   *actual;
    size_t  len;
    int     mismatches;
    int     lines = 0;

    actual = run_trace(ST_TRACE_BYTECODES, TRACE2_CYCLES, &len);
    CHECK(actual != NULL);
    if (!actual)
        return;
    mismatches = compare_to_reference(TRACE2_PATH, actual, &lines);
    printf("  trace2: %d reference lines, %d mismatches\n", lines, mismatches);
    CHECK_EQ_INT(lines, 611);
    CHECK_EQ_INT(mismatches, 0);
    free(actual);
}

/*
 *  trace3 is the same run reported at send level with one tab per call
 *  depth.  It matches for its first 482 lines and then takes a different
 *  route through one message.
 *
 *  At cycle 1680 the image evaluates "16383 @ 16383" -- both operands are
 *  exactly SmallInteger maxVal.  The Xerox VM's primitive 18 refuses it and
 *  falls back to the Smalltalk body, which builds the same Point the long
 *  way round via Point class>>x:y:.  Ours accepts it.  Every later
 *  difference is that detour's cycle numbers shifting the rest of the file;
 *  the sequence of sends is otherwise identical, and both runs produce the
 *  same Point.
 *
 *  The same primitive succeeds silently for "0 @ 0" earlier in trace2, so
 *  this is a value-dependent limit in the 1983 VM at the SmallInteger
 *  boundary rather than a rule we can derive.  Primitive 18 is one the Blue
 *  Book marks optional, meaning the Smalltalk fallback must be complete --
 *  which is exactly why both paths agree on the result.  We do not reproduce
 *  the refusal: failing a correct primitive to match a quirk would be
 *  bending the VM to the trace rather than to the specification.
 *
 *  This test therefore pins the prefix that must not regress.
 */
#define TRACE3_MATCHING_PREFIX  482

static void
test_trace3_prefix(void)
{
    char   *actual;
    size_t  len;
    FILE   *f;
    char    expected[4096];
    const char *p;
    int     line = 0;
    int     mismatches = 0;

    actual = run_trace(ST_TRACE_SENDS, 1979, &len);
    CHECK(actual != NULL);
    if (!actual)
        return;
    f = fopen(TRACE3_PATH, "rb");
    if (!f) {
        free(actual);
        return;
    }
    p = actual;
    while (line < TRACE3_MATCHING_PREFIX && fgets(expected, sizeof expected, f)) {
        char    got[4096];
        size_t  n = 0;
        char   *nl;

        ++line;
        nl = strpbrk(expected, "\r\n");
        if (nl)
            *nl = '\0';
        while (p[n] && p[n] != '\n' && n < sizeof got - 1)
            ++n;
        memcpy(got, p, n);
        got[n] = '\0';
        p += n;
        if (*p == '\n')
            ++p;
        if (strcmp(got, expected) != 0) {
            if (mismatches < 5)
                printf("  trace3 line %d:\n    Xerox: \"%s\"\n    ours : \"%s\"\n",
                       line, expected, got);
            ++mismatches;
        }
    }
    fclose(f);
    printf("  trace3: %d of %d prefix lines checked, %d mismatches\n",
           line, TRACE3_MATCHING_PREFIX, mismatches);
    CHECK_EQ_INT(line, TRACE3_MATCHING_PREFIX);
    CHECK_EQ_INT(mismatches, 0);
    free(actual);
}

/*
 *  ----------  What the closure design rests on  ----------
 *
 *  Squeak's closures put a MethodContext's closure in the field this layout
 *  calls ST_CTX_INITIAL_IP, and mark an unwind-protected method with
 *  primitive 198 and a handler with 199.  Both are only safe here if the
 *  1983 image never uses either -- and the evidence for that is documentary
 *  rather than measured: MethodContext declares field 4 as "receiverMap",
 *  its own comment says "unused (we expect to use it later for multiple
 *  inheritance)", and nothing in sources/ assigns it; the primitive numbers
 *  the 1983 library declares stop well below 198.
 *
 *  Documentary is not good enough for something everything else depends on.
 *  The shipped VirtualImage was built by Xerox from sources that need not
 *  match the ones vendored here, so this asks the image itself.
 */
static void
test_the_image_leaves_room_for_closures(void)
{
    char        err[256];
    st_oop      p;
    unsigned    contexts = 0;
    unsigned    used_field_4 = 0;
    unsigned    methods = 0;
    unsigned    marker_primitives = 0;
    unsigned    highest_primitive = 0;

    if (OM_init() != 0 || OM_image_load(IMAGE_PATH, err, sizeof err) != 0) {
        printf("  cannot load %s: %s\n", IMAGE_PATH, err);
        CHECK(0);
        return;
    }
    for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
        st_oop  class_oop = OM_fetch_class(p);

        if (class_oop == ST_CLASS_METHOD_CONTEXT) {
            ++contexts;
            if (OM_is_present(OM_fetch_pointer(ST_CTX_INITIAL_IP, p)))
                ++used_field_4;
            continue;
        }
        if (class_oop == ST_CLASS_COMPILED_METHOD) {
            unsigned    primitive;

            ++methods;
            primitive = ST_method_primitive_index(p);
            if (primitive > highest_primitive)
                highest_primitive = primitive;
            if (primitive == 198 || primitive == 199)
                ++marker_primitives;
        }
    }
    printf("  %u contexts, %u using field 4; %u methods, highest primitive %u\n",
           contexts, used_field_4, methods, highest_primitive);

    /*  Field 4 is free for closureOrNil.  */
    CHECK(contexts > 0);
    CHECK_EQ_INT((int) used_field_4, 0);
    /*  198 and 199 are free to mean "unwind" and "handler".  */
    CHECK(methods > 1000);
    CHECK_EQ_INT((int) marker_primitives, 0);

    OM_shutdown();
}

int
main(void)
{
    ST_TEST_BEGIN("interpreter against the Xerox traces");

    if (!file_exists(IMAGE_PATH)) {
        printf("  SKIP: %s missing -- see doc/LICENSING.md\n", IMAGE_PATH);
        return ST_TEST_END();
    }
    test_the_image_leaves_room_for_closures();
    test_trace2();
    test_trace3_prefix();
    return ST_TEST_END();
}

#else   /*  not ST_OM_BB  */

int
main(void)
{
    printf("skipped: this suite validates the Blue Book object memory\n");
    return 0;
}

#endif

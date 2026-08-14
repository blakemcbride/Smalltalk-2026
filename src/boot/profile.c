/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Profiles.  See profile.h.
 */

#include "profile.h"

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

#define MAX_DEPTH   16

typedef struct {
    st_names    files;
    int        *dialects;           /*  one per file  */
    unsigned    dialect_capacity;
    st_names    exclude;
    st_names    supersede;
    /*
     *  The files a #supersede dropped.  Kept because dropping them is the
     *  moment protocol can be lost silently: the replacement is a different
     *  class by a different author, and nothing else in the system will
     *  notice that it answers less than the thing it replaced.
     */
    st_names    superseded_paths;
    /*
     *  How much of `exclude' applies to the packages being added right now.
     *
     *  A profile excludes a class in order to REPLACE it -- that is what
     *  the substitution ratchet is -- so its own packages must not be
     *  filtered by its own exclusions, or it removes the replacement along
     *  with the thing replaced.  Excluding SharedQueue to supersede the
     *  1983 one with lib/Concurrency's deleted both and left the image
     *  without the class at all.
     *
     *  So: exclusions apply to everything a profile INHERITS -- the
     *  profiles it requires, the manifests it names -- and not to what it
     *  provides itself.  This holds the count as it stood before this
     *  profile added its own.
     */
    unsigned    supersede_own;
    st_names    loaded;             /*  profiles already expanded  */
    char       *error;
    size_t      error_len;
} expansion;

/*  Add a file, remembering which dialect the profile that named it uses.  */
static int
add_file(expansion *e, const char *path, int dialect)
{
    if (e->files.count == e->dialect_capacity) {
        unsigned    want = e->dialect_capacity ? e->dialect_capacity * 2 : 256;
        int        *grown = (int *) realloc(e->dialects, want * sizeof *grown);

        if (!grown)
            return 0;
        e->dialects         = grown;
        e->dialect_capacity = want;
    }
    e->dialects[e->files.count] = dialect;
    return SRC_names_add(&e->files, path);
}

static int expand_one(expansion *e, const char *path, unsigned depth);

/*  ----------  Paths  ----------  */

/*  The directory a path lives in, "." when it has none.  */
static void
directory_of(const char *path, char *out, size_t out_len)
{
    const char *slash = strrchr(path, '/');

    if (!slash) {
        snprintf(out, out_len, ".");
        return;
    }
    snprintf(out, out_len, "%.*s", (int) (slash - path), path);
}

/*
 *  A path that is not absolute is relative to `base`.
 *
 *  Answers 0 rather than truncating.  A truncated path does not fail here;
 *  it fails later as "cannot open", naming a file nobody wrote.
 */
static int
resolve(const char *base, const char *path, char *out, size_t out_len)
{
    int n;

    if (path[0] == '/')
        n = snprintf(out, out_len, "%s", path);
    else
        n = snprintf(out, out_len, "%s/%s", base, path);
    return n > 0 && (size_t) n < out_len;
}

/*
 *  The class name a source file carries, which is what #exclude names.
 *  "a/b/Foo.class.st" and "a/b/Foo/Foo.stClass" both answer "Foo".
 */
static void
class_name_of(const char *path, char *out, size_t out_len)
{
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    const char *dot = strchr(name, '.');
    size_t      n = dot ? (size_t) (dot - name) : strlen(name);

    if (n >= out_len)
        n = out_len - 1;
    memcpy(out, name, n);
    out[n] = '\0';
}

/*
 *  Does this file DEFINE its class, rather than extend one?
 *
 *  It decides what #supersede drops, and the distinction is not pedantic.
 *  Superseding says "my version of that class instead of the one I
 *  inherited", which is a statement about the class DEFINITION.  It is not
 *  a statement about everyone else's extensions to it -- and dropping those
 *  too is silently destructive, because an extension is usually the only
 *  reason the class answers something.
 *
 *  Found by superseding Dictionary with Pharo's: lib/Collections-Protocol/
 *  Dictionary.extension.st went with it, so at:ifAbsentPut: and the rest
 *  vanished from that image, and the method I had just added to fix three
 *  tests was never installed.  Nothing reported it; the class was there and
 *  simply answered less.
 */
static int
file_defines_its_class(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;

    return strstr(name, ".extension.") == NULL;
}

/*
 *  Forget an earlier file that defines this class, so a later package of the
 *  same profile can provide it instead.  Answers nothing: a class named in
 *  #supersede that nothing earlier defined is the ordinary case, not an
 *  error.
 */
static void
drop_earlier_definition(expansion *e, const char *want)
{
    unsigned    i;

    for (i = 0; i < e->files.count; ++i) {
        char    name[256];

        class_name_of(e->files.items[i], name, sizeof name);
        if (strcmp(name, want) != 0 || !file_defines_its_class(e->files.items[i]))
            continue;
        SRC_names_add(&e->superseded_paths, e->files.items[i]);
        free(e->files.items[i]);
        memmove(&e->files.items[i], &e->files.items[i + 1],
                (e->files.count - i - 1) * sizeof *e->files.items);
        memmove(&e->dialects[i], &e->dialects[i + 1],
                (e->files.count - i - 1) * sizeof *e->dialects);
        --e->files.count;
        return;
    }
}


static int
names_contain_upto(const st_names *l, const char *text, unsigned limit)
{
    unsigned    i;

    if (limit > l->count)
        limit = (unsigned) l->count;
    for (i = 0; i < limit; ++i)
        if (strcmp(l->items[i], text) == 0)
            return 1;
    return 0;
}

static int
names_contain(const st_names *l, const char *text)
{
    unsigned    i;

    for (i = 0; i < l->count; ++i) {
        if (strcmp(l->items[i], text) == 0)
            return 1;
    }
    return 0;
}

static int
is_source_file(const char *name)
{
    size_t  n = strlen(name);

    if (n > 3 && strcmp(name + n - 3, ".st") == 0)
        return 1;
    if (n > 8 && strcmp(name + n - 8, ".stClass") == 0)
        return 1;
    return 0;
}

/*  ----------  Directories  ----------  */

static int
compare_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/*
 *  Every source file in a directory, sorted.
 *
 *  Sorted because a directory hands its entries back in whatever order the
 *  file system likes, and a build whose load order depends on that is a
 *  build that cannot be compared against another one.  Load order does not
 *  affect correctness -- pass zero settles that -- but it does affect the
 *  bytes, and being able to say "byte for byte the same image" is worth
 *  a qsort.
 */
static int
add_directory(expansion *e, const char *dir, int dialect)
{
    st_names    found;
    unsigned    i;
    int         ok = 1;

    memset(&found, 0, sizeof found);

#if defined(_WIN32)
    {
        WIN32_FIND_DATAA    data;
        HANDLE              h;
        char                pattern[1024];

        snprintf(pattern, sizeof pattern, "%s\\*", dir);
        h = FindFirstFileA(pattern, &data);
        if (h == INVALID_HANDLE_VALUE) {
            snprintf(e->error, e->error_len, "cannot read directory %s", dir);
            return 0;
        }
        do {
            if (is_source_file(data.cFileName))
                SRC_names_add(&found, data.cFileName);
        } while (FindNextFileA(h, &data));
        FindClose(h);
    }
#else
    {
        DIR            *d = opendir(dir);
        struct dirent  *entry;

        if (!d) {
            snprintf(e->error, e->error_len, "cannot read directory %s", dir);
            return 0;
        }
        while ((entry = readdir(d)) != NULL) {
            if (is_source_file(entry->d_name))
                SRC_names_add(&found, entry->d_name);
        }
        closedir(d);
    }
#endif

    if (found.count > 1)
        qsort(found.items, found.count, sizeof found.items[0], compare_names);

    for (i = 0; i < found.count && ok; ++i) {
        char    path[1024];
        char    name[256];

        if (snprintf(path, sizeof path, "%s/%s", dir, found.items[i])
                >= (int) sizeof path) {
            snprintf(e->error, e->error_len, "%s: path too long", dir);
            ok = 0;
            break;
        }
        class_name_of(path, name, sizeof name);
        /*
         *  Two lists, answering different questions.
         *
         *  #exclude drops a class wherever it comes from -- Pharo's
         *  package manifests, say: lint metadata for a browser we do not
         *  have, carrying a superclass we will never implement.
         *
         *  #supersede says "this profile provides that class instead", so
         *  it drops only what is INHERITED and leaves this profile's own
         *  copy alone.  Using one mechanism for both removed both
         *  providers and left the image without the class at all, which is
         *  how the distinction was learnt.
         */
        if (names_contain(&e->exclude, name))
            continue;
        if (names_contain_upto(&e->supersede, name, e->supersede_own)
         && file_defines_its_class(path)) {
            /*
             *  Superseded, not merely dropped.  Remember the file so the
             *  guard can ask afterwards whether the class that replaced it
             *  still answers what it defined -- see PROFILE_superseded_files.
             */
            SRC_names_add(&e->superseded_paths, path);
            continue;
        }
        /*
         *  A superseded class may also be replaced from WITHIN this
         *  profile, by a later package than the one that first defined it.
         *  The rule is the same one as above read the other way round --
         *  the last provider wins -- and it is needed whenever a package is
         *  imported whole and one of its classes has to be corrected here:
         *  WeakOrderedCollection is Pharo's, and Pharo's gets its weakness
         *  from a storage array that this system's OrderedCollection does
         *  not have.
         *
         *  Excluding it instead removes both providers, which is the
         *  distinction #supersede exists to make; superseding it from an
         *  inherited package cannot work either, because the thing being
         *  replaced is not inherited.  So the drop happens here, after the
         *  fact, and the replaced file is recorded for the same guard.
         */
        if (names_contain(&e->supersede, name) && file_defines_its_class(path))
            drop_earlier_definition(e, name);
        ok = add_file(e, path, dialect);
    }
    SRC_names_free(&found);
    return ok;
}

/*  ----------  Manifests  ----------  */

static int
add_manifest(expansion *e, const char *path, int dialect)
{
    FILE   *f = fopen(path, "r");
    char    line[1024];

    if (!f) {
        snprintf(e->error, e->error_len, "cannot open manifest %s", path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        size_t  n = strlen(line);
        char    name[256];

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (!n)
            continue;
        class_name_of(line, name, sizeof name);
        /*
         *  Manifests are inherited content, so both lists apply in
         *  full -- the 1983 SharedQueue arrives through sources/MANIFEST
         *  rather than through a package directory, and a #supersede
         *  that did not look here left it in place beside ours.
         */
        if (names_contain(&e->exclude, name))
            continue;
        if (names_contain(&e->supersede, name)
         && file_defines_its_class(line)) {
            SRC_names_add(&e->superseded_paths, line);
            continue;
        }
        add_file(e, line, dialect);
    }
    fclose(f);
    return 1;
}

/*  ----------  A profile  ----------  */

static int
expand_one(expansion *e, const char *path, unsigned depth)
{
    unsigned    inherited;

    char           *text;
    size_t          length;
    st_cursor       cursor;
    st_ston_pair    pairs[ST_STON_MAX_PAIRS];
    unsigned        count = 0;
    char            base[1024];
    char            detail[512];
    const st_names *list;
    const char     *dialect_name;
    int             dialect = ST_DIALECT_BLUE_BOOK;
    unsigned        i;
    int             ok = 1;

    if (depth >= MAX_DEPTH) {
        snprintf(e->error, e->error_len,
                 "%s: profiles are nested more than %d deep", path, MAX_DEPTH);
        return 0;
    }
    if (names_contain(&e->loaded, path))
        return 1;                   /*  required by two things; load once  */
    SRC_names_add(&e->loaded, path);

    text = SRC_slurp(path, &length, e->error, e->error_len);
    if (!text)
        return 0;
    directory_of(path, base, sizeof base);

    memset(&cursor, 0, sizeof cursor);
    memset(pairs, 0, sizeof pairs);
    cursor.text   = text;
    cursor.length = length;
    cursor.line   = 1;

    SRC_skip_separators(&cursor, NULL, 0);
    /*  The word "Profile" is optional decoration; the object is the thing. */
    while (cursor.pos < cursor.length && cursor.text[cursor.pos] != '{')
        ++cursor.pos;

    if (!SRC_ston_object(&cursor, pairs, &count, ST_STON_MAX_PAIRS,
                         detail, sizeof detail)) {
        snprintf(e->error, e->error_len, "%s: %s", path, detail);
        free(text);
        return 0;
    }

    /*
     *  Exclusions are collected before anything is added, so that a profile
     *  can exclude something a profile it requires would otherwise bring.
     */
    dialect_name = SRC_ston_value(pairs, count, "dialect");
    if (dialect_name) {
        if (strcmp(dialect_name, "closures") == 0) {
            dialect = ST_DIALECT_CLOSURES;
        }  else if (strcmp(dialect_name, "bluebook") != 0) {
            snprintf(e->error, e->error_len,
                     "%s: unknown dialect '%s'", path, dialect_name);
            free(text);
            return 0;
        }
    }

    inherited = e->supersede_own;
    list = SRC_ston_list(pairs, count, "exclude");
    for (i = 0; list && i < list->count; ++i)
        SRC_names_add(&e->exclude, list->items[i]);
    list = SRC_ston_list(pairs, count, "supersede");
    for (i = 0; list && i < list->count; ++i)
        SRC_names_add(&e->supersede, list->items[i]);
    /*
     *  Requires and manifests see this profile's exclusions; its own
     *  packages, below, see only what it inherited.
     */
    e->supersede_own = (unsigned) e->supersede.count;

    list = SRC_ston_list(pairs, count, "requires");
    for (i = 0; list && i < list->count && ok; ++i) {
        char    required[1024];

        if (!resolve(base, list->items[i], required, sizeof required)) {
            snprintf(e->error, e->error_len, "%s: path too long", path);
            ok = 0;
            break;
        }
        if (!strstr(list->items[i], ".profile"))
            snprintf(required + strlen(required),
                     sizeof required - strlen(required), ".profile");
        ok = expand_one(e, required, depth + 1);
    }

    list = SRC_ston_list(pairs, count, "manifests");
    for (i = 0; list && i < list->count && ok; ++i) {
        char    manifest[1024];

        if (!resolve(base, list->items[i], manifest, sizeof manifest)) {
            snprintf(e->error, e->error_len, "%s: path too long", path);
            ok = 0;
            break;
        }
        ok = add_manifest(e, manifest, dialect);
    }

    list = SRC_ston_list(pairs, count, "packages");
    for (i = 0; list && i < list->count && ok; ++i) {
        char    dir[1024];

        if (!resolve(base, list->items[i], dir, sizeof dir)) {
            snprintf(e->error, e->error_len, "%s: path too long", path);
            ok = 0;
            break;
        }
        {
            unsigned    saved = e->supersede_own;

            e->supersede_own = inherited;
            ok = add_directory(e, dir, dialect);
            e->supersede_own = saved;
        }
    }

    list = SRC_ston_list(pairs, count, "files");
    for (i = 0; list && i < list->count && ok; ++i) {
        char    file[1024];
        char    name[256];

        if (!resolve(base, list->items[i], file, sizeof file)) {
            snprintf(e->error, e->error_len, "%s: path too long", path);
            ok = 0;
            break;
        }
        class_name_of(file, name, sizeof name);
        if (names_contain(&e->exclude, name))
            continue;
        ok = add_file(e, file, dialect);
    }

    SRC_ston_free(pairs, count);
    free(text);
    return ok;
}

/*
 *  The files the last expansion superseded.
 *
 *  A single global rather than an out-parameter because PROFILE_expand is
 *  called once per run and threading a fifth argument through every caller
 *  buys nothing.  Read it with PROFILE_superseded_files immediately after
 *  expanding.
 */
static st_names     last_superseded;

const st_names *
PROFILE_superseded_files(void)
{
    return &last_superseded;
}

int
PROFILE_expand(const char *path, st_names *out, int **dialects, char *error,
               size_t error_len)
{
    expansion   e;
    int         ok;

    memset(&e, 0, sizeof e);
    e.error     = error;
    e.error_len = error_len;
    if (error && error_len)
        error[0] = '\0';

    ok = expand_one(&e, path, 0);
    SRC_names_free(&e.exclude);
    SRC_names_free(&e.supersede);
    SRC_names_free(&e.loaded);
    SRC_names_free(&last_superseded);
    last_superseded = e.superseded_paths;
    if (!ok) {
        SRC_names_free(&e.files);
        free(e.dialects);
        memset(out, 0, sizeof *out);
        if (dialects)
            *dialects = NULL;
        return 0;
    }
    *out = e.files;
    if (dialects)
        *dialects = e.dialects;
    else
        free(e.dialects);
    return 1;
}

/*  Is this path a directory?  */
static int
is_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

/*
 *  ----------  A directory of sources, walked  ----------
 *
 *  Phase 5's exit criterion is spelled `st80 -bootstrap sources/ -o
 *  st80.image', and until now only `-manifest sources/MANIFEST' worked --
 *  a bare directory was handed to the reader as if it were a file, which
 *  failed with "short read on sources/".
 *
 *  Walking it recursively is faithful rather than approximate: the 1983
 *  tree holds 226 .stClass files and MANIFEST names all of them, and the
 *  bootstrap reads definitions in a pass of its own before compiling
 *  anything, so the order a manifest fixes is not load-bearing.  Sorted at
 *  every level regardless, because a bootstrap that depends on readdir
 *  order is a bootstrap that differs between machines.
 */
int
PROFILE_expand_tree(const char *dir, st_names *out, char *error,
                    size_t error_len)
{
    st_names    here;
    unsigned    i;
    int         ok = 1;

    /*
     *  A bare directory has no profile and so supersedes nothing.  Cleared
     *  rather than left alone so that the guard cannot read a list from an
     *  expansion this one replaced.
     */
    SRC_names_free(&last_superseded);
    memset(&here, 0, sizeof here);
#ifdef _WIN32
    {
        WIN32_FIND_DATAA    data;
        HANDLE              h;
        char                pattern[1024];

        snprintf(pattern, sizeof pattern, "%s\\*", dir);
        h = FindFirstFileA(pattern, &data);
        if (h == INVALID_HANDLE_VALUE) {
            snprintf(error, error_len, "cannot read directory %s", dir);
            return 0;
        }
        do {
            if (data.cFileName[0] != '.')
                SRC_names_add(&here, data.cFileName);
        } while (FindNextFileA(h, &data));
        FindClose(h);
    }
#else
    {
        DIR            *d = opendir(dir);
        struct dirent  *entry;

        if (!d) {
            snprintf(error, error_len, "cannot read directory %s", dir);
            return 0;
        }
        while ((entry = readdir(d)) != NULL)
            if (entry->d_name[0] != '.')
                SRC_names_add(&here, entry->d_name);
        closedir(d);
    }
#endif
    if (here.count > 1)
        qsort(here.items, here.count, sizeof here.items[0], compare_names);

    for (i = 0; i < here.count && ok; ++i) {
        char    path[1024];

        if (snprintf(path, sizeof path, "%s/%s", dir, here.items[i])
                >= (int) sizeof path) {
            snprintf(error, error_len, "%s: path too long", dir);
            ok = 0;
            break;
        }
        if (is_directory(path)) {
            ok = PROFILE_expand_tree(path, out, error, error_len);
        }  else if (is_source_file(here.items[i])) {
            ok = SRC_names_add(out, path);
        }
    }
    SRC_names_free(&here);
    return ok;
}

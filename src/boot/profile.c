/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Profiles.  See profile.h.
 */

#include "profile.h"

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
    st_names    exclude;
    st_names    loaded;             /*  profiles already expanded  */
    char       *error;
    size_t      error_len;
} expansion;

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
add_directory(expansion *e, const char *dir)
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
        if (names_contain(&e->exclude, name))
            continue;
        ok = SRC_names_add(&e->files, path);
    }
    SRC_names_free(&found);
    return ok;
}

/*  ----------  Manifests  ----------  */

static int
add_manifest(expansion *e, const char *path)
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
        if (names_contain(&e->exclude, name))
            continue;
        SRC_names_add(&e->files, line);
    }
    fclose(f);
    return 1;
}

/*  ----------  A profile  ----------  */

static int
expand_one(expansion *e, const char *path, unsigned depth)
{
    char           *text;
    size_t          length;
    st_cursor       cursor;
    st_ston_pair    pairs[ST_STON_MAX_PAIRS];
    unsigned        count = 0;
    char            base[1024];
    char            detail[512];
    const st_names *list;
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
    list = SRC_ston_list(pairs, count, "exclude");
    for (i = 0; list && i < list->count; ++i)
        SRC_names_add(&e->exclude, list->items[i]);

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
        ok = add_manifest(e, manifest);
    }

    list = SRC_ston_list(pairs, count, "packages");
    for (i = 0; list && i < list->count && ok; ++i) {
        char    dir[1024];

        if (!resolve(base, list->items[i], dir, sizeof dir)) {
            snprintf(e->error, e->error_len, "%s: path too long", path);
            ok = 0;
            break;
        }
        ok = add_directory(e, dir);
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
        ok = SRC_names_add(&e->files, file);
    }

    SRC_ston_free(pairs, count);
    free(text);
    return ok;
}

int
PROFILE_expand(const char *path, st_names *out, char *error, size_t error_len)
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
    SRC_names_free(&e.loaded);
    if (!ok) {
        SRC_names_free(&e.files);
        memset(out, 0, sizeof *out);
        return 0;
    }
    *out = e.files;
    return 1;
}

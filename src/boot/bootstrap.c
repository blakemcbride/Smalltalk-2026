/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The image bootstrap.  See bootstrap.h for the shape of the problem.
 */

#include "bootstrap.h"
#include "chunk.h"
#include "compiler.h"
#include "interp.h"
#include "census.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_CLASSES     512
#define MAX_IVARS       64
#define MAX_SYMBOLS     8192

/*  ----------  Bootstrap state  ----------  */

typedef struct {
    char        name[64];
    char        superclass[64];
    char        ivars[MAX_IVARS][64];
    unsigned    ivar_count;
    char        class_ivars[MAX_IVARS][64];
    unsigned    class_ivar_count;
    int         indexable;
    int         bytes;              /*  byte-indexable rather than pointer  */
    int         words;

    st_oop      class_oop;
    st_oop      metaclass_oop;

    /*  Instance variables including every inherited one, in frame order.  */
    char        all_ivars[MAX_IVARS][64];
    unsigned    all_ivar_count;
    int         resolved;
} boot_class;

static boot_class   classes[MAX_CLASSES];
static unsigned     class_count;

static st_oop       symbols[MAX_SYMBOLS];
static char         symbol_text[MAX_SYMBOLS][64];
static unsigned     symbol_count;

static st_oop       smalltalk;          /*  the SystemDictionary  */
static st_oop       globals_values;
static unsigned     global_count;
#define MAX_GLOBALS 1024

static st_bootstrap_result *result;

/*
 *  Classes the interpreter names by fixed pointer.  They are allocated
 *  before any global exists, so the mapping is kept here rather than looked
 *  up; without it the fixed pointers would end up naming empty placeholders
 *  while the real classes lived elsewhere.
 */
static struct {
    const char *name;
    st_oop      oop;
} reserved_classes[32];
static unsigned reserved_class_count;

static st_oop
reserved_class(const char *name)
{
    unsigned    i;

    for (i = 0; i < reserved_class_count; ++i) {
        if (strcmp(reserved_classes[i].name, name) == 0)
            return reserved_classes[i].oop;
    }
    return ST_OOP_INVALID;
}

static void
boot_fail(const char *fmt, ...)
{
    va_list ap;

    if (!result || result->error[0])
        return;
    va_start(ap, fmt);
    vsnprintf(result->error, sizeof result->error, fmt, ap);
    va_end(ap);
}

/*  ----------  Literals the compiler asks us to make  ----------  */

static st_oop
make_string_object(const char *text)
{
    size_t  n = strlen(text);
    st_oop  s = OM_instantiate_bytes(BOOT_global("String"), (uint32_t) n);
    size_t  i;

    if (!OM_is_object(s))
        return ST_NIL;
    for (i = 0; i < n; ++i)
        OM_store_byte((uint32_t) i, s, (uint8_t) text[i]);
    return s;
}

st_oop
BOOT_intern_symbol(const char *text, void *user)
{
    unsigned    i;
    st_oop      s;
    size_t      n = strlen(text);

    (void) user;
    for (i = 0; i < symbol_count; ++i) {
        if (strcmp(symbol_text[i], text) == 0)
            return symbols[i];
    }
    s = OM_instantiate_bytes(BOOT_global("Symbol"), (uint32_t) n);
    if (!OM_is_object(s))
        return ST_NIL;
    for (i = 0; i < n; ++i)
        OM_store_byte(i, s, (uint8_t) text[i]);
    if (symbol_count < MAX_SYMBOLS) {
        symbols[symbol_count] = s;
        snprintf(symbol_text[symbol_count], 64, "%.63s", text);
        ++symbol_count;
        if (result)
            ++result->symbols_interned;
    }
    /*  Symbols are permanent; nothing else keeps the table alive.  */
    OM_increase_ref(s);
    return s;
}

st_oop
BOOT_make_string(const char *text, void *user)
{
    (void) user;
    return make_string_object(text);
}

st_oop
BOOT_make_float(double value, void *user)
{
    st_oop  f = OM_instantiate_words(BOOT_global("Float"), 4);
    union { double d; uint16_t w[4]; } bits;
    unsigned i;

    (void) user;
    if (!OM_is_object(f))
        return ST_NIL;
    bits.d = value;
    for (i = 0; i < 4; ++i)
        OM_store_word(i, f, bits.w[i]);
    return f;
}

st_oop
BOOT_make_large_integer(int64_t value, void *user)
{
    /*
     *  A 64-bit object memory has room for these as SmallIntegers, so the
     *  path only matters once a literal exceeds 62 bits.
     */
    uint64_t    magnitude = (value < 0) ? (uint64_t) -value : (uint64_t) value;
    st_oop      big;
    unsigned    bytes = 0;
    unsigned    i;
    uint64_t    scan = magnitude;

    (void) user;
    while (scan) {
        ++bytes;
        scan >>= 8;
    }
    if (bytes == 0)
        bytes = 1;
    big = OM_instantiate_bytes(BOOT_global("LargePositiveInteger"), bytes);
    if (!OM_is_object(big))
        return ST_NIL;
    for (i = 0; i < bytes; ++i)
        OM_store_byte(i, big, (uint8_t) ((magnitude >> (i * 8)) & 0xFF));
    return big;
}

st_oop
BOOT_make_array(st_oop *elements, unsigned count, void *user)
{
    st_oop      array = OM_instantiate_pointers(BOOT_global("Array"), count);
    unsigned    i;

    (void) user;
    if (!OM_is_object(array))
        return ST_NIL;
    for (i = 0; i < count; ++i)
        OM_store_pointer(i, array, elements[i]);
    return array;
}

/*  ----------  Globals  ----------  */

st_oop
BOOT_smalltalk(void)
{
    return smalltalk;
}

/*
 *  Answers ST_OOP_INVALID when the name is unknown -- deliberately not nil.
 *  nil is a perfectly good object, so a caller testing the result with
 *  OM_is_object would take "not found" for "found", which is exactly the
 *  mistake that left every global undefined: define_global saw nil, believed
 *  the binding already existed, and quietly overwrote a field of nil instead
 *  of making one.
 */
/*
 *  The globals live in the image, hanging off Smalltalk, and are searched
 *  there rather than in a table beside it.  A C-side table would go stale
 *  the moment an image was written and read back, since the reload replaces
 *  every object; keeping them in the graph means a reloaded image is as
 *  usable as the one that was built.
 */
static st_oop
global_association(const char *name)
{
    st_oop      associations;
    uint32_t    n;
    uint32_t    i;
    char        text[64];

    if (!OM_is_present(ST_SMALLTALK))
        return ST_OOP_INVALID;
    associations = OM_fetch_pointer(0, ST_SMALLTALK);
    if (!OM_is_present(associations))
        return ST_OOP_INVALID;
    n = OM_fetch_word_length(associations);
    for (i = 0; i < n; ++i) {
        st_oop  association = OM_fetch_pointer(i, associations);

        if (!OM_is_present(association))
            continue;
        OM_string_of(OM_fetch_pointer(ST_ASSOCIATION_KEY, association),
                     text, sizeof text);
        if (strcmp(text, name) == 0)
            return association;
    }
    return ST_OOP_INVALID;
}

st_oop
BOOT_global(const char *name)
{
    st_oop  association = global_association(name);

    if (association == ST_OOP_INVALID)
        return ST_NIL;
    return OM_fetch_pointer(ST_ASSOCIATION_VALUE, association);
}

/*
 *  Globals live in an Association each, because that is what a compiled
 *  method's literal frame holds: the method points at the binding, not the
 *  value, so reassigning a global is visible to code already compiled.
 */
static st_oop
define_global(const char *name, st_oop value)
{
    st_oop      association = global_association(name);
    st_oop      key;

    if (association != ST_OOP_INVALID) {
        OM_store_pointer(ST_ASSOCIATION_VALUE, association, value);
        return association;
    }
    if (global_count >= MAX_GLOBALS) {
        boot_fail("more than %d globals", MAX_GLOBALS);
        return ST_NIL;
    }
    key         = BOOT_intern_symbol(name, NULL);
    /*
     *  Association itself may not exist yet during the earliest steps, so a
     *  binding made then carries a nil class and is adopted later.
     */
    association = OM_instantiate_pointers(BOOT_global("Association"), 2);
    if (!OM_is_object(association))
        return ST_OOP_INVALID;
    OM_increase_ref(association);
    OM_store_pointer(ST_ASSOCIATION_KEY, association, key);
    OM_store_pointer(ST_ASSOCIATION_VALUE, association, value);
    OM_store_pointer(global_count, globals_values, association);
    ++global_count;
    return association;
}

st_oop
BOOT_lookup_global(const char *name, void *user)
{
    (void) user;
    return global_association(name);
}

/*  ----------  Class construction  ----------  */

static boot_class *
find_class(const char *name)
{
    unsigned    i;

    for (i = 0; i < class_count; ++i) {
        if (strcmp(classes[i].name, name) == 0)
            return &classes[i];
    }
    return NULL;
}

/*
 *  The format word, in the layout derived from the 1983 image itself:
 *  bit 15 pointers, bit 14 not-bytes, bit 13 indexable, bits 1..11 the count
 *  of named instance variables.  It is a SmallInteger, so the low bit is the
 *  tag and every field sits one place higher than its nominal position.
 */
static st_oop
make_format(unsigned fixed, int pointers, int indexable, int bytes)
{
    uint64_t    bits = 1;               /*  SmallInteger tag  */

    if (pointers)
        bits |= (uint64_t) 1 << 15;
    if (!bytes)
        bits |= (uint64_t) 1 << 14;
    if (indexable)
        bits |= (uint64_t) 1 << 13;
    bits |= (uint64_t) (fixed & 0x7FF) << 1;
    return (st_oop) bits;
}

/*  Behavior's own instance variables, which every class object begins with. */
#define CLASS_SUPERCLASS        0
#define CLASS_METHOD_DICT       1
#define CLASS_FORMAT            2
#define CLASS_SUBCLASSES        3
#define CLASS_INSTANCE_VARS     4
#define CLASS_ORGANIZATION      5
#define CLASS_NAME              6
#define CLASS_POOL              7
#define CLASS_SHARED_POOLS      8
#define CLASS_FIXED_FIELDS      9

#define METACLASS_THIS_CLASS    6
#define METACLASS_FIXED_FIELDS  7

/*
 *  Resolve a class's full instance-variable list by walking to the root and
 *  concatenating on the way back down.  The order is what the compiler
 *  assigns indices from, so it has to match what the interpreter expects:
 *  inherited variables first, in superclass order.
 */
static int
resolve_ivars(boot_class *c, unsigned depth)
{
    boot_class *super;
    unsigned    i;

    if (c->resolved)
        return 1;
    if (depth > 64) {
        boot_fail("class %s has a cycle in its superclass chain", c->name);
        return 0;
    }
    c->all_ivar_count = 0;
    if (c->superclass[0] && strcmp(c->superclass, "nil") != 0) {
        super = find_class(c->superclass);
        if (!super) {
            boot_fail("class %s has an unknown superclass %s", c->name,
                      c->superclass);
            return 0;
        }
        if (!resolve_ivars(super, depth + 1))
            return 0;
        for (i = 0; i < super->all_ivar_count; ++i) {
            snprintf(c->all_ivars[c->all_ivar_count], 64, "%.63s",
                     super->all_ivars[i]);
            ++c->all_ivar_count;
        }
        /*  Shape is inherited unless the subclass declares its own.  */
        if (!c->indexable && super->indexable) {
            c->indexable = super->indexable;
            c->bytes     = super->bytes;
            c->words     = super->words;
        }
    }
    for (i = 0; i < c->ivar_count && c->all_ivar_count < MAX_IVARS; ++i) {
        snprintf(c->all_ivars[c->all_ivar_count], 64, "%.63s", c->ivars[i]);
        ++c->all_ivar_count;
    }
    c->resolved = 1;
    return 1;
}

/*  ----------  Method dictionaries  ----------  */

/*
 *  A MethodDictionary is a Set with a parallel value array: field 0 is the
 *  tally, field 1 the values, and the indexed part from field 2 holds the
 *  selectors.  Method lookup walks exactly this, and it was validated
 *  against Xerox's method.oops before any of it was written.
 */
static st_oop
make_method_dictionary(unsigned capacity)
{
    st_oop  dict;
    st_oop  values;

    if (capacity < 4)
        capacity = 4;
    dict = OM_instantiate_pointers(BOOT_global("MethodDictionary"),
                                   ST_MD_FIRST_KEY + capacity);
    if (!OM_is_object(dict))
        return ST_NIL;
    values = OM_instantiate_pointers(BOOT_global("Array"), capacity);
    if (!OM_is_object(values))
        return ST_NIL;
    OM_store_pointer(ST_MD_TALLY, dict, OM_int_oop(0));
    OM_store_pointer(ST_MD_VALUE_ARRAY, dict, values);
    return dict;
}

static int
method_dictionary_at_put(st_oop dict, st_oop selector, st_oop method)
{
    uint32_t    capacity;

    if (dict == ST_NIL || !OM_is_object(dict))
        return 0;
    capacity = OM_method_dict_capacity(dict);
    uint32_t    slot;
    st_oop      values = OM_fetch_pointer(ST_MD_VALUE_ARRAY, dict);
    st_oop      tally;

    if (values == ST_NIL || !OM_is_object(values))
        return 0;

    for (slot = 0; slot < capacity; ++slot) {
        st_oop  key = OM_fetch_pointer(ST_MD_FIRST_KEY + slot, dict);

        if (key == selector || key == ST_NIL) {
            OM_store_pointer(ST_MD_FIRST_KEY + slot, dict, selector);
            OM_store_pointer(slot, values, method);
            tally = OM_fetch_pointer(ST_MD_TALLY, dict);
            if (key == ST_NIL && OM_is_int(tally))
                OM_store_pointer(ST_MD_TALLY, dict,
                                 OM_int_oop(OM_int_value(tally) + 1));
            return 1;
        }
    }
    return 0;                   /*  full; the caller grows and retries  */
}

/*  ----------  Parsing the source  ----------  */

/*  Pull the contents of the first single-quoted string out of a chunk.  */
static int
quoted_after(const char *text, const char *keyword, char *out, size_t outlen)
{
    const char *p = strstr(text, keyword);
    const char *q;
    size_t      n = 0;

    out[0] = '\0';
    if (!p)
        return 0;
    p += strlen(keyword);
    q = strchr(p, '\'');
    if (!q)
        return 0;
    ++q;
    while (*q && *q != '\'' && n + 1 < outlen)
        out[n++] = *q++;
    out[n] = '\0';
    return 1;
}

static void
split_words(const char *text, char table[][64], unsigned *count,
            unsigned limit)
{
    const char *p = text;

    *count = 0;
    while (*p && *count < limit) {
        size_t  n = 0;

        while (*p == ' ' || *p == '\t' || *p == '\n')
            ++p;
        if (!*p)
            break;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && n + 1 < 64)
            table[*count][n++] = *p++;
        table[*count][n] = '\0';
        if (n)
            ++(*count);
    }
}

/*
 *  A class definition chunk:
 *
 *      Object subclass: #Point
 *          instanceVariableNames: 'x y'
 *          classVariableNames: ''
 *          poolDictionaries: ''
 *          category: 'Kernel-Objects'
 *
 *  variableSubclass:, variableByteSubclass: and variableWordSubclass: say
 *  the instances are indexable and how.
 */
static int
parse_class_definition(const char *text)
{
    static const struct {
        const char *keyword;
        int         indexable;
        int         bytes;
        int         words;
    } forms[] = {
        { " variableByteSubclass: #", 1, 1, 0 },
        { " variableWordSubclass: #", 1, 0, 1 },
        { " variableSubclass: #",     1, 0, 0 },
        { " subclass: #",             0, 0, 0 }
    };
    unsigned    f;
    const char *at = NULL;
    unsigned    form = 0;
    boot_class *c;
    const char *p;
    size_t      n;
    char        ivars[512];

    for (f = 0; f < sizeof forms / sizeof forms[0]; ++f) {
        at = strstr(text, forms[f].keyword);
        if (at) {
            form = f;
            break;
        }
    }
    if (!at)
        return 0;

    if (class_count >= MAX_CLASSES) {
        boot_fail("more than %d classes", MAX_CLASSES);
        return 0;
    }
    c = &classes[class_count];
    memset(c, 0, sizeof *c);

    /*  The superclass name is the word before the keyword.  */
    p = at;
    while (p > text && (p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\t'))
        --p;
    {
        const char *end = p;

        while (p > text && (isalnum((unsigned char) p[-1]) || p[-1] == '_'))
            --p;
        n = (size_t) (end - p);
        if (n >= sizeof c->superclass)
            n = sizeof c->superclass - 1;
        memcpy(c->superclass, p, n);
        c->superclass[n] = '\0';
    }

    p = at + strlen(forms[form].keyword);
    n = 0;
    while (*p && (isalnum((unsigned char) *p) || *p == '_')
        && n + 1 < sizeof c->name)
        c->name[n++] = *p++;
    c->name[n] = '\0';
    if (!c->name[0])
        return 0;

    c->indexable = forms[form].indexable;
    c->bytes     = forms[form].bytes;
    c->words     = forms[form].words;

    if (quoted_after(text, "instanceVariableNames:", ivars, sizeof ivars))
        split_words(ivars, c->ivars, &c->ivar_count, MAX_IVARS);

    ++class_count;
    if (result)
        ++result->classes_created;
    return 1;
}

/*
 *  A methods-for chunk introduces a run of method chunks:
 *
 *      !Point methodsFor: 'accessing'!
 *      x
 *          ^x! !
 *
 *  "Point class methodsFor:" puts them on the metaclass instead.
 */
static int
parse_methods_for(const char *text, char *class_name, size_t namelen,
                  int *class_side)
{
    const char *at = strstr(text, "methodsFor:");
    const char *p  = text;
    size_t      n  = 0;

    *class_side = 0;
    if (!at)
        return 0;
    while (*p == ' ' || *p == '\n' || *p == '\t')
        ++p;
    while (*p && (isalnum((unsigned char) *p) || *p == '_') && n + 1 < namelen)
        class_name[n++] = *p++;
    class_name[n] = '\0';
    if (!class_name[0])
        return 0;
    while (*p == ' ')
        ++p;
    if (strncmp(p, "class", 5) == 0)
        *class_side = 1;
    return 1;
}

/*  ----------  The three passes  ----------  */

/*
 *  Pass one: allocate a class and a metaclass object for every definition,
 *  with the class field left nil.  Nothing reads it yet, and leaving it
 *  empty is what breaks the circularity.
 */
static int
allocate_class_objects(void)
{
    unsigned    i;

    for (i = 0; i < class_count; ++i) {
        boot_class *c = &classes[i];

        c->metaclass_oop = OM_instantiate_pointers(ST_NIL,
                                                   METACLASS_FIXED_FIELDS);
        if (!OM_is_object(c->metaclass_oop)) {
            boot_fail("out of memory allocating metaclass for %s", c->name);
            return 0;
        }
        OM_increase_ref(c->metaclass_oop);

        /*
         *  A class that the interpreter names by fixed pointer must land on
         *  that pointer, so it was pre-allocated and is adopted here.
         */
        {
            st_oop  reserved = reserved_class(c->name);

            if (reserved != ST_OOP_INVALID)
                c->class_oop = reserved;
            else
                c->class_oop = OM_instantiate_pointers(ST_NIL,
                                                       CLASS_FIXED_FIELDS);
        }
        if (!OM_is_object(c->class_oop)) {
            boot_fail("out of memory allocating class %s", c->name);
            return 0;
        }
        OM_increase_ref(c->class_oop);
    }
    return 1;
}

/*
 *  Pass two: fill in every field, including the class-of links that close
 *  the loops.  After this the graph is complete and consistent.
 */
static int
link_class_objects(void)
{
    unsigned    i;
    st_oop      metaclass_class;
    boot_class *metaclass_entry = find_class("Metaclass");

    if (!metaclass_entry) {
        boot_fail("the kernel must define Metaclass");
        return 0;
    }
    metaclass_class = metaclass_entry->class_oop;

    for (i = 0; i < class_count; ++i) {
        boot_class *c = &classes[i];
        boot_class *super;
        st_oop      ivar_array;
        unsigned    v;

        if (!resolve_ivars(c, 0))
            return 0;

        super = c->superclass[0] && strcmp(c->superclass, "nil") != 0
                    ? find_class(c->superclass) : NULL;

        /*  The class itself.  */
        OM_store_pointer(CLASS_SUPERCLASS, c->class_oop,
                         super ? super->class_oop : ST_NIL);
        OM_store_pointer(CLASS_FORMAT, c->class_oop,
                         make_format(c->all_ivar_count, !c->bytes && !c->words,
                                     c->indexable, c->bytes));
        OM_store_pointer(CLASS_NAME, c->class_oop,
                         BOOT_intern_symbol(c->name, NULL));
        OM_set_class_of_object(c->class_oop, c->metaclass_oop);

        ivar_array = OM_instantiate_pointers(BOOT_global("Array"),
                                             c->ivar_count);
        for (v = 0; v < c->ivar_count; ++v)
            OM_store_pointer(v, ivar_array, make_string_object(c->ivars[v]));
        OM_store_pointer(CLASS_INSTANCE_VARS, c->class_oop, ivar_array);

        /*
         *  The metaclass.  Its superclass is the superclass's metaclass, so
         *  class-side inheritance parallels the instance side; the root's
         *  metaclass inherits from Class, which is what makes "Object new"
         *  reach Behavior's methods.
         */
        OM_store_pointer(CLASS_SUPERCLASS, c->metaclass_oop,
                         super ? super->metaclass_oop
                               : BOOT_global("Class"));
        OM_store_pointer(CLASS_FORMAT, c->metaclass_oop,
                         make_format(c->class_ivar_count, 1, 0, 0));
        OM_store_pointer(METACLASS_THIS_CLASS, c->metaclass_oop, c->class_oop);
        OM_set_class_of_object(c->metaclass_oop, metaclass_class);

        define_global(c->name, c->class_oop);
    }
    return 1;
}

/*  ----------  Compiling  ----------  */

static int
compile_into(boot_class *c, int class_side, const char *source,
             const char *file, unsigned line)
{
    st_compile_context  ctx;
    st_compile_result   res;
    const char         *ivar_pointers[MAX_IVARS];
    unsigned            i;
    st_oop              target = class_side ? c->metaclass_oop : c->class_oop;
    st_oop              dict;
    st_oop              selector;

    memset(&ctx, 0, sizeof ctx);
    for (i = 0; i < c->all_ivar_count; ++i)
        ivar_pointers[i] = c->all_ivars[i];
    ctx.instance_variables      = class_side ? NULL : ivar_pointers;
    ctx.instance_variable_count = class_side ? 0 : c->all_ivar_count;
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_array         = BOOT_make_array;
    ctx.lookup_global      = BOOT_lookup_global;

    if (COMPILE_method(source, &ctx, &res) != 0) {
        boot_fail("%s:%u: in %s%s: %s", file, line + res.error_line,
                  c->name, class_side ? " class" : "", res.error);
        if (result) {
            snprintf(result->error_file, sizeof result->error_file, "%s", file);
            result->error_line = line + res.error_line;
        }
        return 0;
    }

    dict = OM_fetch_pointer(CLASS_METHOD_DICT, target);
    /*
     *  nil is a live object, so testing with OM_is_object alone would take
     *  an empty slot for a dictionary and index straight off the end of it.
     */
    if (dict == ST_NIL || !OM_is_object(dict)) {
        dict = make_method_dictionary(8);
        OM_store_pointer(CLASS_METHOD_DICT, target, dict);
    }
    selector = BOOT_intern_symbol(res.selector, NULL);
    if (!method_dictionary_at_put(dict, selector, res.method)) {
        /*  Grow: a new dictionary of twice the size, rehashed by copying.  */
        uint32_t    old_capacity = OM_method_dict_capacity(dict);
        st_oop      grown = make_method_dictionary(old_capacity * 2);
        uint32_t    slot;

        for (slot = 0; slot < old_capacity; ++slot) {
            st_oop  key = OM_method_dict_key(dict, slot);

            if (key != ST_NIL)
                method_dictionary_at_put(grown, key,
                                         OM_method_dict_value(dict, slot));
        }
        method_dictionary_at_put(grown, selector, res.method);
        OM_store_pointer(CLASS_METHOD_DICT, target, grown);
    }
    if (result)
        ++result->methods_compiled;
    return 1;
}

/*  ----------  Reading a source file  ----------  */

static int
read_source(const char *path, int definitions_only)
{
    st_chunk_reader    *reader;
    st_chunk            chunk;
    char                err[256];
    char                class_name[64];
    int                 class_side = 0;
    boot_class         *current = NULL;
    int                 in_methods = 0;

    reader = CHUNK_open(path, err, sizeof err);
    if (!reader) {
        boot_fail("%s", err);
        return 0;
    }
    while (CHUNK_next(reader, &chunk)) {
        if (chunk.is_empty) {
            in_methods = 0;
            current    = NULL;
            continue;
        }
        if (chunk.is_reader) {
            if (parse_methods_for(chunk.text, class_name, sizeof class_name,
                                  &class_side)) {
                current    = find_class(class_name);
                in_methods = 1;
                if (!current && !definitions_only) {
                    boot_fail("%s:%u: methods for unknown class %s", path,
                              CHUNK_line(reader), class_name);
                    CHUNK_close(reader);
                    return 0;
                }
            }
            continue;
        }
        if (in_methods) {
            if (!definitions_only && current
             && !compile_into(current, class_side, chunk.text, path,
                              CHUNK_line(reader))) {
                CHUNK_close(reader);
                return 0;
            }
            continue;
        }
        if (definitions_only)
            parse_class_definition(chunk.text);
    }
    CHUNK_close(reader);
    return 1;
}

/*  ----------  The fixed pointers  ----------  */

/*
 *  These must land on exactly the pointers the interpreter names, so they
 *  are created first and in order.  The table is the Blue Book's list of
 *  guaranteed pointers; anything not yet meaningful is a placeholder that
 *  later passes overwrite.
 */
typedef enum {
    FIX_INSTANCE,       /*  a plain object with no fields  */
    FIX_CLASS,          /*  a class object, filled in later  */
    FIX_SYMBOL,
    FIX_ARRAY,
    FIX_ASSOCIATION,
    FIX_SYSTEM_DICT
} fix_kind;

static int
allocate_fixed_objects(void)
{
    static const struct {
        st_oop      expected;
        fix_kind    kind;
        const char *text;
        unsigned    size;
    } fixed[] = {
        { ST_NIL,                          FIX_INSTANCE,    "nil",    0 },
        { ST_FALSE,                        FIX_INSTANCE,    "false",  0 },
        { ST_TRUE,                         FIX_INSTANCE,    "true",   0 },
        { ST_SCHEDULER_ASSOCIATION,        FIX_ASSOCIATION, NULL,     2 },
        { 10,                              FIX_INSTANCE,    NULL,     0 },
        { ST_CLASS_SMALL_INTEGER,          FIX_CLASS, "SmallInteger", 0 },
        { ST_CLASS_STRING,                 FIX_CLASS, "String",       0 },
        { ST_CLASS_ARRAY,                  FIX_CLASS, "Array",        0 },
        { ST_SMALLTALK,                    FIX_SYSTEM_DICT, NULL,     1 },
        { ST_CLASS_FLOAT,                  FIX_CLASS, "Float",        0 },
        { ST_CLASS_METHOD_CONTEXT,         FIX_CLASS, "MethodContext",0 },
        { ST_CLASS_BLOCK_CONTEXT,          FIX_CLASS, "BlockContext", 0 },
        { ST_CLASS_POINT,                  FIX_CLASS, "Point",        0 },
        { ST_CLASS_LARGE_POSITIVE_INTEGER, FIX_CLASS,
          "LargePositiveInteger", 0 },
        { ST_CLASS_DISPLAY_BITMAP,         FIX_CLASS, "DisplayBitmap",0 },
        { ST_CLASS_MESSAGE,                FIX_CLASS, "Message",      0 },
        { ST_CLASS_COMPILED_METHOD,        FIX_CLASS, "CompiledMethod",0 },
        { ST_THE_INTERPRETER,              FIX_INSTANCE,    NULL,     0 },
        { ST_CLASS_SEMAPHORE,              FIX_CLASS, "Semaphore",    0 },
        { ST_CLASS_CHARACTER,              FIX_CLASS, "Character",    0 },
        { ST_SELECTOR_DOES_NOT_UNDERSTAND, FIX_SYMBOL,
          "doesNotUnderstand:", 0 },
        { ST_SELECTOR_CANNOT_RETURN,       FIX_SYMBOL, "cannotReturn:", 0 },
        { ST_PROCESS_SIGNALING_LOW_SPACE,  FIX_INSTANCE,    NULL,     0 },
        { ST_SPECIAL_SELECTORS,            FIX_ARRAY,       NULL,    64 },
        { ST_CHARACTER_TABLE,              FIX_ARRAY,       NULL,   256 },
        { ST_SELECTOR_MUST_BE_BOOLEAN,     FIX_SYMBOL, "mustBeBoolean", 0 },
        { ST_CLASS_DISPLAY_SCREEN,         FIX_CLASS, "DisplayScreen", 0 },
        { ST_SELECTOR_CANNOT_INTERPRET,    FIX_SYMBOL, "cannotInterpret", 0 }
    };
    unsigned    i;

    for (i = 0; i < sizeof fixed / sizeof fixed[0]; ++i) {
        st_oop  p = ST_OOP_INVALID;

        switch (fixed[i].kind) {
        case FIX_INSTANCE:
            p = OM_instantiate_pointers(ST_NIL, 0);
            break;
        case FIX_CLASS:
            p = OM_instantiate_pointers(ST_NIL, CLASS_FIXED_FIELDS);
            break;
        case FIX_SYMBOL:
        case FIX_SYSTEM_DICT:
        case FIX_ASSOCIATION:
        case FIX_ARRAY:
            /*
             *  Symbols are byte objects and the rest are pointer objects,
             *  but all of them are filled in later; what matters here is
             *  only that each takes the next index.
             */
            p = OM_instantiate_pointers(ST_NIL,
                    fixed[i].kind == FIX_SYMBOL ? 0 : fixed[i].size);
            break;
        }
        if (fixed[i].kind == FIX_CLASS && fixed[i].text
         && reserved_class_count < 32) {
            reserved_classes[reserved_class_count].name = fixed[i].text;
            reserved_classes[reserved_class_count].oop  = p;
            ++reserved_class_count;
        }
        if (p != fixed[i].expected) {
            boot_fail("fixed object %u landed on pointer %llu, expected %llu"
                      " -- the table is out of step with the interpreter",
                      i, (unsigned long long) p,
                      (unsigned long long) fixed[i].expected);
            return 0;
        }
        OM_increase_ref(p);
    }
    return 1;
}

/*
 *  Once the classes exist, the fixed objects that are instances get their
 *  classes, and the ones that are placeholders get their real contents.
 */
static int
finish_fixed_objects(void)
{
    static const struct { st_oop oop; const char *class_name; } instances[] = {
        { ST_NIL,   "UndefinedObject" },
        { ST_FALSE, "False" },
        { ST_TRUE,  "True" }
    };
    unsigned    i;
    st_oop      character_class = BOOT_global("Character");

    for (i = 0; i < sizeof instances / sizeof instances[0]; ++i) {
        st_oop  cls = BOOT_global(instances[i].class_name);

        if (!OM_is_object(cls)) {
            boot_fail("the kernel must define %s", instances[i].class_name);
            return 0;
        }
        OM_set_class_of_object(instances[i].oop, cls);
    }

    /*
     *  The character table.  Characters are unique per code point, which is
     *  what makes "$a == $a" answer true, so they are made once here.
     */
    if (OM_is_object(character_class)) {
        OM_set_class_of_object(ST_CHARACTER_TABLE, BOOT_global("Array"));
        for (i = 0; i < 256; ++i) {
            st_oop  ch = OM_instantiate_pointers(character_class, 1);

            OM_store_pointer(0, ch, OM_int_oop((st_int) i));
            OM_store_pointer(i, ST_CHARACTER_TABLE, ch);
        }
    }

    /*
     *  The special selectors array: thirty-two selector and argument-count
     *  pairs, sixty-four elements in all.  The interpreter reads it when an
     *  arithmetic or special-selector bytecode cannot be answered by a
     *  primitive and has to become a real send -- bytecode 204, "new",
     *  indexes element 56, so an array of thirty-two runs off the end and
     *  the VM executes whatever follows as a method.
     */
    {
        static const struct { const char *selector; int argc; } special[32] = {
            /*  176..191, the arithmetic group: all binary.  */
            { "+", 1 }, { "-", 1 }, { "<", 1 }, { ">", 1 },
            { "<=", 1 }, { ">=", 1 }, { "=", 1 }, { "~=", 1 },
            { "*", 1 }, { "/", 1 }, { "\\\\", 1 }, { "@", 1 },
            { "bitShift:", 1 }, { "//", 1 }, { "bitAnd:", 1 }, { "bitOr:", 1 },
            /*  192..207, the special group, whose arities vary.  */
            { "at:", 1 }, { "at:put:", 2 }, { "size", 0 }, { "next", 0 },
            { "nextPut:", 1 }, { "atEnd", 0 }, { "==", 1 }, { "class", 0 },
            { "blockCopy:", 1 }, { "value", 0 }, { "value:", 1 },
            { "do:", 1 }, { "new", 0 }, { "new:", 1 }, { "x", 0 }, { "y", 0 }
        };

        OM_set_class_of_object(ST_SPECIAL_SELECTORS, BOOT_global("Array"));
        for (i = 0; i < 32; ++i) {
            OM_store_pointer(i * 2, ST_SPECIAL_SELECTORS,
                             BOOT_intern_symbol(special[i].selector, NULL));
            OM_store_pointer(i * 2 + 1, ST_SPECIAL_SELECTORS,
                             OM_int_oop(special[i].argc));
        }
    }

    OM_set_class_of_object(ST_SMALLTALK, BOOT_global("SystemDictionary"));

    /*
     *  The globals that are not classes.  CharacterTable in particular has
     *  to be reachable by name, because that is how Character class>>value:
     *  finds the unique instance for a code point -- which is what makes
     *  "$a == $a" answer true.
     */
    define_global("Smalltalk",      ST_SMALLTALK);
    define_global("CharacterTable", ST_CHARACTER_TABLE);
    define_global("Display",        ST_NIL);
    define_global("Processor",      ST_NIL);
    return 1;
}

/*  ----------  Driver  ----------  */

int
BOOT_build(const char *const *paths, unsigned path_count,
           st_bootstrap_result *out)
{
    unsigned    i;

    result = out;
    memset(out, 0, sizeof *out);
    class_count  = 0;
    symbol_count = 0;
    reserved_class_count = 0;
    global_count = 0;

    if (OM_init() != 0) {
        boot_fail("cannot initialize the object memory");
        return -1;
    }
    if (!allocate_fixed_objects())
        return -1;

    smalltalk      = ST_SMALLTALK;
    globals_values = OM_instantiate_pointers(ST_NIL, MAX_GLOBALS);
    OM_increase_ref(globals_values);
    /*
     *  Published immediately, so that a lookup during the bootstrap finds
     *  what has been defined so far and a reload finds all of it.
     */
    OM_store_pointer(0, ST_SMALLTALK, globals_values);

    /*  Pass zero: read every definition, so forward references resolve.  */
    for (i = 0; i < path_count; ++i) {
        if (!read_source(paths[i], 1))
            return -1;
    }
    if (class_count == 0) {
        boot_fail("no class definitions found");
        return -1;
    }
    if (getenv("ST_BOOT_LOG")) {
        unsigned    k;

        fprintf(stderr, "  %u class definitions:\n", class_count);
        for (k = 0; k < class_count; ++k)
            fprintf(stderr, "    %-24s super=%-20s ivars=%u%s\n",
                    classes[k].name, classes[k].superclass,
                    classes[k].ivar_count,
                    classes[k].indexable
                        ? (classes[k].bytes ? " bytes"
                            : (classes[k].words ? " words" : " pointers"))
                        : "");
    }
    if (!allocate_class_objects())
        return -1;
    if (!link_class_objects())
        return -1;
    if (!finish_fixed_objects())
        return -1;

    if (getenv("ST_BOOT_LOG")) {
        unsigned    k;

        fprintf(stderr, "  %u globals defined\n", global_count);
        for (k = 0; k < global_count && k < 8; ++k) {
            char    text[64];

            OM_string_of(OM_fetch_pointer(ST_ASSOCIATION_KEY,
                             OM_fetch_pointer(k, globals_values)),
                         text, sizeof text);
            fprintf(stderr, "    [%u] '%s'\n", k, text);
        }
        fprintf(stderr, "  lookup Character -> %llu\n",
                (unsigned long long) global_association("Character"));
    }

    /*  Pass two: compile, now that every name resolves.  */
    for (i = 0; i < path_count; ++i) {
        if (!read_source(paths[i], 0))
            return -1;
    }

    return 0;
}

uint32_t
BOOT_method_initial_ip(st_oop method)
{
    /*  Header word plus the literal frame, in bytes.  */
    st_oop  header = OM_fetch_pointer(0, method);

    return (uint32_t) ((ST_header_literal_count(header) + 1)
                       * sizeof(st_oop));
}

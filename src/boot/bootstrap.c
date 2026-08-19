/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The image bootstrap.  See bootstrap.h for the shape of the problem.
 */

#include "bootstrap.h"
#include "chunk.h"
#include "source.h"
#include "profile.h"
#include "compiler.h"
#include "interp.h"
#include "census.h"
#include "gfx.h"
#include "font.h"
#include "st_sched.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_IVARS       256
/*
 *  Initial sizes, not ceilings.  Every table below this line grows; the
 *  names say "first guess" rather than "limit" because the difference is
 *  what stops a Pharo-scale library hitting a wall the 1983 one never
 *  approached.
 */
#define SYMBOLS_FIRST   8192

/*  ----------  Names  ----------  */

/*
 *  A growable list of names.
 *
 *  This used to be char[64][64] inline in boot_class, six times over, which
 *  made one class 25 KB and the fixed table of 512 of them twelve megabytes
 *  of BSS.  That is survivable for a 226-class library and is not survivable
 *  for a Pharo-scale one: the same shape at 8192 classes is two hundred
 *  megabytes, all of it resident, nearly all of it empty.
 *
 *  Separately allocated strings rather than one arena of offsets, because an
 *  arena has to be reallocated as it grows and reallocation moves it -- and
 *  the whole point of `items` is that it can be handed to the compiler as
 *  `const char *const *` with no copy.  A pointer that moves under the
 *  compiler would be a fine way to spend a week.
 */
/*  Shared with the source readers; see src/compiler/source.h.  */
typedef st_names name_list;

#define name_list_add   SRC_names_add
#define name_list_free  SRC_names_free

/*  ----------  Bootstrap state  ----------  */

typedef struct {
    char        name[64];
    char        superclass[64];
    name_list   ivars;
    name_list   class_ivars;
    int         indexable;
    int         bytes;              /*  byte-indexable rather than pointer  */
    int         words;
    int         weak;               /*  indexed fields are weak  */
    int         ephemeron;          /*  first field is a weakly held key  */

    st_oop      class_oop;
    st_oop      metaclass_oop;

    /*
     *  Class variables.  Shared by the class, its metaclass and every
     *  subclass of both, so they are looked up by walking the superclass
     *  chain.  Each is an Association, because that is what a compiled
     *  method's literal frame holds.
     */
    /*  nil -> the metaclass, for super sends in class methods.  */
    st_oop      metaclass_association;
    int         reserved_pointer;

    char        category[64];

    /*  Pool dictionaries this class shares, by name.  */
    name_list   pools;

    /*  Class variables, and the Association each one is bound to.  */
    name_list   cvars;
    st_oop     *cvar_assoc;             /*  cvars.capacity entries  */

    /*
     *  Whether a package format defined this class, rather than the 1983
     *  chunk files.  It decides one thing: whether the loader may
     *  synthesize "new ^super new initialize" for it.  The 1983 classes
     *  that want that idiom already write it out by hand, and adding a
     *  second one would run initialize twice.
     */
    int         from_package;

    /*
     *  The trait composition this class declared, verbatim -- "TA + TB".
     *  Empty when it declared none.  Kept as written so a report can quote
     *  it, and applied only after every method has been compiled.
     */
    /*
     *  1024, not 256.  Pharo's collection tests compose a dozen traits at a
     *  time -- BagTest's #traits is 329 characters and its #classTraits is
     *  471 -- and a composition that is silently truncated does not fail
     *  loudly, it fails as "a #classTraits of its own is not supported",
     *  which sends you looking at the wrong thing entirely.
     */
    char        traits[1024];

    /*  Instance variables including every inherited one, in frame order.  */
    name_list   all_ivars;
    /*  The same for the metaclass side, which has its own parallel chain.  */
    name_list   all_class_ivars;
    int         resolved;
} boot_class;

/*
 *  ----------  Traits  ----------
 *
 *  A trait is a named bag of methods with no instances and no place in the
 *  superclass chain, and it is applied by FLATTENING: every method it
 *  provides is compiled into each class that names it.
 *
 *  Flattening copies SOURCE and compiles it once per using class, rather
 *  than compiling once and copying the CompiledMethod.  That is not an
 *  implementation detail: a Blue Book method names an instance variable by
 *  its index in the frame and holds its own class binding in the literal
 *  frame, so the same method object in two classes would read whichever
 *  field happened to sit at that index.  Recompiling per class is what makes
 *  a trait method mean the same thing in every class that takes it.
 *
 *  Only "+" is implemented.  Exclusion ("-") and aliasing ("@") are refused
 *  by name, with the expression quoted, because a composition silently
 *  losing its exclusion would load and then behave differently.
 */
typedef struct {
    char       *source;
    char       *file;
    unsigned    line;
    char        category[64];
    int         class_side;
    int         dialect;
} trait_method;

typedef struct {
    char            name[64];
    char            composition[1024];  /*  traits this trait itself uses  */
    trait_method   *methods;
    unsigned        method_count;
    unsigned        method_capacity;
} boot_trait;

static boot_trait  *traits;
static unsigned     trait_count;
static unsigned     trait_capacity;

/*
 *  Classes whose SHAPE this system cannot build, remembered by name.
 *
 *  Refusing the definition was only half of it.  The methods arrive in the
 *  next pass and asked for a class that was never made, which came out as
 *  "methods for unknown class" and stopped the build -- so "reject loudly
 *  and keep going" rejected loudly and then did not keep going.  The first
 *  real Pharo package found it on its first load: WeakKeyAssociation is an
 *  ephemeron, which is refused by name, and its four methods then killed
 *  the bootstrap.
 */
static st_names     refused_classes;

static int
class_was_refused(const char *name)
{
    unsigned    i;

    for (i = 0; i < refused_classes.count; ++i) {
        if (strcmp(refused_classes.items[i], name) == 0)
            return 1;
    }
    return 0;
}

/*
 *  The class table, grown on demand rather than reserved.  There is no
 *  MAX_CLASSES any more; the ceiling is memory.
 */
static boot_class  *classes;
static unsigned     class_count;
static unsigned     class_capacity;

/*
 *  Add a class variable and its (initially unbound) Association slot.  The
 *  two arrays must grow together or the binding belongs to the wrong name.
 */
static int
add_cvar(boot_class *c, const char *name)
{
    unsigned    was = c->cvars.capacity;

    if (!name_list_add(&c->cvars, name))
        return 0;
    if (c->cvars.capacity != was) {
        st_oop *grown = (st_oop *) realloc(c->cvar_assoc,
                                           c->cvars.capacity * sizeof *grown);

        if (!grown)
            return 0;
        c->cvar_assoc = grown;
    }
    c->cvar_assoc[c->cvars.count - 1] = 0;
    return 1;
}

static boot_class *
new_class_entry(void)
{
    if (class_count == class_capacity) {
        unsigned    want = class_capacity ? class_capacity * 2 : 256;
        boot_class *grown = (boot_class *) realloc(classes,
                                                   want * sizeof *grown);

        if (!grown)
            return NULL;
        memset(grown + class_capacity, 0,
               (want - class_capacity) * sizeof *grown);
        classes        = grown;
        class_capacity = want;
    }
    return &classes[class_count++];
}

/*
 *  Whether the library's symbol table exists yet.  Until it does there is
 *  nowhere to put a symbol; afterwards every symbol interned has to go in,
 *  or "#foo == 'foo' asSymbol" answers false -- the compiler would hold one
 *  Symbol and the image would make itself another.
 */
static int          symbol_table_ready;
static st_oop       symbol_table;

/*
 *  Every Symbol this side has made, and an index over them.
 *
 *  The array alone was scanned linearly on every intern, comparing the text
 *  against each Symbol's own bytes.  For a 3,549-symbol library that is a
 *  few million comparisons and unnoticeable; for the forty thousand a Pharo
 *  library interns it is most of a billion, because the cost is quadratic in
 *  a number that is about to grow by an order of magnitude.
 *
 *  So there is an open-addressed index beside it, holding indices rather
 *  than pointers so that growing the array does not invalidate it.  The
 *  index is a lookup aid and nothing else: the answer always comes from the
 *  Symbol's own bytes, which is the rule the linear scan was already
 *  following and the reason it was correct where a cached copy of the text
 *  had not been.
 */
static st_oop      *symbols;
static unsigned     symbol_count;
static unsigned     symbol_capacity;

static uint32_t    *symbol_index;       /*  slot -> position + 1, 0 empty  */
static unsigned     symbol_index_size;  /*  a power of two                 */

static uint32_t BOOT_string_hash_of_text(const char *text, size_t length);

/*  Does the Symbol at `position` spell `text`?  */
static int
symbol_is(unsigned position, const char *text, size_t n)
{
    size_t  k;

    if (OM_fetch_byte_length(symbols[position]) != n)
        return 0;
    for (k = 0; k < n; ++k) {
        if (OM_fetch_byte((uint32_t) k, symbols[position]) != (uint8_t) text[k])
            return 0;
    }
    return 1;
}

static void symbol_index_insert(unsigned position);

static int
symbol_index_rebuild(unsigned want)
{
    unsigned    i;

    free(symbol_index);
    symbol_index_size = want;
    symbol_index = (uint32_t *) calloc(want, sizeof *symbol_index);
    if (!symbol_index) {
        symbol_index_size = 0;
        return 0;
    }
    for (i = 0; i < symbol_count; ++i)
        symbol_index_insert(i);
    return 1;
}

static void
symbol_index_insert(unsigned position)
{
    uint32_t    n = (uint32_t) OM_fetch_byte_length(symbols[position]);
    char        text[512];
    uint32_t    slot;
    uint32_t    i;

    if (n >= sizeof text)
        return;                     /*  found by the scan below instead  */
    for (i = 0; i < n; ++i)
        text[i] = (char) OM_fetch_byte(i, symbols[position]);
    slot = BOOT_string_hash_of_text(text, n) & (symbol_index_size - 1);
    while (symbol_index[slot] != 0)
        slot = (slot + 1) & (symbol_index_size - 1);
    symbol_index[slot] = position + 1;
}

/*  The position of the Symbol spelling `text`, or -1.  */
static long
symbol_find(const char *text, size_t n)
{
    uint32_t    slot;

    if (symbol_index_size == 0 || n >= 512) {
        unsigned    i;              /*  the long ones, and before the index */

        for (i = 0; i < symbol_count; ++i) {
            if (symbol_is(i, text, n))
                return (long) i;
        }
        return -1;
    }
    slot = BOOT_string_hash_of_text(text, n) & (symbol_index_size - 1);
    while (symbol_index[slot] != 0) {
        unsigned    position = symbol_index[slot] - 1;

        if (symbol_is(position, text, n))
            return (long) position;
        slot = (slot + 1) & (symbol_index_size - 1);
    }
    return -1;
}

/*  Remember a Symbol, growing both the array and the index as needed.  */
static int
symbol_remember(st_oop s)
{
    if (symbol_count == symbol_capacity) {
        unsigned    want = symbol_capacity ? symbol_capacity * 2 : SYMBOLS_FIRST;
        st_oop     *grown = (st_oop *) realloc(symbols, want * sizeof *grown);

        if (!grown)
            return 0;
        symbols         = grown;
        symbol_capacity = want;
    }
    symbols[symbol_count++] = s;
    /*  Keep the index under half full, which is what keeps probes short.  */
    if (symbol_count * 2 >= symbol_index_size) {
        unsigned    want = symbol_index_size ? symbol_index_size * 2 : 8192;

        if (!symbol_index_rebuild(want))
            return 1;               /*  the scan still answers correctly  */
    }  else  {
        symbol_index_insert(symbol_count - 1);
    }
    return 1;
}

static st_oop       smalltalk;          /*  the SystemDictionary  */
static st_oop       globals_values;
static unsigned     global_count;
#define GLOBALS_FIRST 1024

/*  How many buckets the library's symbol table has.  */
#define USTABLE_BUCKETS 512

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

/*
 *  Something the build could not act on, which is not a reason to stop.
 *
 *  A porting effort needs the whole list rather than the first item, so
 *  these go to stderr as they happen and are counted; boot_fail is for the
 *  things that end the build.
 */
static void
boot_note(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "st80: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
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

static int place_in_symbol_table(st_oop sym);
static int remember_source(const char *text, unsigned *file_index,
                           size_t *position);
static void install_closure_support(void);

/*
 *  Which language the file being read is written in.
 *
 *  A property of the package rather than of the system: sources/ is Blue
 *  Book and always will be, lib/ is closures, and one image is made of
 *  both.  The profile says which is which; this is where it lands.
 */
static const int   *path_dialects;
static int          current_dialect;
uint32_t BOOT_string_hash_of_text(const char *text, size_t length);
static int adopt_symbols(void);
static int adopt_associations(void);

st_oop
BOOT_intern_symbol(const char *text, void *user)
{
    unsigned    i;
    st_oop      s;
    size_t      n = strlen(text);

    (void) user;
    /*
     *  Compare against the Symbol's own bytes, not a copy.
     *
     *  This kept a parallel C array of the text, 64 bytes per entry, and
     *  compared against that -- so a selector longer than 63 characters never
     *  matched itself, and every mention of it made a NEW Symbol.  The method
     *  was then installed under one Symbol and sent with another, and a
     *  dictionary keyed by identity could not find it.  Fourteen selectors in
     *  the 1983 library are that long, including
     *  subclass:instanceVariableNames:classVariableNames:poolDictionaries:category:
     *  and the whole of BitBlt's setDestForm:...clipRect:, so neither
     *  defining a class from inside the image nor drawing anything worked.
     *
     *  The copy is gone rather than widened.  A second representation of the
     *  same text is a thing to keep in step, and this one had already
     *  drifted; the Symbol itself cannot.
     */
    {
        long    found = symbol_find(text, n);

        if (found >= 0)
            return symbols[found];
    }
    /*
     *  Then the library's own table, if it exists yet.
     *
     *  The scan above only sees symbols this side made.  Once the image is
     *  running it interns symbols too -- classify:under: sends asSymbol to
     *  every category name -- and those never reach the C table.  Creating
     *  another Symbol with the same characters gives two, and Symbol>>= is
     *  identity, so they are not even equal: a category the image stored
     *  could not be found by a name the compiler read.
     */
    if (symbol_table_ready && OM_is_present(symbol_table)) {
        uint32_t    index = BOOT_string_hash_of_text(text, n) % USTABLE_BUCKETS;
        st_oop      bucket = OM_fetch_pointer(index, symbol_table);
        uint32_t    count = OM_is_present(bucket)
                                ? OM_fetch_word_length(bucket) : 0;
        uint32_t    b;

        for (b = 0; b < count; ++b) {
            st_oop      candidate = OM_fetch_pointer(b, bucket);
            size_t      k;

            if (!OM_is_present(candidate)
             || OM_fetch_byte_length(candidate) != n)
                continue;
            for (k = 0; k < n; ++k) {
                if (OM_fetch_byte((uint32_t) k, candidate)
                        != (uint8_t) text[k])
                    break;
            }
            if (k == n) {
                /*  Remember it so the next lookup is the quick one.  */
                symbol_remember(candidate);
                return candidate;
            }
        }
    }

    s = OM_instantiate_bytes(BOOT_global("Symbol"), (uint32_t) n);
    if (!OM_is_object(s))
        return ST_NIL;
    for (i = 0; i < n; ++i)
        OM_store_byte(i, s, (uint8_t) text[i]);
    if (symbol_remember(s) && result)
        ++result->symbols_interned;
    /*  Symbols are permanent; nothing else keeps the table alive.  */
    OM_increase_ref(s);
    /*
     *  Once the library has a symbol table, a symbol that is not in it is a
     *  second Symbol with the same characters waiting to happen.
     */
    place_in_symbol_table(s);
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
    /*
     *  A literal Float must be the same shape the interpreter computes, and
     *  in this memory that is IEEE 754 DOUBLE precision: four 16-bit words,
     *  most significant first.  Chapter 30 specifies single, and the Blue
     *  Book build still makes single for that reason -- it loads Xerox's own
     *  image and answers trace2 byte for byte.  This bootstrap builds its own
     *  image and is under no such obligation; see make_float in prim.c for
     *  what twenty-four bits of mantissa was costing the date arithmetic.

     *  What has NOT changed is that the two must agree.  This used to store
     *  the host's double in native word order, which was wrong twice over:
     *  the size disagreed, so a literal and a computed value of the same
     *  number were different shapes; and the order disagreed with the
     *  reader, which takes the words most significant first.  The visible
     *  effect was that 3.5 exponent answered -1060.
     */
    union { double d; uint64_t u; } bits;
    st_oop  p = OM_instantiate_words(BOOT_global("Float"), 4);
    int     i;

    (void) user;
    if (!OM_is_object(p))
        return ST_NIL;
    bits.d = value;
    for (i = 0; i < 4; ++i)
        OM_store_word((uint32_t) i, p,
                      (uint16_t) ((bits.u >> (16 * (3 - i))) & 0xFFFF));
    return p;
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

/*
 *  A literal ByteArray, for #[1 2 3].
 *
 *  ByteArray is an ordinary class in the 1983 library rather than one the
 *  interpreter names, so this looks it up like any other global; a profile
 *  that has not loaded it gets nil and a compile-time complaint rather than
 *  a byte array full of nothing.
 */
st_oop
BOOT_make_byte_array(const uint8_t *bytes, unsigned count, void *user)
{
    st_oop      class_oop = BOOT_global("ByteArray");
    st_oop      array;
    unsigned    i;

    (void) user;
    if (!OM_is_object(class_oop))
        return ST_NIL;
    array = OM_instantiate_bytes(class_oop, count);
    if (!OM_is_object(array))
        return ST_NIL;
    for (i = 0; i < count; ++i)
        OM_store_byte(i, array, bytes[i]);
    return array;
}

/*
 *  An AdditionalMethodState holding a method's pragmas.
 *
 *  Answers nil when the profile has not loaded the class, and the compiler
 *  then leaves the method exactly as it was -- which is the Blue Book case
 *  and keeps its image unchanged.
 */
st_oop
BOOT_make_method_state(st_oop pragmas, void *user)
{
    st_oop  class_oop = BOOT_global("AdditionalMethodState");
    st_oop  state;

    (void) user;
    if (!OM_is_object(class_oop))
        return ST_NIL;
    state = OM_instantiate_pointers(class_oop, 1);
    if (!OM_is_object(state))
        return ST_NIL;
    OM_store_pointer(0, state, pragmas);
    return state;
}

/*  Characters are unique per code point, which is what makes == work.  */
st_oop
BOOT_make_character(unsigned code, void *user)
{
    (void) user;
    return OM_fetch_pointer(code, ST_CHARACTER_TABLE);
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

    /*
     *  Read the bootstrap's own array, not field 0 of Smalltalk.
     *
     *  Smalltalk starts as a placeholder whose first field is this array, and
     *  it is tempting to go through it -- but once install_system_dictionary
     *  turns Smalltalk into a real SystemDictionary, field 0 is the tally and
     *  every global lookup silently answers nothing.  The two views share the
     *  same Association objects, so either sees the other's changes; only the
     *  route to them differs.
     */
    associations = globals_values;
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
    /*
     *  Grow the binding array when it fills.
     *
     *  It starts at GLOBALS_FIRST and doubles, rather than starting small and
     *  growing, because its size is visible: it is an object IN the image,
     *  and a Blue Book build must produce the same image it always has.  The
     *  1983 library needs about three hundred bindings, so it never grows at
     *  all and nothing moves; a Pharo-scale one grows and the image is a
     *  different image anyway.
     */
    if (global_count >= OM_fetch_word_length(globals_values)) {
        uint32_t    have = OM_fetch_word_length(globals_values);
        st_oop      grown = OM_instantiate_pointers(ST_NIL, have * 2);
        uint32_t    i;

        if (!OM_is_object(grown)) {
            boot_fail("out of memory for global number %u", global_count + 1);
            return ST_NIL;
        }
        for (i = 0; i < have; ++i)
            OM_store_pointer(i, grown, OM_fetch_pointer(i, globals_values));
        OM_increase_ref(grown);
        /*
         *  Republish through Smalltalk while it is still the placeholder
         *  whose field 0 is this array.  Once install_system_dictionary has
         *  swapped in a real SystemDictionary that field means something
         *  else, and this array is the bootstrap's private view.
         */
        if (OM_is_present(smalltalk)
         && OM_fetch_pointer(0, smalltalk) == globals_values)
            OM_store_pointer(0, smalltalk, grown);
        OM_decrease_ref(globals_values);
        globals_values = grown;
    }
    key         = BOOT_intern_symbol(name, NULL);
    /*
     *  Association itself may not exist yet during the earliest steps, so a
     *  binding made then carries a nil class, and adopt_associations gives
     *  it one once there is one to give.
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

/*
 *  Names the library used before anything defined them.  A real Smalltalk
 *  fileIn does the same thing: an unknown capitalised identifier becomes a
 *  binding in Undeclared, so the method compiles and the reference is live
 *  the moment something assigns to the name.  Without it the 1983 library
 *  cannot load at all -- Sensor, Display, Transcript and Processor are all
 *  built when an image is made, long after the code that uses them.
 *
 *  They are counted and can be listed, because "undeclared" is a fact worth
 *  seeing.  A typo becomes a global here exactly as it would in 1983.
 */
static char         undeclared_names[256][64];
static unsigned     undeclared_count;
static unsigned     undeclared_lowercase;

/*
 *  Bindings that belong in a pool dictionary.
 *
 *  A pool's names are referenced by methods compiled long before anything
 *  puts values in the pool -- TextConstants is filled by Text class>>
 *  initialize, which cannot run until every class exists.  So the binding is
 *  created when the name is first seen and the SAME Association is put into
 *  the pool afterwards.  Dictionary>>at:put: updates an existing element's
 *  value in place rather than replacing it, so when the initializer finally
 *  runs, the methods are already pointing at the binding it sets.
 *
 *  Creating a second Association instead -- which is what happens if the
 *  pool is simply left empty -- leaves every compiled reference reading nil
 *  for ever, and nothing says so.
 */
static struct {
    st_oop  association;
    char    pool[64];
} pool_bindings[512];
static unsigned     pool_binding_count;

/*
 *  Which protocol each method belongs to.
 *
 *  The Browser's third pane is the protocol list, and it comes from the
 *  class's organization -- an object every class has and none of ours had.
 *  The information is in the sources, in the "methodsFor:" that introduces
 *  each run of methods, and it went past unread until now.
 */
static struct {
    unsigned    class_index;
    int         class_side;
    st_oop      selector;
    char        protocol[64];
} *method_protocols;
static unsigned     method_protocol_count;
static unsigned     method_protocol_capacity;

/*
 *  Every method's source, in one buffer, in chunk format.
 *
 *  Smalltalk-80 does not keep source in the image: a CompiledMethod carries
 *  a position into a sources file, and the Browser reads the chunk there.
 *  Nothing says the stream has to be a file -- RemoteString asks it only to
 *  position: and nextChunk, which any PositionableStream does -- so the
 *  sources live in a String and the image reads them the ordinary way.
 */
/*
 *  Where method source is kept, and why there is more than one of it.
 *
 *  Chapter 27 puts a method's source location in the last three bytes of
 *  the CompiledMethod: twenty-two bits of position and, above them, two
 *  bits naming one of four files.  Convention gives 1 to .sources and 2 to
 *  .changes, so a build that outgrows one file spills into 3 and then 4.
 *
 *  Two things about that trailer are load-bearing and were not respected.
 *
 *  Twenty-two bits is 4,194,303, and beyond it the position simply would
 *  not fit -- so the old code STORED NOTHING and said nothing, which is the
 *  worst of the available behaviours: every method past the boundary loses
 *  its source and the Browser shows an empty pane with no explanation.  The
 *  1983 library is 1.4 MB and never came near it; a Pharo-scale one does.
 *  It is now reported, and there are three files rather than one.
 *
 *  And position ZERO means "no source" to CompiledMethod>>getSource, so
 *  whatever landed at offset 0 was invisible.  Exactly one method did --
 *  the first one compiled, ArrayedCollection class>>new -- and it has been
 *  sourceless since the bootstrap was written.  A single filler byte at the
 *  front of each file means nothing starts at zero.
 */
#define SOURCE_FILES        4
#define SOURCE_CHANGES      1       /*  index 2 to the image; stays empty  */
/*
 *  62 << 16 rather than the full 22 bits: setSourcePosition:inFile: warns
 *  on the Transcript above that, and a build that prints "Source file is
 *  getting full" thousands of times has told nobody anything.
 */
#define SOURCE_FILE_LIMIT   (62u << 16)

typedef struct {
    char       *text;
    size_t      length;
    size_t      capacity;
} source_file;

static source_file  source_files[SOURCE_FILES];
static unsigned     source_current;     /*  which file is being filled  */
static unsigned     source_overflowed;  /*  methods that lost their source */

/*  Kept for the code that reports on the whole of it.  */
#define source_text     (source_files[0].text)
#define source_length   (source_files[0].length)

unsigned
BOOT_undeclared(const char **names, unsigned max)
{
    unsigned    i;

    for (i = 0; i < undeclared_count && i < max; ++i)
        names[i] = undeclared_names[i];
    return undeclared_count;
}

unsigned
BOOT_undeclared_lowercase(void)
{
    return undeclared_lowercase;
}

static boot_class *find_class(const char *name);

/*
 *  A class variable is shared by a class, its metaclass and all their
 *  subclasses, so it is found by walking the superclass chain.  It shadows a
 *  global of the same name, which is the point: several classes in the 1983
 *  library have a class variable spelled like something in Smalltalk.
 */
static st_oop
class_variable_association(boot_class *c, const char *name, unsigned depth)
{
    unsigned    i;

    if (!c || depth > 64)
        return ST_OOP_INVALID;
    for (i = 0; i < c->cvars.count; ++i) {
        if (strcmp(c->cvars.items[i], name) != 0)
            continue;
        if (c->cvar_assoc[i] == 0) {
            st_oop  key = BOOT_intern_symbol(name, NULL);
            st_oop  assoc = OM_instantiate_pointers(BOOT_global("Association"),
                                                    2);

            if (!OM_is_object(assoc))
                return ST_OOP_INVALID;
            OM_increase_ref(assoc);
            OM_store_pointer(ST_ASSOCIATION_KEY, assoc, key);
            OM_store_pointer(ST_ASSOCIATION_VALUE, assoc, ST_NIL);
            c->cvar_assoc[i] = assoc;
        }
        return c->cvar_assoc[i];
    }
    if (!c->superclass[0] || strcmp(c->superclass, "nil") == 0)
        return ST_OOP_INVALID;
    return class_variable_association(find_class(c->superclass), name,
                                      depth + 1);
}

st_oop
BOOT_lookup_global(const char *name, void *user)
{
    st_oop      association;

    /*
     *  user is the class whose method is being compiled, when there is one.
     *  Class variables are in scope there and nowhere else, which is why the
     *  compiler asks through this one hook rather than being told up front.
     */
    association = class_variable_association((boot_class *) user, name, 0);
    if (association != ST_OOP_INVALID)
        return association;

    association = global_association(name);
    if (association != ST_OOP_INVALID)
        return association;
    /*
     *  Lower-case names get the same treatment, because the 1983 sources
     *  need it.  FillInTheBlank class>>request:initialAnswer: ends
     *
     *      action: [:response | response]
     *      initialAnswer: aString.
     *      ^response
     *
     *  where response is a block argument being read from the method that
     *  encloses it -- so the method always answers nil.  That text is in
     *  Xerox's own Smalltalk-80.sources verbatim, so it shipped: their
     *  compiler put the name in Undeclared and carried on, and refusing it
     *  here would mean refusing to load the library Xerox actually released.
     *  They are counted apart from the capitalised ones, which are ordinary
     *  forward references to globals an image build creates.
     */
    if (undeclared_count < 256) {
        snprintf(undeclared_names[undeclared_count],
                 sizeof undeclared_names[0], "%.63s", name);
        ++undeclared_count;
        if (!(name[0] >= 'A' && name[0] <= 'Z'))
            ++undeclared_lowercase;
    }
    /*
     *  A Pharo pool is a CLASS, not a Dictionary.
     *
     *  1983 pools are Dictionary globals -- TextConstants is one -- and the
     *  machinery below was built for those.  Pharo declares
     *  `#pools : ['ChronologyConstants']' where ChronologyConstants is a
     *  SharedPool subclass whose CLASS VARIABLES are the pool, so its names
     *  have to resolve to that class's class variables and not to fresh
     *  globals.
     *
     *  Resolving them as globals left every one of them nil: Epoch,
     *  SecondsInDay and the rest.  The pool class's own initializer then
     *  assigned to its class variables, which nothing was reading, and
     *  `DateAndTime now' arrived at `nil * 1000' -- reported as
     *  "Message not understood: generality", two layers from the cause.
     */
    {
        boot_class *pooled = (boot_class *) user;
        unsigned    p;

        if (pooled) {
            for (p = 0; p < pooled->pools.count; ++p) {
                boot_class *pool = find_class(pooled->pools.items[p]);
                st_oop      assoc;

                if (!pool)
                    continue;
                assoc = class_variable_association(pool, name, 0);
                if (assoc != ST_OOP_INVALID)
                    return assoc;
            }
        }
    }
    {
        st_oop      assoc = define_global(name, ST_NIL);
        boot_class *c = (boot_class *) user;

        /*
         *  A capitalised name first seen while compiling a class that shares
         *  a pool is almost certainly one of that pool's.  Remember it so the
         *  binding can be put where the pool's own initializer will find it.
         */
        if (c && c->pools.count > 0 && assoc != ST_OOP_INVALID
         && pool_binding_count < 512
         && (name[0] >= 'A' && name[0] <= 'Z')) {
            pool_bindings[pool_binding_count].association = assoc;
            snprintf(pool_bindings[pool_binding_count].pool, 64, "%.63s",
                     c->pools.items[0]);
            ++pool_binding_count;
        }
        return assoc;
    }
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

static boot_trait *
find_trait(const char *name)
{
    unsigned    i;

    for (i = 0; i < trait_count; ++i) {
        if (strcmp(traits[i].name, name) == 0)
            return &traits[i];
    }
    return NULL;
}

static boot_trait *
new_trait_entry(void)
{
    if (trait_count == trait_capacity) {
        unsigned    want  = trait_capacity ? trait_capacity * 2 : 16;
        void       *grown = realloc(traits, want * sizeof *traits);

        if (!grown)
            return NULL;
        traits = grown;
        memset(traits + trait_capacity, 0,
               (want - trait_capacity) * sizeof *traits);
        trait_capacity = want;
    }
    return &traits[trait_count++];
}

static int
trait_add_method(boot_trait *t, int class_side, const char *category,
                 const char *source, const char *file, unsigned line,
                 int dialect)
{
    trait_method   *m;

    if (t->method_count == t->method_capacity) {
        unsigned    want  = t->method_capacity ? t->method_capacity * 2 : 16;
        void       *grown = realloc(t->methods, want * sizeof *t->methods);

        if (!grown)
            return 0;
        t->methods = grown;
        memset(t->methods + t->method_capacity, 0,
               (want - t->method_capacity) * sizeof *t->methods);
        t->method_capacity = want;
    }
    m = &t->methods[t->method_count];
    m->source = strdup(source);
    m->file   = strdup(file ? file : "");
    if (!m->source || !m->file) {
        free(m->source);
        free(m->file);
        memset(m, 0, sizeof *m);
        return 0;
    }
    m->line       = line;
    m->class_side = class_side;
    m->dialect    = dialect;
    snprintf(m->category, sizeof m->category, "%.63s",
             category ? category : "");
    ++t->method_count;
    return 1;
}

static void
free_traits(void)
{
    unsigned    i;
    unsigned    k;

    for (i = 0; i < trait_count; ++i) {
        for (k = 0; k < traits[i].method_count; ++k) {
            free(traits[i].methods[k].source);
            free(traits[i].methods[k].file);
        }
        free(traits[i].methods);
    }
    free(traits);
    traits         = NULL;
    trait_count    = 0;
    trait_capacity = 0;
}

/*
 *  The format word, in the layout derived from the 1983 image itself:
 *  bit 15 pointers, bit 14 not-bytes, bit 13 indexable, bits 1..11 the count
 *  of named instance variables.  It is a SmallInteger, so the low bit is the
 *  tag and every field sits one place higher than its nominal position.
 */
static st_oop
make_format(unsigned fixed, int pointers, int indexable, int bytes, int weak,
            int ephemeron)
{
    uint64_t    bits = 1;               /*  SmallInteger tag  */

    if (pointers)
        bits |= (uint64_t) 1 << 15;
    if (!bytes)
        bits |= (uint64_t) 1 << 14;
    if (indexable)
        bits |= (uint64_t) 1 << 13;
    /*
     *  Bit 12 is the one the Blue Book leaves between the indexable flag
     *  and the instance size, and it is where "weak" goes -- so a weak
     *  class is an ordinary class to everything that does not ask.
     */
    if (weak)
        bits |= (uint64_t) 1 << 12;
    /*
     *  Bit 16 is above the whole Blue Book layout, which is where an
     *  ephemeron has to go: 13, 14 and 15 are taken and 12 is weak's.  A
     *  1983 format word never sets it, so an old class reads as ordinary.
     */
    if (ephemeron)
        bits |= (uint64_t) 1 << 16;
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
#define CLASS_ORGANIZATION      5
#define CLASS_NAME              6
#define CLASS_POOL              7
#define CLASS_SHARED_POOLS      8
#define CLASS_FIXED_FIELDS      9
/*
 *  Slack in the classes the interpreter names by fixed pointer, for their
 *  class-side instance variables.  The most any class in the 1983 library
 *  declares is six.
 */
#define CLASS_PLACEHOLDER_EXTRA 16

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
    c->all_ivars.count = 0;
    if (c->superclass[0] && strcmp(c->superclass, "nil") != 0) {
        super = find_class(c->superclass);
        if (!super) {
            boot_fail("class %s has an unknown superclass %s", c->name,
                      c->superclass);
            return 0;
        }
        if (!resolve_ivars(super, depth + 1))
            return 0;
        for (i = 0; i < super->all_ivars.count; ++i) {
            name_list_add(&c->all_ivars, super->all_ivars.items[i]);
        }
        /*
         *  Metaclasses form their own chain, parallel to the classes': the
         *  metaclass of a subclass inherits from the metaclass of its
         *  superclass.  So a class method of Form sees the class-side
         *  instance variables of DisplayMedium, Object and so on.
         */
        for (i = 0; i < super->all_class_ivars.count; ++i) {
            name_list_add(&c->all_class_ivars,
                          super->all_class_ivars.items[i]);
        }
        /*  Shape is inherited unless the subclass declares its own.  */
        if (!c->indexable && super->indexable) {
            c->indexable = super->indexable;
            c->bytes     = super->bytes;
            c->words     = super->words;
        }
    }
    for (i = 0; i < c->ivars.count && c->all_ivars.count < MAX_IVARS; ++i)
        name_list_add(&c->all_ivars, c->ivars.items[i]);
    for (i = 0; i < c->class_ivars.count
             && c->all_class_ivars.count < MAX_IVARS; ++i)
        name_list_add(&c->all_class_ivars, c->class_ivars.items[i]);
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
    uint32_t    probe;
    uint32_t    start;
    st_oop      values = OM_fetch_pointer(ST_MD_VALUE_ARRAY, dict);
    st_oop      tally;

    if (values == ST_NIL || !OM_is_object(values) || capacity == 0)
        return 0;

    /*
     *  Placed where the image will look for it.
     *
     *  IdentityDictionary>>findKeyOrNil: begins at "key asOop \\ length + 1"
     *  and probes forward, wrapping.  Filling from slot zero instead is
     *  invisible to the interpreter, which scans the whole dictionary, and
     *  wrong for everything in the image, which does not: includesSelector:,
     *  compiledMethodAt: and sourceCodeAt: all go through the hash.
     *
     *  Sends therefore worked while the Browser did not.  Selecting a
     *  message showed "key not found" for three selectors in five -- the
     *  ones whose slot happened not to lie on the probe path from their own
     *  hash.  The other two in five worked, which made it look like
     *  particular methods were broken rather than every one of them.
     */
    start = (uint32_t) OM_identity_hash(selector) % capacity;
    for (probe = 0; probe < capacity; ++probe) {
        uint32_t    slot = (start + probe) % capacity;
        st_oop      key = OM_fetch_pointer(ST_MD_FIRST_KEY + slot, dict);

        if (key == selector) {
            OM_store_pointer(slot, values, method);
            return 1;
        }
        if (key == ST_NIL) {
            tally = OM_fetch_pointer(ST_MD_TALLY, dict);
            /*
             *  Room to spare, as HashedCollection>>fullCheck keeps.  A
             *  dictionary with no nil in it never ends the image's probe on
             *  a selector it does not hold.
             */
            if (OM_is_int(tally)
             && (OM_int_value(tally) + 1) * 4 > (st_int) capacity * 3)
                return 0;       /*  too full; the caller grows and retries  */
            OM_store_pointer(ST_MD_FIRST_KEY + slot, dict, selector);
            OM_store_pointer(slot, values, method);
            if (OM_is_int(tally))
                OM_store_pointer(ST_MD_TALLY, dict,
                                 OM_int_oop(OM_int_value(tally) + 1));
            return 1;
        }
    }
    return 0;                   /*  full; the caller grows and retries  */
}

/*  ----------  Parsing the source  ----------  */



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

/*
 *  The metaclass side of a class definition:
 *
 *      Form class
 *          instanceVariableNames: 'whiteMask blackMask'
 *
 *  These are instance variables of the metaclass, so they are in scope in
 *  class methods and nowhere else.  Form class>>initialize assigns to them.
 */

/*
 *  A methods-for chunk introduces a run of method chunks:
 *
 *      !Point methodsFor: 'accessing'!
 *      x
 *          ^x! !
 *
 *  "Point class methodsFor:" puts them on the metaclass instead.
 */

/*  ----------  The three passes  ----------  */

/*
 *  Pass one: allocate a class and a metaclass object for every definition,
 *  with the class field left nil.  Nothing reads it yet, and leaving it
 *  empty is what breaks the circularity.
 */
/*
 *  Instance-variable layout has to be settled BEFORE the class objects are
 *  allocated: a class object is an instance of its metaclass, so its size
 *  includes the class-side instance variables, and those are only known once
 *  the superclass chains have been walked.
 */
static int
resolve_all_ivars(void)
{
    unsigned    i;

    for (i = 0; i < class_count; ++i) {
        if (!resolve_ivars(&classes[i], 0))
            return 0;
    }
    return 1;
}

/*  How many fields a class object needs: Class's own, plus the class-side. */
static unsigned
class_object_size(const boot_class *c)
{
    return CLASS_FIXED_FIELDS + c->all_class_ivars.count;
}

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
                                                       class_object_size(c));
            c->reserved_pointer = (reserved != ST_OOP_INVALID);
        }
        /*
         *  A class the interpreter names by fixed pointer was allocated
         *  before any source was read, so its size is whatever the
         *  placeholder got.  If the class turns out to declare class-side
         *  instance variables that do not fit, say so: the alternative is a
         *  store past the end of a class object, which corrupts the heap
         *  somewhere else entirely.
         */
        if (c->reserved_pointer
         && OM_fetch_word_length(c->class_oop) < class_object_size(c)) {
            boot_fail("%s needs %u fields but its reserved pointer has %u"
                      " -- raise CLASS_PLACEHOLDER_EXTRA", c->name,
                      class_object_size(c),
                      (unsigned) OM_fetch_word_length(c->class_oop));
            return 0;
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

    /*
     *  Every class is named before any of them is furnished.
     *
     *  The loop below builds each class an Array of its instance variable
     *  names, whose elements are Strings, and it used to name the class only
     *  at the END of its own iteration.  So a class that came before Array
     *  in the file order -- ArrayedCollection, Collection,
     *  SequenceableCollection, and Array itself -- got an Array with no
     *  class, built at a moment when nothing was called Array yet.
     *
     *  An object with no class answers no messages.  Nothing noticed,
     *  because the interpreter never asks a class for its instance variable
     *  names; only the image does, and only when something wants to see
     *  inside an object.  Behavior>>allInstVarNames walks the superclass
     *  chain adding each class's names to the last, so asking ANY collection
     *  what its fields are called failed -- and that is the first thing an
     *  Inspector does.
     */
    for (i = 0; i < class_count; ++i) {
        if (OM_is_present(classes[i].class_oop))
            define_global(classes[i].name, classes[i].class_oop);
    }

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
                         make_format(c->all_ivars.count, !c->bytes && !c->words,
                                     c->indexable, c->bytes, c->weak,
                                     c->ephemeron));
        OM_store_pointer(CLASS_NAME, c->class_oop,
                         BOOT_intern_symbol(c->name, NULL));
        OM_set_class_of_object(c->class_oop, c->metaclass_oop);

        ivar_array = OM_instantiate_pointers(BOOT_global("Array"),
                                             c->ivars.count);
        for (v = 0; v < c->ivars.count; ++v)
            OM_store_pointer(v, ivar_array,
                             make_string_object(c->ivars.items[v]));
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
                         make_format(class_object_size(c), 1, 0, 0, 0, 0));
        OM_store_pointer(METACLASS_THIS_CLASS, c->metaclass_oop, c->class_oop);
        OM_set_class_of_object(c->metaclass_oop, metaclass_class);

        define_global(c->name, c->class_oop);
    }
    return 1;
}

/*  ----------  Compiling  ----------  */

static int
compile_into(boot_class *c, int class_side, const char *source,
             const char *file, unsigned line, const char *protocol)
{
    st_compile_context  ctx;
    st_compile_result   res;
    const char         *ivar_pointers[MAX_IVARS];
    unsigned            i;
    st_oop              target = class_side ? c->metaclass_oop : c->class_oop;
    st_oop              dict;
    st_oop              selector;

    memset(&ctx, 0, sizeof ctx);
    /*
     *  A class method sees the metaclass's instance variables, not the
     *  class's.  Form class>>initialize assigns whiteMask; Form>>displayOn:
     *  reads bits.  Neither can see the other's.
     */
    if (class_side) {
        /*
         *  A class method's receiver is the class OBJECT, whose first fields
         *  are Class's own instance variables -- superclass, methodDict,
         *  name and the rest.  The class-side ones follow.  Numbering the
         *  class-side variables from zero instead puts the first of them on
         *  top of superclass, and a store lands past the end of the object.
         */
        boot_class *shape = find_class("Class");
        unsigned    n = 0;

        if (shape) {
            for (i = 0; i < shape->all_ivars.count && n < MAX_IVARS; ++i)
                ivar_pointers[n++] = shape->all_ivars.items[i];
        }
        for (i = 0; i < c->all_class_ivars.count && n < MAX_IVARS; ++i)
            ivar_pointers[n++] = c->all_class_ivars.items[i];
        ctx.instance_variables      = ivar_pointers;
        ctx.instance_variable_count = n;
    }  else  {
        /*  No copy: name_list already holds exactly this shape.  */
        ctx.instance_variables      = (const char *const *) c->all_ivars.items;
        ctx.instance_variable_count = c->all_ivars.count;
    }
    ctx.user = c;

    /*
     *  What a super send looks up from.  On the instance side that is the
     *  class's own global binding, which is what Xerox used
     *  (#SmallInteger -> SmallInteger).  On the class side the method class
     *  is the metaclass, and there is no global naming it, so a keyless
     *  Association is made once per class -- again matching 1983, where
     *  IdentityDictionary class>>new: carries nil -> IdentityDictionary class.
     */
    if (class_side) {
        if (c->metaclass_association == 0) {
            st_oop  assoc = OM_instantiate_pointers(BOOT_global("Association"),
                                                    2);

            if (OM_is_object(assoc)) {
                OM_increase_ref(assoc);
                OM_store_pointer(ST_ASSOCIATION_KEY, assoc, ST_NIL);
                OM_store_pointer(ST_ASSOCIATION_VALUE, assoc,
                                 c->metaclass_oop);
                c->metaclass_association = assoc;
            }
        }
        ctx.method_class_association = c->metaclass_association;
    }  else  {
        ctx.method_class_association = global_association(c->name);
    }
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_array         = BOOT_make_array;
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;
    ctx.dialect            = current_dialect;

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
    /*
     *  The source pointer, in the three bytes the compiler left for it.
     *  Chapter 27 reads them from the end: the last holds the top six bits
     *  of the position and, above those, the file number.
     */
    {
        unsigned    file_index = 0;
        size_t      position = 0;
        uint32_t    size = OM_fetch_byte_length(res.method);

        if (!remember_source(source, &file_index, &position)) {
            /*
             *  Loud, and once.  Losing a method's source is invisible from
             *  inside -- the Browser simply shows an empty pane -- so the
             *  only place it can be reported is here.
             */
            if (source_overflowed == 1)
                boot_note("%s:%u: out of source-file room; methods from here "
                          "on will have no source", file, line);
        }  else if (size >= 3) {
            OM_store_byte(size - 3, res.method, (uint8_t) (position & 0xFF));
            OM_store_byte(size - 2, res.method,
                          (uint8_t) ((position >> 8) & 0xFF));
            /*
             *  The top two bits name the file, one-relative to the image:
             *  CompiledMethod>>fileIndex is "self last // 64 + 1".
             */
            OM_store_byte(size - 1, res.method,
                          (uint8_t) (((file_index & 3) << 6)
                                     | ((position >> 16) & 0x3F)));
        }
    }

    /*  Remember where it belongs, so the class can be organized later.  */
    if (protocol && protocol[0]) {
        unsigned    slot;

        if (method_protocol_count == method_protocol_capacity) {
            unsigned    want = method_protocol_capacity
                                 ? method_protocol_capacity * 2 : 8192;
            void       *grown = realloc(method_protocols,
                                        want * sizeof *method_protocols);

            if (!grown)
                return 1;           /*  the method is in; only the Browser
                                        pane loses this entry  */
            method_protocols         = grown;
            method_protocol_capacity = want;
        }
        slot = method_protocol_count++;

        method_protocols[slot].class_index = (unsigned) (c - classes);
        method_protocols[slot].class_side  = class_side;
        method_protocols[slot].selector    = selector;
        snprintf(method_protocols[slot].protocol, 64, "%.63s", protocol);
    }
    if (result)
        ++result->methods_compiled;
    return 1;
}

/*  ----------  Reading a source file  ----------  */

/*
 *  The bootstrap as a sink for source events.  See src/compiler/source.h.
 *
 *  Which pass this is decides which events matter.  Pass zero takes class
 *  definitions and nothing else, so that every class name exists before any
 *  method is compiled and load order stops mattering; pass two takes methods
 *  and ignores definitions it has already seen.  That split is what lets a
 *  package format work at all, and it was already here -- it just used to be
 *  spelled `definitions_only` inside one function that also knew what a
 *  chunk was.
 */
typedef struct {
    int     definitions;            /*  pass zero rather than pass two  */
    int     rejected;               /*  shapes this system cannot build */
    int     package_format;         /*  Tonel rather than 1983 chunks    */
} boot_sink_state;

static int
sink_class_def(const st_source_class_def *def, void *user)
{
    boot_sink_state    *state = (boot_sink_state *) user;
    boot_class         *c;
    unsigned            i;

    if (!state->definitions)
        return 1;

    if (def->unsupported_shape) {
        boot_note("%s: %s is not supported here", def->name,
                  def->unsupported_shape);
        SRC_names_add(&refused_classes, def->name);
        ++state->rejected;
        if (result)
            ++result->classes_rejected;
        return 1;
    }
    /*
     *  A trait defines no instances and takes no place in the superclass
     *  chain, so it is not a class entry at all: it is a named bag of
     *  method source that classes name in #traits.  Its methods arrive
     *  next, as ordinary method events naming it.
     */
    if (def->is_trait) {
        boot_trait *t;

        if (find_class(def->name)) {
            boot_fail("%s is defined as both a class and a trait", def->name);
            return 0;
        }
        if (find_trait(def->name)) {
            boot_fail("trait %s is defined twice", def->name);
            return 0;
        }
        t = new_trait_entry();
        if (!t) {
            boot_fail("out of memory for trait %s", def->name);
            return 0;
        }
        snprintf(t->name, sizeof t->name, "%.63s", def->name);
        /*
         *  No %.255s here.  The buffer is 1024 because compositions are
         *  long, and a precision left over from when it was 256 truncated
         *  them anyway -- silently, and in the middle of a trait name:
         *  BagTest's composition became "... + (TCreat", and the loader
         *  reported "unknown trait TCreat", which points at trait
         *  resolution rather than at a string length.
         */
        snprintf(t->composition, sizeof t->composition, "%s",
                 def->traits ? def->traits : "");
        if (result)
            ++result->traits_created;
        return 1;
    }
    if (find_class(def->name)) {
        /*
         *  Two packages defining one class.  Silent until now, because the
         *  entry was appended and find_class answered the first -- so the
         *  second definition's instance variables simply vanished.
         */
        boot_fail("class %s is defined twice", def->name);
        return 0;
    }

    c = new_class_entry();
    if (!c) {
        boot_fail("out of memory for class number %u", class_count + 1);
        return 0;
    }
    snprintf(c->name, sizeof c->name, "%.63s", def->name);
    snprintf(c->superclass, sizeof c->superclass, "%.63s", def->superclass);
    snprintf(c->category, sizeof c->category, "%.63s", def->category);
    /*  Likewise: the buffer is the limit, not a precision.  */
    snprintf(c->traits, sizeof c->traits, "%s",
             def->traits ? def->traits : "");
    c->from_package = state->package_format;
    c->indexable = def->indexable;
    c->bytes     = def->bytes;
    c->words     = def->words;
    c->weak      = def->weak;
    c->ephemeron = def->ephemeron;

    for (i = 0; def->ivars && i < def->ivars->count; ++i)
        name_list_add(&c->ivars, def->ivars->items[i]);
    for (i = 0; def->class_ivars && i < def->class_ivars->count; ++i)
        name_list_add(&c->class_ivars, def->class_ivars->items[i]);
    for (i = 0; def->cvars && i < def->cvars->count; ++i)
        add_cvar(c, def->cvars->items[i]);
    for (i = 0; def->pools && i < def->pools->count; ++i)
        name_list_add(&c->pools, def->pools->items[i]);

    if (result)
        ++result->classes_created;
    return 1;
}

static int
sink_class_side_def(const char *name, const st_names *ivars, void *user)
{
    boot_sink_state    *state = (boot_sink_state *) user;
    boot_class         *c;
    unsigned            i;

    if (!state->definitions)
        return 1;
    c = find_class(name);
    if (!c)
        return 1;                   /*  as before: not ours, not an error  */
    for (i = 0; i < ivars->count; ++i)
        name_list_add(&c->class_ivars, ivars->items[i]);
    return 1;
}

static int
sink_comment(const char *class_name, int class_side, const char *text,
             void *user)
{
    /*
     *  Class comments are carried by Tonel and not by the chunk files this
     *  system reads, and nothing installs them yet.  Accepting and dropping
     *  them keeps the two formats producing the same image, which is the
     *  property Phase C is gated on.
     */
    (void) class_name;
    (void) class_side;
    (void) text;
    (void) user;
    return 1;
}

static int
sink_method(const char *class_name, int class_side, const char *category,
            const char *source, const char *file, unsigned line, void *user)
{
    boot_sink_state    *state = (boot_sink_state *) user;
    boot_class         *c;
    boot_trait         *t;

    /*
     *  A trait's methods are kept as source and compiled later, once per
     *  class that takes them.  They are captured in the DEFINITIONS pass,
     *  so that a class using a trait defined in a file read after it still
     *  finds the methods -- the same reason definitions are read first.
     */
    t = find_trait(class_name);
    if (t) {
        if (!state->definitions)
            return 1;               /*  captured already  */
        if (!trait_add_method(t, class_side, category, source, file, line,
                              current_dialect)) {
            boot_fail("out of memory for a method of trait %s", class_name);
            return 0;
        }
        return 1;
    }
    if (state->definitions)
        return 1;
    /*
     *  A class this system refused is a class with no methods, quietly --
     *  the refusal was already reported by name, once, and repeating it
     *  per method would bury it.
     */
    if (class_was_refused(class_name))
        return 1;
    c = find_class(class_name);
    if (!c) {
        boot_fail("%s:%u: methods for unknown class %s", file, line,
                  class_name);
        return 0;
    }
    return compile_into(c, class_side, source, file, line, category);
}

static void
sink_diagnostic(const char *file, unsigned line, const char *message,
                void *user)
{
    boot_sink_state    *state = (boot_sink_state *) user;

    ++state->rejected;
    if (result)
        ++result->classes_rejected;
    boot_note("%s:%u: %s", file, line, message);
}

static const st_source_sink boot_sink = {
    sink_class_def,
    sink_class_side_def,
    sink_comment,
    sink_method,
    sink_diagnostic
};

static int
read_source(const char *path, int definitions_only)
{
    boot_sink_state state;
    char            err[512];

    memset(&state, 0, sizeof state);
    state.definitions    = definitions_only;
    state.package_format = strcmp(SRC_format_of(path), "tonel") == 0;
    if (!SRC_read(path, &boot_sink, &state, err, sizeof err)) {
        if (err[0])
            boot_fail("%s", err);
        return 0;
    }
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
            p = OM_instantiate_pointers(ST_NIL, CLASS_FIXED_FIELDS
                                                + CLASS_PLACEHOLDER_EXTRA);
            break;
        case FIX_SYMBOL: {
            /*
             *  A selector the interpreter sends by fixed pointer, and it has
             *  to spell something.
             *
             *  These were allocated empty and left empty -- the table has
             *  carried the text since the file was written and nothing read
             *  it.  So the VM looked up a BLANK symbol, matched nothing, and
             *  doesNotUnderstand: was never once sent to the image in the
             *  history of this bootstrap: every unhandled message went to
             *  the VM's own fallback report instead of to the 1983
             *  NotifierView the library expects.  mustBeBoolean,
             *  cannotReturn: and cannotInterpret were in the same state.
             *
             *  Filling them is only half of it.  They must also be INTERNED,
             *  or the next mention of the same characters makes a second
             *  Symbol, the method is installed under that one, and the
             *  interpreter looking up the fixed pointer still finds nothing.
             */
            size_t  n = fixed[i].text ? strlen(fixed[i].text) : 0;
            size_t  k;

            p = OM_instantiate_bytes(ST_NIL, (uint32_t) n);
            if (!OM_is_object(p))
                break;
            for (k = 0; k < n; ++k)
                OM_store_byte((uint32_t) k, p, (uint8_t) fixed[i].text[k]);
            OM_increase_ref(p);
            symbol_remember(p);
            break;
        }
        case FIX_SYSTEM_DICT:
        case FIX_ASSOCIATION:
        case FIX_ARRAY:
            p = OM_instantiate_pointers(ST_NIL, fixed[i].size);
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
     *  Character class>>value: reads the class variable CharacterTable, so
     *  the table the VM owns has to BE that class variable.  Otherwise every
     *  Character creation reads nil, and "nil at: n" walks into
     *  Object>>at:'s failure path, which reports an error by building a
     *  message that needs a Character.
     *
     *  This is the first of the class variables an image build is expected
     *  to fill in -- 62 classes in the library define a class-side
     *  initialize that has never been run here.  See doc/PLAN.md; the rest
     *  belong to Phase 8, which needs Display and Sensor to exist first.
     */
    {
        boot_class *character = find_class("Character");

        if (character) {
            st_oop  assoc = class_variable_association(character,
                                                       "CharacterTable", 0);

            if (assoc != ST_OOP_INVALID)
                OM_store_pointer(ST_ASSOCIATION_VALUE, assoc,
                                 ST_CHARACTER_TABLE);
        }
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

/*
 *  Forget everything from a previous build.
 *
 *  Zeroing the counts was enough while every table was a fixed array.  It is
 *  not now: the class entries own strdup'd names, and the symbol index holds
 *  POSITIONS into an array that is about to be refilled with entirely
 *  different Symbols -- so a stale index would answer a lookup with a
 *  confident wrong Symbol rather than with nothing.
 */
static void
reset_bootstrap_state(void)
{
    unsigned    i;

    for (i = 0; i < class_count; ++i) {
        name_list_free(&classes[i].ivars);
        name_list_free(&classes[i].class_ivars);
        name_list_free(&classes[i].cvars);
        name_list_free(&classes[i].pools);
        name_list_free(&classes[i].all_ivars);
        name_list_free(&classes[i].all_class_ivars);
        free(classes[i].cvar_assoc);
        classes[i].cvar_assoc = NULL;
    }
    if (classes)
        memset(classes, 0, class_capacity * sizeof *classes);
    class_count = 0;

    symbol_count = 0;
    free(symbol_index);
    symbol_index      = NULL;
    symbol_index_size = 0;

    method_protocol_count = 0;
    free_traits();
    SRC_names_free(&refused_classes);

    /*
     *  The source files, which nothing used to reset -- so a second build
     *  in one process appended to the first one's text and carried its
     *  length forward.  Harmless while the positions were still monotonic
     *  and fatal once there is a per-file limit to run into.
     */
    for (i = 0; i < SOURCE_FILES; ++i) {
        free(source_files[i].text);
        source_files[i].text     = NULL;
        source_files[i].length   = 0;
        source_files[i].capacity = 0;
    }
    source_current    = 0;
    source_overflowed = 0;
}

/*  ----------  Applying a trait  ----------  */

static st_oop   method_in_dictionary(st_oop dict, const char *selector);

/*
 *  One method a composition provides, with the trait it came from.
 *
 *  The origin is carried because a conflict has to be reported by naming
 *  both sides, and because the protocol the method lands in records where
 *  its source lives.
 */
typedef struct {
    const trait_method  *method;
    const boot_trait    *origin;
    char                 selector[256];
    int                  conflicted;
} flat_method;

typedef struct {
    flat_method    *items;
    unsigned        count;
    unsigned        capacity;
} flat_list;

static void
flat_free(flat_list *l)
{
    free(l->items);
    l->items    = NULL;
    l->count    = 0;
    l->capacity = 0;
}

static flat_method *
flat_find(flat_list *l, const char *selector, int class_side)
{
    unsigned    i;

    for (i = 0; i < l->count; ++i) {
        if (l->items[i].method->class_side == class_side
         && strcmp(l->items[i].selector, selector) == 0)
            return &l->items[i];
    }
    return NULL;
}

static int
flat_add(flat_list *l, const flat_method *item)
{
    if (l->count == l->capacity) {
        unsigned    want  = l->capacity ? l->capacity * 2 : 32;
        void       *grown = realloc(l->items, want * sizeof *l->items);

        if (!grown)
            return 0;
        l->items    = grown;
        l->capacity = want;
    }
    l->items[l->count++] = *item;
    return 1;
}

/*
 *  Split "TA + TB" into its terms.
 *
 *  Two of the three trait operators are understood.
 *
 *  "+" composes.  "-" excludes: `(TCreationWithTest - {#testOfSize})' takes
 *  everything that trait provides except those selectors, and Pharo's
 *  collection tests are full of it -- a suite shared by twelve collection
 *  classes, minus the two assertions that do not hold for this one.  Without
 *  it BagTest cannot load, and without BagTest neither can IdentityBagTest,
 *  so a single unimplemented operator costs a whole package.
 *
 *  "@" aliases, and is still refused.  Exclusion narrows what a class gets
 *  and can be honoured exactly; aliasing INVENTS a selector, and a class
 *  that loaded cleanly with a method under the wrong name is worse than one
 *  that refused to load.
 *
 *  Each term's exclusions are written into `excludes' at the same index as
 *  the term itself, space separated, empty when there are none -- parallel
 *  arrays rather than a struct because st_names is what every caller here
 *  already speaks.
 */
static int
split_composition(const char *composition, const char *who,
                  st_names *terms, st_names *excludes)
{
    const char *p = composition;

    while (*p) {
        char        term[128];
        char        drop[512];
        size_t      n = 0;
        size_t      d = 0;
        int         parenthesised = 0;

        drop[0] = '\0';
        while (*p && isspace((unsigned char) *p))
            ++p;
        if (*p == '(') {
            parenthesised = 1;
            ++p;
            while (*p && isspace((unsigned char) *p))
                ++p;
        }
        while (*p && *p != '+' && *p != '-' && *p != '@' && *p != ')'
            && !isspace((unsigned char) *p)) {
            if (n + 1 < sizeof term)
                term[n++] = *p;
            ++p;
        }
        term[n] = '\0';
        while (*p && isspace((unsigned char) *p))
            ++p;

        if (*p == '@') {
            boot_note("%s: trait composition '%s' uses '@', which this "
                      "system does not implement", who, composition);
            return 0;
        }
        if (*p == '-') {
            ++p;
            while (*p && isspace((unsigned char) *p))
                ++p;
            if (*p != '{') {
                boot_note("%s: trait composition '%s': expected { after -",
                          who, composition);
                return 0;
            }
            ++p;
            while (*p && *p != '}') {
                char    sel[128];
                size_t  m = 0;

                while (*p && (isspace((unsigned char) *p) || *p == '.'
                           || *p == '#'))
                    ++p;
                while (*p && *p != '}' && *p != '.'
                    && !isspace((unsigned char) *p)) {
                    if (m + 1 < sizeof sel)
                        sel[m++] = *p;
                    ++p;
                }
                sel[m] = '\0';
                if (m && d + m + 2 < sizeof drop)
                    d += (size_t) snprintf(drop + d, sizeof drop - d, "%s%s",
                                           d ? " " : "", sel);
            }
            if (*p == '}')
                ++p;
        }
        while (*p && isspace((unsigned char) *p))
            ++p;
        if (parenthesised && *p == ')')
            ++p;

        if (n) {
            SRC_names_add(terms, term);
            SRC_names_add(excludes, drop);
        }
        while (*p && (isspace((unsigned char) *p) || *p == '+'))
            ++p;
    }
    return 1;
}

/*  Is `selector' one of the space-separated names in `list'?  */
static int
composition_excludes(const char *list, const char *selector)
{
    const char *p = list;
    size_t      n = strlen(selector);

    if (!list || !list[0])
        return 0;
    while (*p) {
        while (*p == ' ')
            ++p;
        if (strncmp(p, selector, n) == 0 && (p[n] == ' ' || p[n] == '\0'))
            return 1;
        while (*p && *p != ' ')
            ++p;
    }
    return 0;
}

/*
 *  Everything a composition provides, flattened.
 *
 *  Within one trait, its own methods override the ones it takes from the
 *  traits it composes -- that is not a conflict, it is what composing means.
 *  BETWEEN two traits at the same level it is a conflict, and neither is
 *  installed: a silent first-wins would make the answer depend on the order
 *  the names were written, which is exactly the bug traits exist to avoid.
 *  A trait reached twice by different paths is one trait, not a conflict.
 */
static int
gather_trait(const char *name, const char *who, flat_list *out,
             st_names *visiting, const char *excluding)
{
    boot_trait *t = find_trait(name);
    unsigned    i;
    st_names    terms;
    st_names    drops;
    int         ok = 1;

    if (!t) {
        boot_note("%s: unknown trait %s", who, name);
        return 0;
    }
    for (i = 0; i < visiting->count; ++i) {
        if (strcmp(visiting->items[i], name) == 0)
            return 1;               /*  a cycle, or a diamond; once is enough */
    }
    SRC_names_add(visiting, name);

    memset(&terms, 0, sizeof terms);
    memset(&drops, 0, sizeof drops);
    if (!split_composition(t->composition, t->name, &terms, &drops)) {
        SRC_names_free(&terms);
        SRC_names_free(&drops);
        return 0;
    }
    for (i = 0; ok && i < terms.count; ++i)
        ok = gather_trait(terms.items[i], who, out, visiting,
                          i < drops.count ? drops.items[i] : NULL);
    SRC_names_free(&terms);
    SRC_names_free(&drops);
    if (!ok)
        return 0;

    /*  Its own methods, which override anything it composed.  */
    for (i = 0; i < t->method_count; ++i) {
        flat_method  item;
        flat_method *existing;

        memset(&item, 0, sizeof item);
        item.method = &t->methods[i];
        item.origin = t;
        if (COMPILE_selector_of(t->methods[i].source, item.selector,
                                sizeof item.selector) != 0) {
            boot_note("%s:%u: in trait %s: not a method pattern",
                      t->methods[i].file, t->methods[i].line, t->name);
            return 0;
        }
        /*
         *  An excluded selector is dropped HERE, after the composition this
         *  trait made of others has been gathered -- so `(T - {#x})' removes
         *  T's own #x and leaves whatever T composed from elsewhere, which
         *  is what the operator means.
         */
        if (composition_excludes(excluding, item.selector))
            continue;
        existing = flat_find(out, item.selector, t->methods[i].class_side);
        if (existing)
            *existing = item;       /*  the composing trait wins  */
        else if (!flat_add(out, &item))
            return 0;
    }
    return 1;
}

/*
 *  Merge one trait's flattened methods into a class's, detecting conflicts
 *  between siblings in the class's own composition.
 */
static int
merge_sibling(flat_list *into, flat_list *from, const char *who)
{
    unsigned    i;

    for (i = 0; i < from->count; ++i) {
        flat_method *existing = flat_find(into, from->items[i].selector,
                                          from->items[i].method->class_side);

        if (!existing) {
            if (!flat_add(into, &from->items[i]))
                return 0;
            continue;
        }
        if (existing->origin == from->items[i].origin)
            continue;               /*  one trait reached twice  */
        if (!existing->conflicted) {
            boot_note("%s: %s%s is provided by both %s and %s; neither is "
                      "installed", who,
                      from->items[i].method->class_side ? "class>>" : "",
                      from->items[i].selector,
                      existing->origin->name, from->items[i].origin->name);
            existing->conflicted = 1;
        }
    }
    return 1;
}

/*
 *  Apply every class's trait composition, after all of its own methods are
 *  compiled -- a method the class defines itself always wins, and it can
 *  only be known to be there once the compile pass has run.
 */
static int
flatten_traits(void)
{
    unsigned    ci;

    for (ci = 0; ci < class_count; ++ci) {
        boot_class *c = &classes[ci];
        st_names    terms;
        st_names    drops;
        flat_list   all;
        unsigned    i;
        int         ok = 1;

        if (!c->traits[0])
            continue;

        memset(&terms, 0, sizeof terms);
        memset(&drops, 0, sizeof drops);
        memset(&all, 0, sizeof all);
        if (!split_composition(c->traits, c->name, &terms, &drops)) {
            SRC_names_free(&terms);
            SRC_names_free(&drops);
            if (result)
                ++result->traits_rejected;
            continue;
        }
        for (i = 0; ok && i < terms.count; ++i) {
            flat_list   one;
            st_names    visiting;

            memset(&one, 0, sizeof one);
            memset(&visiting, 0, sizeof visiting);
            ok = gather_trait(terms.items[i], c->name, &one, &visiting,
                              i < drops.count ? drops.items[i] : NULL);
            SRC_names_free(&visiting);
            if (ok)
                ok = merge_sibling(&all, &one, c->name);
            flat_free(&one);
        }
        SRC_names_free(&terms);
        SRC_names_free(&drops);
        if (!ok) {
            flat_free(&all);
            if (result)
                ++result->traits_rejected;
            continue;
        }

        for (i = 0; i < all.count; ++i) {
            const trait_method *m = all.items[i].method;
            st_oop              target;
            char                protocol[64];
            int                 saved;

            if (all.items[i].conflicted)
                continue;
            target = m->class_side ? c->metaclass_oop : c->class_oop;
            if (OM_is_present(method_in_dictionary(
                    OM_fetch_pointer(CLASS_METHOD_DICT, target),
                    all.items[i].selector)))
                continue;           /*  the class says it itself  */

            /*
             *  The protocol says where the source lives.  A leading star is
             *  the convention for "defined elsewhere", which is exactly
             *  true here -- and it puts every flattened method together in
             *  the Browser, where the trait's file is the place to edit it.
             */
            snprintf(protocol, sizeof protocol, "*trait:%.40s",
                     all.items[i].origin->name);
            saved = current_dialect;
            current_dialect = m->dialect;
            if (!compile_into(c, m->class_side, m->source, m->file, m->line,
                              protocol)) {
                current_dialect = saved;
                flat_free(&all);
                return 0;
            }
            current_dialect = saved;
            if (result)
                ++result->methods_flattened;
        }
        flat_free(&all);
    }
    return 1;
}

/*  ----------  "new" that runs "initialize"  ----------  */

/*
 *  Pharo-flavoured code writes an initialize method and expects "new" to
 *  call it.  The 1983 system does not: Behavior>>new is primitive 70 and
 *  nothing else, and the ~34 classes that want initialization write
 *  "^super new initialize" out by hand.
 *
 *  Changing Behavior>>new globally would run initialize TWICE for every one
 *  of those thirty-four, and a per-class flag read at allocation time would
 *  tax the hottest path in the system.  So the loader writes the 1983 idiom
 *  instead -- the same method those thirty-four already have, for the
 *  classes that need it and did not say it.
 *
 *  Only for classes a package format defined.  A chunk file is 1983 source
 *  and already means what it says.
 *
 *  The rule that keeps it correct is the one about the chain: a class gets
 *  the synthesized "new" only when NOTHING between it and Behavior has a
 *  class-side "new" already.  Give it to both a class and its subclass and
 *  the subclass's initialize runs twice -- the subclass's "super new" finds
 *  the superclass's "new", which sends initialize, which dispatches back
 *  down to the subclass.  That is the same double-initialization the global
 *  change would have caused, arrived at from the other direction.
 */

/*  The loaded chain, nearest first, as boot_class entries.  */
static boot_class *
superclass_of(const boot_class *c)
{
    if (!c->superclass[0] || strcmp(c->superclass, "nil") == 0)
        return NULL;
    return find_class(c->superclass);
}

static int
chain_defines(const boot_class *c, int class_side, const char *selector)
{
    const boot_class   *p;

    for (p = c; p; p = superclass_of(p)) {
        st_oop  target = class_side ? p->metaclass_oop : p->class_oop;

        if (!OM_is_present(target))
            continue;
        if (OM_is_present(method_in_dictionary(
                OM_fetch_pointer(CLASS_METHOD_DICT, target), selector)))
            return 1;
    }
    return 0;
}

/*  How far below the root a class sits, so parents are decided first.  */
static unsigned
chain_depth(const boot_class *c)
{
    unsigned            n = 0;
    const boot_class   *p;

    for (p = superclass_of(c); p && n < 1000; p = superclass_of(p))
        ++n;
    return n;
}

static int
synthesize_initializing_new(void)
{
    static const char   source[] = "new\n\t\"Synthesized by the loader: this "
                                   "class defines initialize and no "
                                   "new.\"\n\t^super new initialize";
    unsigned            depth;
    unsigned            deepest = 0;
    unsigned            i;
    unsigned            made = 0;
    char                names[512] = "";

    for (i = 0; i < class_count; ++i) {
        unsigned    d = chain_depth(&classes[i]);

        if (d > deepest)
            deepest = d;
    }

    /*
     *  Shallowest first, so that when a subclass asks "does anything above
     *  me have new" the answer includes what this pass just wrote.
     */
    for (depth = 0; depth <= deepest; ++depth) {
        for (i = 0; i < class_count; ++i) {
            boot_class *c = &classes[i];

            if (!c->from_package || chain_depth(c) != depth)
                continue;
            if (!OM_is_present(c->class_oop)
             || !OM_is_present(c->metaclass_oop))
                continue;
            if (!chain_defines(c, 0, "initialize"))
                continue;
            /*
             *  Only a `new' this class defines ITSELF, not one anywhere up
             *  the chain.
             *
             *  Asking the whole chain declines for every Pharo class that
             *  subclasses a 1983 collection, because HashedCollection
             *  class>>new exists -- and 1983's route is new -> new: ->
             *  init:, which never sends #initialize.  So
             *  AnnouncementSetWithExclusions, whose initialize builds its
             *  exclusions collection, got a new that did not call it and
             *  every method touching exclusions failed on nil.
             *
             *  Overriding an inherited `new' with `^super new initialize'
             *  is exactly what the class wanted; declining because an
             *  ancestor had one is what left it half-built.
             */
            {
                st_oop  meta = c->metaclass_oop;

                if (OM_is_present(meta)
                 && OM_is_present(method_in_dictionary(
                        OM_fetch_pointer(CLASS_METHOD_DICT, meta), "new")))
                    continue;
            }
            if (!compile_into(c, 1, source, "<the loader>", 0,
                              "instance creation"))
                return 0;
            ++made;
            if (result)
                ++result->news_synthesized;
            if (made <= 12) {
                size_t  used = strlen(names);

                snprintf(names + used, sizeof names - used, "%s%s",
                         used ? " " : "", c->name);
            }
        }
    }
    if (made)
        boot_note("%u class%s given \"new ^super new initialize\": %s%s",
                  made, made == 1 ? " was" : "es were", names,
                  made > 12 ? " ... and more" : "");
    return 1;
}

/*  ----------  The supersession guard  ----------
 *
 *  A profile's #supersede says "my version of that class, not the one you
 *  inherited".  It is the only thing the loader does that can remove
 *  behaviour without anything going wrong at the time.  An #exclude leaves
 *  a hole a sender falls into immediately; a supersession leaves a class
 *  that is still there, still answers most of what it used to, and answers
 *  nothing at all to the handful of selectors its replacement never had.
 *  Those show up much later, in whatever happened to send one.
 *
 *  This is not hypothetical.  Superseding 1983's Date and Time with Pharo's
 *  Chronology lost `Time millisecondsToRun:', `Time dateAndTimeNow',
 *  `Time readFrom:', `Date leapYear:' and `Date readFrom:' -- five methods
 *  the remaining 1983 sources still send.  They were found by predicting
 *  them and grepping.  Nothing would have caught them, and every future
 *  turn of the ratchet has the same hazard.
 *
 *  So ask the question mechanically: re-read each file the profile dropped,
 *  and for every method it defined, ask the built image whether the class
 *  that replaced it still answers that selector.  Reading the dropped file
 *  a second time is the whole trick -- it is the only remaining record of
 *  what the class used to be, and it costs one pass over a few files.
 *
 *  A gap is reported, not fatal.  Some are deliberate: a replacement is
 *  allowed to drop protocol nobody wants any more, and the loader is not
 *  in a position to know which.  What it can do is make the list short,
 *  visible and countable, so that a gap is a decision somebody took rather
 *  than one that took itself.
 */

/*  One selector a supersession dropped, and who might miss it.  */
typedef struct {
    char        class_name[128];
    char        selector[128];
    int         class_side;
    /*
     *  2  something sends it and NOTHING in the image implements it -- a
     *     hole, and the only list worth acting on.
     *  1  something sends it and another class implements it: the name is
     *     shared, and the send almost certainly means the other one.
     *  0  nothing sends it at all -- the old implementation's own.
     */
    int         known;
    /*
     *  The methods that send it, named.  This is the whole value of the
     *  report: a bare list of dropped selectors says how bad and not which,
     *  and every one of them then has to be grepped for by hand.  With the
     *  senders in front of you, `Dictionary>>declare:from:, sent by
     *  Class>>declare:' is obviously real and `Set>>swap:with:, no sender'
     *  is obviously not.
     */
    char        senders[192];
    char        elsewhere[128];         /*  who else implements the name  */
} supersede_gap;

typedef struct {
    supersede_gap   gaps[256];
    unsigned        count;              /*  gaps recorded, capped         */
    unsigned        found;              /*  gaps seen, uncapped           */
} supersede_scan;

/*
 *  Whether that method sends that selector.
 *
 *  A send names its selector in the method's literal frame, and the frame is
 *  the leading `literal count' words of the method after the header -- the
 *  same decoding the collector does when it walks a CompiledMethod.  The
 *  thirty-two special selectors are encoded in bytecodes instead and are not
 *  in the frame, which costs nothing here: none of them can be superseded,
 *  because they are the Blue Book's own and every one is implemented by a
 *  class this system cannot replace.
 */
static int
method_sends(st_oop method, st_oop selector_oop)
{
    uint32_t    slots;
    uint32_t    literals;
    uint32_t    i;

    if (!OM_is_present(method) || !OM_is_present(selector_oop))
        return 0;
    slots = OM_fetch_byte_length(method) / (uint32_t) sizeof(st_oop);
    if (slots == 0)
        return 0;
    literals = (uint32_t) ((OM_fetch_pointer(0, method) >> 1) & 63);
    for (i = 1; i <= literals && i < slots; ++i) {
        if (OM_fetch_pointer(i, method) == selector_oop)
            return 1;
    }
    return 0;
}

/*
 *  Every loaded method that sends that selector, and every class other than
 *  this one that implements it.
 *
 *  Both questions, because they are the two ways a dropped name can still be
 *  spelled in the image and only one of them is a problem.  A name with
 *  senders is protocol something will ask for; a name with no senders and an
 *  implementor elsewhere is a collision -- `init:' is Parser's and
 *  `swap:with:' is SequenceableCollection's, and neither has anything to do
 *  with the class that lost its own.
 *
 *  Read-only: the selector arrives as an oop that was already interned, so
 *  nothing here can add to the symbol table the guard is reading.
 */
static unsigned
senders_of(st_oop selector_oop, const boot_class *skip, int skip_side,
           char *out, size_t out_len, char *elsewhere, size_t elsewhere_len)
{
    unsigned    found = 0;
    unsigned    named = 0;
    unsigned    ci;

    out[0] = '\0';
    elsewhere[0] = '\0';
    for (ci = 0; ci < class_count; ++ci) {
        int     side;

        for (side = 0; side < 2; ++side) {
            st_oop      target = side ? classes[ci].metaclass_oop
                                      : classes[ci].class_oop;
            st_oop      dict;
            uint32_t    capacity;
            uint32_t    slot;

            if (!OM_is_present(target))
                continue;
            dict = OM_fetch_pointer(CLASS_METHOD_DICT, target);
            if (!OM_is_present(dict))
                continue;
            capacity = OM_method_dict_capacity(dict);
            for (slot = 0; slot < capacity; ++slot) {
                st_oop  key    = OM_method_dict_key(dict, slot);
                st_oop  method = OM_method_dict_value(dict, slot);
                char    name[300];

                if (!OM_is_present(method))
                    continue;
                if (key == selector_oop
                 && !(&classes[ci] == skip && side == skip_side)
                 && elsewhere[0] == '\0')
                    snprintf(elsewhere, elsewhere_len, "%s%s",
                             classes[ci].name, side ? " class" : "");
                if (!method_sends(method, selector_oop))
                    continue;
                ++found;
                if (named >= 3)
                    continue;
                {
                    char    selector[128];

                    OM_string_of(key, selector, sizeof selector);
                    snprintf(name, sizeof name, "%s%s%s>>%s",
                             named ? ", " : "", classes[ci].name,
                             side ? " class" : "", selector);
                }
                if (strlen(out) + strlen(name) + 1 < out_len) {
                    strcat(out, name);
                    ++named;
                }
            }
        }
    }
    if (found > named && strlen(out) + 16 < out_len)
        snprintf(out + strlen(out), out_len - strlen(out), " and %u more",
                 found - named);
    return found;
}

static int
supersede_saw_method(const char *class_name, int class_side,
                     const char *category, const char *source,
                     const char *file, unsigned line, void *user)
{
    supersede_scan *scan = user;
    boot_class     *c;
    supersede_gap  *gap;
    char            selector[128];
    long            position;

    (void) category;
    (void) file;
    (void) line;

    if (COMPILE_selector_of(source, selector, sizeof selector) != 0)
        return 1;                       /*  no pattern: not our question  */
    c = find_class(class_name);
    if (!c || !OM_is_present(c->class_oop))
        return 1;                       /*  the class went away entirely   */

    /*
     *  The symbol table is asked FIRST, and it decides both questions.
     *
     *  If nothing in the image spells this name, no method dictionary can
     *  be keyed by it, so the selector is certainly not answered -- and
     *  chain_defines must not be called, because looking a selector up
     *  interns it, which would add the symbol to the image the guard is
     *  supposed to be reading.  That is not a stylistic point: the first
     *  version grew the symbol table by one per gap, so the report claimed
     *  every gap was live, each having been made live by the check before
     *  it.  Asking in this order makes the guard read-only and the answer
     *  true at the same time.
     */
    position = symbol_find(selector, strlen(selector));
    if (position >= 0 && chain_defines(c, class_side, selector))
        return 1;

    ++scan->found;
    if (result)
        ++result->supersession_gaps;
    if (scan->count == sizeof scan->gaps / sizeof scan->gaps[0])
        return 1;
    gap = &scan->gaps[scan->count++];
    snprintf(gap->class_name, sizeof gap->class_name, "%s", class_name);
    snprintf(gap->selector, sizeof gap->selector, "%s", selector);
    gap->class_side = class_side;
    /*
     *  Most of what a supersession drops is the old implementation's own
     *  scaffolding: `makeRoomAtEnd' existed to serve an array the new class
     *  does not have, and losing it costs nothing.  Separating that from
     *  protocol something still wants is what keeps the list short enough
     *  to read.
     *
     *  The name EXISTING is not that question, and asking it that way put
     *  `init:' and `swap:with:' at the top of the list -- Parser implements
     *  the first and SequenceableCollection the second, and neither has
     *  anything to do with the class that lost its own.  Twelve of the
     *  fourteen names reported that way were collisions.  The question is
     *  whether a loaded method SENDS it, and the answer comes with the
     *  senders named, because that is what turns the report into work
     *  somebody can do.
     *
     *  Still a filter and not a proof: a send is untyped, so
     *  `SortedCollection>>sort:' sending `swap:with:' to itself would count
     *  for Set>>swap:with: too if anything sent it at all.  It errs toward
     *  showing too much, which is the only direction a guard may err in.
     */
    gap->senders[0]   = '\0';
    gap->elsewhere[0] = '\0';
    if (position >= 0
     && senders_of(symbols[position], c, class_side,
                   gap->senders, sizeof gap->senders,
                   gap->elsewhere, sizeof gap->elsewhere) > 0)
        gap->known = gap->elsewhere[0] ? 1 : 2;
    else
        gap->known = 0;
    return 1;
}

static void
report_gaps(const supersede_scan *scan, int known, const char *heading)
{
    unsigned    shown = 0;
    unsigned    i;

    for (i = 0; i < scan->count; ++i) {
        const supersede_gap *g = &scan->gaps[i];

        if (g->known != known)
            continue;
        if (shown++ == 0)
            fprintf(stderr, "st80: %s\n", heading);
        /*
         *  Named one per line rather than tallied.  A count answers "how
         *  bad", which is what one asks after already knowing which -- and
         *  not knowing which is the entire failure this guards against.
         */
        if (shown <= 30) {
            char    what[512];

            snprintf(what, sizeof what, "%s%s>>%s", g->class_name,
                     g->class_side ? " class" : "", g->selector);
            if (g->senders[0] && g->elsewhere[0])
                fprintf(stderr, "  %-40s sent by %s -- answered by %s\n",
                        what, g->senders, g->elsewhere);
            else if (g->senders[0])
                fprintf(stderr, "  %-40s sent by %s\n", what, g->senders);
            else if (g->elsewhere[0])
                fprintf(stderr, "  %-40s the name is %s's\n",
                        what, g->elsewhere);
            else
                fprintf(stderr, "  %s\n", what);
        }  else if (shown == 31) {
            fprintf(stderr, "  ... and more\n");
        }
    }
}

static int
check_supersessions(void)
{
    static const st_source_sink sink = { NULL, NULL, NULL,
                                         supersede_saw_method, NULL };
    const st_names *dropped = PROFILE_superseded_files();
    supersede_scan *scan;
    unsigned        i;

    if (dropped->count == 0)
        return 1;
    scan = (supersede_scan *) calloc(1, sizeof *scan);
    if (!scan) {
        boot_fail("out of memory checking supersessions");
        return 0;
    }
    for (i = 0; i < dropped->count; ++i) {
        char    error[256];

        /*
         *  A file that cannot be re-read is not a load failure -- it was
         *  deliberately not loaded.  It means only that this file's
         *  protocol went unchecked, which is worth one line and no more.
         */
        if (!SRC_read(dropped->items[i], &sink, scan, error, sizeof error))
            fprintf(stderr, "st80: cannot re-read superseded %s: %s\n",
                    dropped->items[i], error);
    }
    report_gaps(scan, 2,
                "superseded protocol that is GONE, still sent, and answered "
                "by nothing -- these are holes:");
    report_gaps(scan, 1,
                "superseded protocol whose name another class answers "
                "(so the senders below probably mean that one):");
    report_gaps(scan, 0,
                "superseded protocol that nothing sends at all "
                "(the old implementation's own scaffolding):");
    if (scan->found)
        fprintf(stderr,
                "st80: %u selector%s lost to supersession across %u file%s\n",
                scan->found, scan->found == 1 ? "" : "s",
                dropped->count, dropped->count == 1 ? "" : "s");
    free(scan);
    return 1;
}

static int
boot_build_locked(const char *const *paths, const int *dialects,
                  unsigned path_count, st_bootstrap_result *out)
{
    unsigned    i;

    result = out;
    memset(out, 0, sizeof *out);
    reset_bootstrap_state();
    reserved_class_count = 0;
    global_count = 0;

    if (OM_init() != 0) {
        boot_fail("cannot initialize the object memory");
        return -1;
    }
    if (!allocate_fixed_objects())
        return -1;

    smalltalk      = ST_SMALLTALK;
    globals_values = OM_instantiate_pointers(ST_NIL, GLOBALS_FIRST);
    OM_increase_ref(globals_values);
    /*
     *  Published immediately, so that a lookup during the bootstrap finds
     *  what has been defined so far and a reload finds all of it.
     */
    OM_store_pointer(0, ST_SMALLTALK, globals_values);

    path_dialects = dialects;

    /*  Pass zero: read every definition, so forward references resolve.  */
    for (i = 0; i < path_count; ++i) {
        current_dialect = dialects ? dialects[i] : ST_DIALECT_BLUE_BOOK;
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
                    classes[k].ivars.count,
                    classes[k].indexable
                        ? (classes[k].bytes ? " bytes"
                            : (classes[k].words ? " words" : " pointers"))
                        : "");
    }
    if (!resolve_all_ivars())
        return -1;
    if (!allocate_class_objects())
        return -1;
    if (!link_class_objects())
        return -1;
    if (!adopt_symbols())
        return -1;
    if (!adopt_associations())
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
        current_dialect = dialects ? dialects[i] : ST_DIALECT_BLUE_BOOK;
        if (!read_source(paths[i], 0))
            return -1;
    }

    if (!flatten_traits())
        return -1;
    /*
     *  After flattening, because a trait can be where initialize comes
     *  from -- and after the compile pass, because "does this class say
     *  new itself" cannot be answered before its methods are in.
     */
    if (!synthesize_initializing_new())
        return -1;
    /*
     *  Last, because the question is about the finished image: a selector
     *  can arrive from a trait, from an extension file loaded later, or
     *  from the synthesized `new' just above.
     */
    if (!check_supersessions())
        return -1;

    install_closure_support();
    return 0;
}


/*
 *  Tell the VM where BlockClosure is, if the profile loaded one.
 *
 *  Not a guaranteed object pointer: the Blue Book's table ends at 56 and a
 *  bb build reads a real 1983 image where the numbers above that are
 *  ordinary objects.  The VM-state slots are the right home, and they carry
 *  through a snapshot, which a C static would not.
 *
 *  A profile without BlockClosure leaves the slot nil, and then every
 *  closure primitive fails and the closure bytecodes are unreachable.  That
 *  is not a degraded mode, it is the Blue Book, and it is what the bb build
 *  runs.
 */
static void
install_closure_support(void)
{
    st_oop  closure_class = BOOT_global("BlockClosure");

    if (OM_is_present(closure_class)) {
        st_om_vm_state[ST_VM_CLASS_BLOCK_CLOSURE] = closure_class;
        OM_increase_ref(closure_class);
    }
    {
        st_oop  selector = BOOT_intern_symbol("aboutToReturn:through:", NULL);

        if (OM_is_present(selector)) {
            st_om_vm_state[ST_VM_SELECTOR_ABOUT_TO_RETURN] = selector;
            OM_increase_ref(selector);
        }
    }
}

/*
 *  BOOT_build kept the caller's result pointer in a static so that later
 *  steps could count what they did.  Everything that runs AFTER it -- a doIt
 *  compiled against the finished image, say -- still interns symbols, and
 *  BOOT_intern_symbol still incremented through that pointer.  By then the
 *  caller's st_bootstrap_result is usually a dead stack frame, which is a
 *  write to a returned function's locals: harmless for years, then not.
 *
 *  The pointer is dropped here, where the counting ends.
 */
int
BOOT_build_dialects(const char *const *paths, const int *dialects,
                    unsigned path_count, st_bootstrap_result *out)
{
    int status = boot_build_locked(paths, dialects, path_count, out);

    result = NULL;
    path_dialects = NULL;
    /*  Nothing C holds survives a collection unless the walk can see it.  */
    ST_interp_install_roots(BOOT_provide_roots);
    return status;
}

int
BOOT_build(const char *const *paths, unsigned path_count,
           st_bootstrap_result *out)
{
    return BOOT_build_dialects(paths, NULL, path_count, out);
}

/*
 *  Give every symbol its class.
 *
 *  Symbols are interned from the first moment a name is needed, which is
 *  long before the class named Symbol exists -- the class objects are built
 *  in the order the sources were read, and the first of them has a name that
 *  must be interned to be stored.  Those early symbols were created with a
 *  nil class and stayed that way.
 *
 *  The effect was beautifully arbitrary: whether a class could print its own
 *  name depended on where its source file sorted relative to
 *  Collections-Text/Symbol.  Set printed, OrderedCollection did not, and the
 *  reason was alphabetical.
 */
static int
adopt_symbols(void)
{
    st_oop      symbol_class = BOOT_global("Symbol");
    unsigned    i;

    if (!OM_is_present(symbol_class)) {
        boot_fail("the kernel must define Symbol");
        return 0;
    }
    for (i = 0; i < symbol_count; ++i) {
        if (OM_fetch_class(symbols[i]) != symbol_class)
            OM_set_class_of_object(symbols[i], symbol_class);
    }
    return 1;
}

/*
 *  Give every global binding its class.
 *
 *  The same problem as the symbols above, and it went unnoticed for longer
 *  because it hid so much better.  A binding is made the first time a name
 *  is needed, which for the first seventeen of them is before the class
 *  named Association exists -- Association is itself the seventeenth.  Those
 *  carried a nil class.
 *
 *  Nothing about the classes they name stopped working, because a compiled
 *  method holds the binding itself and reads its value field directly; the
 *  interpreter never asks a binding what class it is.  Only ONE thing does,
 *  and it is Dictionary>>add:, which sends #key to what it is given.  So the
 *  seventeen were silently refused by the system dictionary, and
 *  "Smalltalk includesKey: #Array" answered false in a system where Array
 *  worked perfectly -- as did OrderedCollection, Stream, Interval and every
 *  other collection and stream in the kernel.
 *
 *  It cost seventeen doesNotUnderstand lines during the bootstrap, which is
 *  the whole of what it looked like from outside.
 */
static int
adopt_associations(void)
{
    st_oop      association_class = BOOT_global("Association");
    unsigned    i;

    if (!OM_is_present(association_class)) {
        boot_fail("the kernel must define Association");
        return 0;
    }
    for (i = 0; i < global_count; ++i) {
        st_oop  binding = OM_fetch_pointer(i, globals_values);

        if (OM_is_present(binding)
         && OM_fetch_class(binding) != association_class)
            OM_set_class_of_object(binding, association_class);
    }
    return 1;
}

/*
 *  Append one method's source in chunk format, answering which file it went
 *  in and where it started.
 *
 *  A bang inside the text is doubled, which is the chunk format's own escape
 *  and what nextChunk undoes on the way back out.  Answers 0 when there is
 *  nowhere left to put it, which the caller reports.
 */
static int
remember_source(const char *text, unsigned *file_index, size_t *position)
{
    size_t          n = strlen(text);
    size_t          doubled = 0;
    size_t          i;
    source_file    *f;

    for (i = 0; i < n; ++i) {
        if (text[i] == '!')
            ++doubled;
    }
    /*  The text, its doubled bangs, the terminating bang, and the filler.  */
    {
        size_t  want = n + doubled + 2;

        while (source_current < SOURCE_FILES) {
            size_t  have = source_files[source_current].length;

            if (source_current == SOURCE_CHANGES) {
                ++source_current;       /*  reserved for the changes file  */
                continue;
            }
            if (have + want <= SOURCE_FILE_LIMIT)
                break;
            ++source_current;
        }
        if (source_current >= SOURCE_FILES) {
            ++source_overflowed;
            return 0;
        }
        f = &source_files[source_current];
        if (f->length + want > f->capacity) {
            size_t  grow = f->capacity ? f->capacity * 2 : 65536;

            while (grow < f->length + want)
                grow *= 2;
            {
                char *grown = (char *) realloc(f->text, grow);

                if (!grown)
                    return 0;
                f->text     = grown;
                f->capacity = grow;
            }
        }
    }
    /*
     *  The filler.  Position zero is how a CompiledMethod says it has no
     *  source at all, so nothing real may start there.
     */
    if (f->length == 0)
        f->text[f->length++] = '!';

    *file_index = source_current;
    *position   = f->length;
    for (i = 0; i <= n; ++i) {
        char    c = (i < n) ? text[i] : '!';

        f->text[f->length++] = c;
        if (i < n && c == '!')
            f->text[f->length++] = c;
    }
    return 1;
}


/*  ----------  Roots  ----------  */

/*
 *  Everything the bootstrap holds in C.
 *
 *  A marking collection rebuilds every reference count from this walk, so
 *  the OM_increase_ref calls scattered through this file protect nothing on
 *  their own -- interp.h says so in as many words, and the bootstrap was
 *  ignoring it.  The symptom was a symbol being freed and its table slot
 *  reused while C still held the pointer, which then read as a different
 *  string entirely: the same text hashed to two different values depending
 *  on whether a collection had happened in between.
 */
void
BOOT_provide_roots(om_visit_fn visit)
{
    unsigned    i;
    unsigned    k;

    visit(smalltalk);
    visit(globals_values);
    visit(symbol_table);
    for (i = 0; i < symbol_count; ++i)
        visit(symbols[i]);
    for (i = 0; i < class_count; ++i) {
        visit(classes[i].class_oop);
        visit(classes[i].metaclass_oop);
        visit(classes[i].metaclass_association);
        for (k = 0; k < classes[i].cvars.count; ++k)
            visit(classes[i].cvar_assoc[k]);
    }
}

/*  ----------  The display  ----------  */

/*
 *  Give the image a screen to draw on.
 *
 *  In a 1983 image Display is a DisplayScreen that was made when the image
 *  was built and has been carried forward by every snapshot since.  Building
 *  from nothing, someone has to make the first one, and it cannot be
 *  Smalltalk code: DisplayScreen class>>new would need a Display to ask how
 *  big the screen is.
 *
 *  The bits are a WordArray of whole words per scan line, which is what
 *  Form>>setExtent:fromArray:setOffset: would allocate and what BitBlt
 *  assumes.  Telling the graphics layer which Form is the screen is what
 *  primitive 102 does when Smalltalk sends beDisplay; doing it here means an
 *  image can draw before it has run any code of its own.
 */
int
BOOT_install_display(unsigned width, unsigned height)
{
    st_oop      screen_class = BOOT_global("DisplayScreen");
    st_oop      word_array   = BOOT_global("WordArray");
    st_oop      screen;
    st_oop      bits;
    st_oop      origin;
    uint32_t    raster = (width + 15) / 16;

    if (!OM_is_present(screen_class) || !OM_is_present(word_array)) {
        boot_fail("the sources must define DisplayScreen and WordArray");
        return 0;
    }
    bits = OM_instantiate_words(word_array, raster * height);
    if (!OM_is_present(bits))
        return 0;
    screen = OM_instantiate_pointers(screen_class, 4);
    if (!OM_is_present(screen))
        return 0;
    OM_increase_ref(screen);

    origin = OM_instantiate_pointers(ST_CLASS_POINT, 2);
    if (!OM_is_present(origin))
        return 0;
    OM_store_pointer(0, origin, OM_int_oop(0));
    OM_store_pointer(1, origin, OM_int_oop(0));

    OM_store_pointer(ST_FORM_BITS,   screen, bits);
    OM_store_pointer(ST_FORM_WIDTH,  screen, OM_int_oop((st_int) width));
    OM_store_pointer(ST_FORM_HEIGHT, screen, OM_int_oop((st_int) height));
    OM_store_pointer(ST_FORM_OFFSET, screen, origin);

    define_global("Display", screen);
    GFX_set_display(screen);
    return 1;
}

/*  ----------  Class-side initialisers  ----------  */

/*
 *  Look a selector up in one method dictionary, without walking superclasses.
 *
 *  Not walking is the point: Object class>>initialize would otherwise be
 *  found for every class in the image and run 226 times.  Only a class that
 *  DEFINES an initializer should get one.
 */
static st_oop
method_in_dictionary(st_oop dict, const char *selector)
{
    uint32_t    capacity;
    uint32_t    slot;
    st_oop      wanted;

    if (!OM_is_present(dict))
        return ST_OOP_INVALID;
    wanted   = BOOT_intern_symbol(selector, NULL);
    capacity = OM_method_dict_capacity(dict);
    for (slot = 0; slot < capacity; ++slot) {
        if (OM_method_dict_key(dict, slot) == wanted)
            return OM_method_dict_value(dict, slot);
    }
    return ST_OOP_INVALID;
}

/*
 *  The same, but walking the superclass chain -- an ordinary method lookup.
 *
 *  method_in_dictionary deliberately does not inherit, because an
 *  initializer must belong to the class that defines it.  Calling a method
 *  is the opposite case: Dictionary class does not define new:, it inherits
 *  it, and refusing to look up the chain simply fails to find it.
 */
static st_oop
lookup_in_chain(st_oop class_oop, const char *selector)
{
    unsigned    depth = 0;

    while (OM_is_present(class_oop) && depth++ < 64) {
        st_oop  found = method_in_dictionary(
                            OM_fetch_pointer(CLASS_METHOD_DICT, class_oop),
                            selector);

        if (OM_is_present(found))
            return found;
        class_oop = OM_fetch_pointer(CLASS_SUPERCLASS, class_oop);
    }
    return ST_OOP_INVALID;
}

/*
 *  Activate one method on one receiver and run it to completion.
 *
 *  A context whose sender is nil, exactly as the drivers build for a doIt,
 *  so a return with no sender simply stops the interpreter.
 */
static int
run_method_with(st_oop method, st_oop receiver, const st_oop *args,
                unsigned argc, uint64_t budget)
{
    st_oop      context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 64);
    unsigned    i;

    if (!OM_is_present(context))
        return 0;
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, method);
    OM_store_pointer(ST_CTX_RECEIVER, context, receiver);
    /*  Arguments are the first temporaries, which is where the frame keeps
     *  them and where the compiler numbers them from.  */
    for (i = 0; i < argc; ++i)
        OM_store_pointer(ST_CTX_TEMP_FRAME_START + i, context, args[i]);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int)
                        (BOOT_method_initial_ip(method) + 1)));
    /*  The stack begins ABOVE the temporaries, not at zero.  */
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, method))));

    memset(&st_vm, 0, sizeof st_vm);
    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    ST_interp_run(budget);
    return !st_vm.running;
}

static int
run_method_on(st_oop method, st_oop receiver, uint64_t budget)
{
    return run_method_with(method, receiver, NULL, 0, budget);
}

/*
 *  Give the library a symbol table holding the symbols we already interned.
 *
 *  Symbol class>>initialize cannot run in a new image: it interns 128
 *  one-character symbols, interning consults hasInterned:ifTrue:, and that
 *  reads the very table being built.  In a real 1983 image build the
 *  circularity never arises because the image is MUTATED from an older one
 *  that already had a table.  Building from nothing, we have to close the
 *  loop ourselves.
 *
 *  The tables are created here, but the entries are placed by sending the
 *  image's own intern: -- so the bucket is chosen by the library's
 *  String>>hash rather than by a copy of it in C that could drift.  intern:
 *  answers an argument that is already a Symbol unchanged, so the symbols
 *  the compiler put in the literal frames stay the identical objects and
 *  #foo == 'foo' asSymbol holds.
 */
#define USTABLE_BUCKETS 512


/*
 *  String>>hash, as the 1983 library computes it:
 *
 *      hash
 *          | l m |
 *          (l _ m _ self size) <= 2
 *            ifTrue: [l = 2 ifTrue: [m _ 3]
 *                     ifFalse: [l = 1
 *                                 ifTrue: [^((self at: 1) asciiValue
 *                                             bitAnd: 127) * 106].
 *                               ^21845]].
 *          ^(self at: 1) asciiValue * 48 + ((self at: (m - 1)) asciiValue + l)
 *
 *  Smalltalk indexes from one, so "self at: (m - 1)" is byte m - 2 here.
 */
/*  The same formula on plain text, for interning a name not yet a Symbol. */
uint32_t
BOOT_string_hash_of_text(const char *text, size_t length)
{
    size_t  m;

    if (length == 0)
        return 21845;
    if (length == 1)
        return (uint32_t) (text[0] & 127) * 106;
    m = (length == 2) ? 3 : length;
    return (uint32_t) (unsigned char) text[0] * 48
         + (uint32_t) (unsigned char) text[m - 2] + (uint32_t) length;
}

uint32_t
BOOT_string_hash(st_oop string)
{
    uint32_t    length;
    uint32_t    m;

    if (!OM_is_present(string))
        return 0;
    length = OM_fetch_byte_length(string);
    if (length == 0)
        return 21845;
    if (length == 1)
        return (uint32_t) (OM_fetch_byte(0, string) & 127) * 106;
    m = (length == 2) ? 3 : length;
    return (uint32_t) OM_fetch_byte(0, string) * 48
         + (uint32_t) OM_fetch_byte(m - 2, string) + length;
}

/*
 *  Add one symbol to the library's hash table, in the bucket the image's own
 *  String>>hash chooses.  Buckets are Arrays grown by copying, which is what
 *  Symbol class>>intern: does; there are 3601 symbols over 512 buckets, so
 *  the copies are of seven elements and the shape stays what the library
 *  expects rather than something faster that it cannot read.
 */
static int
place_in_symbol_table(st_oop sym)
{
    uint32_t    index;
    st_oop      bucket;
    uint32_t    n;
    st_oop      grown;
    uint32_t    k;

    if (!symbol_table_ready || !OM_is_present(symbol_table)
     || !OM_is_present(sym))
        return 0;
    index  = BOOT_string_hash(sym) % USTABLE_BUCKETS;
    bucket = OM_fetch_pointer(index, symbol_table);
    n      = OM_is_present(bucket) ? OM_fetch_word_length(bucket) : 0;
    for (k = 0; k < n; ++k) {
        if (OM_fetch_pointer(k, bucket) == sym)
            return 0;           /*  already there  */
    }
    grown = OM_instantiate_pointers(ST_CLASS_ARRAY, n + 1);
    if (!OM_is_present(grown))
        return 0;
    for (k = 0; k < n; ++k)
        OM_store_pointer(k, grown, OM_fetch_pointer(k, bucket));
    OM_store_pointer(n, grown, sym);
    OM_store_pointer(index, symbol_table, grown);
    return 1;
}

static int
seed_symbol_table(st_boot_init_report *out)
{
    boot_class *symbol = find_class("Symbol");
    st_oop      single;
    st_oop      table;
    st_oop      assoc;
    st_oop      intern;
    unsigned    i;

    if (!symbol || !OM_is_present(symbol->metaclass_oop))
        return 1;               /*  a kernel without Symbol: nothing to do */

    single = OM_instantiate_pointers(ST_CLASS_ARRAY, 128);
    table  = OM_instantiate_pointers(ST_CLASS_ARRAY, USTABLE_BUCKETS);
    if (!OM_is_present(single) || !OM_is_present(table))
        return 0;
    OM_increase_ref(single);
    OM_increase_ref(table);

    for (i = 0; i < 128; ++i) {
        char    text[2];

        text[0] = (char) i;
        text[1] = '\0';
        OM_store_pointer(i, single, BOOT_intern_symbol(text, NULL));
    }
    for (i = 0; i < USTABLE_BUCKETS; ++i) {
        st_oop  bucket = OM_instantiate_pointers(ST_CLASS_ARRAY, 0);

        if (!OM_is_present(bucket))
            return 0;
        OM_store_pointer(i, table, bucket);
    }

    assoc = class_variable_association(symbol, "SingleCharSymbols", 0);
    if (assoc != ST_OOP_INVALID)
        OM_store_pointer(ST_ASSOCIATION_VALUE, assoc, single);
    assoc = class_variable_association(symbol, "USTable", 0);
    if (assoc != ST_OOP_INVALID)
        OM_store_pointer(ST_ASSOCIATION_VALUE, assoc, table);
    symbol_table = table;

    /*  Now hand every symbol we interned to the library's own intern:.  */
    intern = method_in_dictionary(
                 OM_fetch_pointer(CLASS_METHOD_DICT, symbol->metaclass_oop),
                 "intern:");
    if (!OM_is_present(intern))
        return 1;
    out->symbols_total = symbol_count;

    symbol_table_ready = 1;
    /*
     *  Place every symbol directly, computing the bucket in C.
     *
     *  The obvious alternative -- sending the image's own intern: 3601 times
     *  -- was tried and abandoned: it added two and a half seconds to every
     *  bootstrap and still placed only a sixth of them.  So the hash is
     *  computed here instead, which duplicates String>>hash in C, which is
     *  exactly the sort of copy that drifts.  What makes it safe is that the
     *  copy is CHECKED: tests/unit/test_image.c asks the image for the hash
     *  of a sample of symbols and fails if this disagrees with any of them.
     *  A duplicate that is asserted equal is a cache; one that is trusted is
     *  a bug waiting for a rainy day.
     */
    for (i = 0; i < symbol_count; ++i) {
        if (place_in_symbol_table(symbols[i]))
            ++out->symbols_seeded;
    }
    return 1;
}

/*
 *  Make Smalltalk a real SystemDictionary holding the globals.
 *
 *  Until now Smalltalk has been a placeholder: the globals live in a C array
 *  and an Array of Associations, which the compiler can search but the image
 *  cannot.  A great deal of the library asks -- "Smalltalk at: #Foo put:",
 *  "Smalltalk includes:", "Smalltalk associationAt:" -- and Text class>>
 *  initialize is the first to be caught by it: finding no TextConstants in
 *  Smalltalk, it makes a fresh Dictionary and throws away the bindings every
 *  method compiled against.
 *
 *  The dictionary is built by the image, filled with the SAME Association
 *  objects the compiler handed out, and then swapped into the reserved
 *  pointer with become:.  Swapping is what become: is for, and it is why the
 *  object table earns its indirection: the interpreter names Smalltalk by a
 *  fixed pointer, and that pointer now names a real dictionary without
 *  anything else in the image having to be told.
 */
static int
install_system_dictionary(void)
{
    st_oop      cls = BOOT_global("SystemDictionary");
    st_oop      new_with;
    st_oop      add_to;
    st_oop      dict;
    st_oop      arg;
    unsigned    i;

    if (!OM_is_present(cls))
        cls = BOOT_global("Dictionary");
    if (!OM_is_present(cls))
        return 1;               /*  a kernel without either: nothing to do  */

    new_with = lookup_in_chain(OM_fetch_class(cls), "new:");
    add_to   = lookup_in_chain(cls, "add:");
    if (!OM_is_present(new_with) || !OM_is_present(add_to))
        return 1;

    /*  Room to spare: a hashed collection that fills up stops working.  */
    arg = OM_int_oop((st_int) (global_count * 4 + 64));
    if (!run_method_with(new_with, cls, &arg, 1, 2000000))
        return 1;
    dict = st_vm.return_value;
    if (!OM_is_present(dict))
        return 1;
    OM_increase_ref(dict);

    for (i = 0; i < global_count; ++i) {
        arg = OM_fetch_pointer(i, globals_values);
        if (OM_is_present(arg)) {
            if (getenv("ST_GLOBAL_LOG")) {
                char    cname[64];

                char    kname[64];

                OM_class_name_of(OM_fetch_class(arg), cname, sizeof cname);
                OM_string_of(OM_fetch_pointer(ST_ASSOCIATION_KEY, arg),
                             kname, sizeof kname);
                if (strcmp(cname, "Association") != 0)
                    fprintf(stderr, "  global %u \"%s\" has class %s\n",
                            i, kname, cname);
            }
            run_method_with(add_to, dict, &arg, 1, 2000000);
        }
    }

    /*
     *  The reserved pointer must end up naming the real dictionary, so the
     *  two identities are exchanged rather than the contents copied.
     */
    if (getenv("ST_BOOT_LOG"))
        fprintf(stderr, "  Smalltalk: %u globals into a %u-slot dictionary\n",
                global_count, (unsigned) OM_fetch_word_length(dict));
    OM_swap_identities(ST_SMALLTALK, dict);
    smalltalk = ST_SMALLTALK;
    define_global("Smalltalk", ST_SMALLTALK);
    if (getenv("ST_BOOT_LOG")) {
        char name[64];

        OM_class_name_of(OM_fetch_class(ST_SMALLTALK), name, sizeof name);
        fprintf(stderr, "  Smalltalk: now a %s of %u slots; global = %llu,"
                        " ST_SMALLTALK = %llu, nil = %llu\n",
                name, (unsigned) OM_fetch_word_length(ST_SMALLTALK),
                (unsigned long long) BOOT_global("Smalltalk"),
                (unsigned long long) ST_SMALLTALK,
                (unsigned long long) ST_NIL);
    }
    return 1;
}

/*
 *  Give the image its Sensor and its window scheduler.
 *
 *  Both are made once when an image is built and carried forward by every
 *  snapshot after: InputSensor class>>initSensor assigns Sensor, and
 *  ControlManager is what ScheduledControllers holds.  Neither exists in an
 *  image built from sources, so a great deal of the interface asks nil for
 *  the cursor position or for the active controller and stops there.
 *
 *  They are created by sending the library's own constructors, so whatever
 *  those do to set an object up is done.
 */
static int
install_user_interface(void)
{
    /*
     *  Initialisers that BOOT_run_initializers cannot reach.
     *
     *  Object class>>initialize asks the user to confirm before resetting
     *  every dependency in the system, which is sensible in a running image
     *  and impossible in one being built -- there is nobody to ask.  Its two
     *  halves are separate methods, so they are called directly.  Without
     *  DependentsFields, addDependent: sends at:ifAbsent: to nil, and
     *  nothing in MVC can register interest in a model.
     */
    static const struct { const char *class_name; const char *selector; }
    directly[] = {
        { "Object",      "initializeDependentsFields" },
        { "Object",      "initializeErrorRecursion" },
        { "InputSensor", "initMap" },
        /*
         *  Behavior class>>init, not initialize, so BOOT_run_initializers
         *  never finds it.  It makes the three dictionaries addSelector:
         *  consults on every method installed -- so compiling anything
         *  inside the image asks nil for a selector without them.
         */
        { "Behavior",    "init" }
    };
    static const struct {
        const char *class_name;
        const char *constructor;
        const char *global;
    } wanted[] = {
        { "InputSensor",    "new",  "Sensor" },
        { "ControlManager", "new",  "ScheduledControllers" },
        /*
         *  Where the compiler puts a name it does not know.  Compiling
         *  anything inside the image reaches Encoder>>declareUndeclared:,
         *  which asks Undeclared for the name -- and asks nil, if nothing
         *  has made one.
         */
        { "Dictionary",     "new",  "Undeclared" },
        /*  And where it records what it has changed.  */
        { "ChangeSet",      "new",  "SystemChanges" }
    };
    unsigned    i;

    for (i = 0; i < sizeof directly / sizeof directly[0]; ++i) {
        st_oop  cls = BOOT_global(directly[i].class_name);
        st_oop  m;

        if (!OM_is_present(cls))
            continue;
        m = lookup_in_chain(OM_fetch_class(cls), directly[i].selector);
        if (OM_is_present(m))
            run_method_on(m, cls, 4000000);
    }

    /*
     *  A Transcript to report into.
     *
     *  Everything that wants to say something says it here -- the compiler
     *  reports its errors to Transcript, so with none there a compile error
     *  becomes "nil does not understand #show" and the real message is lost.
     *
     *  Made by SENDING new, not by allocating three fields and filling one.
     *  The hand-built one had an entryStream and nothing else, so its
     *  inherited `contents' and `isLocked' stayed nil -- which is invisible
     *  until something asks, and the thing that asks is the Transcript
     *  WINDOW: StringHolderView>>model: does `self editString: self
     *  getContents', getContents is `^model contents', and nil does not
     *  understand #asText.  Choosing `system transcript' from the desktop
     *  menu was a notifier every time.
     *
     *  TextCollector>>initialize does super initialize -- isLocked false,
     *  contents the empty string -- and then beginEntry, which makes its own
     *  WriteStream on a String.  That is all three fields, set the way the
     *  image expects them, by the image's own code.
     */
    {
        st_oop  collector_class = BOOT_global("TextCollector");

        if (OM_is_present(collector_class)) {
            st_oop  make = lookup_in_chain(OM_fetch_class(collector_class),
                                           "new");

            if (OM_is_present(make)
             && run_method_on(make, collector_class, 4000000)
             && OM_is_present(st_vm.return_value)
             && st_vm.return_value != ST_NIL) {
                OM_increase_ref(st_vm.return_value);
                define_global("Transcript", st_vm.return_value);
            }
        }
    }

    for (i = 0; i < sizeof wanted / sizeof wanted[0]; ++i) {
        st_oop  cls = BOOT_global(wanted[i].class_name);
        st_oop  make;


        if (!OM_is_present(cls))
            continue;
        make = lookup_in_chain(OM_fetch_class(cls), wanted[i].constructor);
        if (!OM_is_present(make))
            continue;
        if (!run_method_on(make, cls, 4000000))
            continue;
        if (OM_is_present(st_vm.return_value)) {
            OM_increase_ref(st_vm.return_value);
            define_global(wanted[i].global, st_vm.return_value);
        }
    }

    /*
     *  Prime the Sensor's idea of the current cursor.
     *
     *  InputSensor and Cursor each declare a class variable called
     *  CurrentCursor -- they are different bindings, which is what the 1983
     *  sources say and not a mistake -- and InputSensor's begins nil.  The
     *  first thing currentCursor: does is ask the old cursor for its offset,
     *  so showing any cursor at all fails until one is already shown.  In an
     *  image built in 1983 that knot was tied when the image was made; here.
     */
    {
        boot_class *sensor = find_class("InputSensor");
        st_oop      cursor_class = BOOT_global("Cursor");

        if (sensor && OM_is_present(cursor_class)) {
            st_oop  normal = lookup_in_chain(OM_fetch_class(cursor_class),
                                             "normal");

            if (OM_is_present(normal)
             && run_method_on(normal, cursor_class, 2000000)
             && OM_is_present(st_vm.return_value)) {
                st_oop  binding = class_variable_association(sensor,
                                      "CurrentCursor", 0);

                if (binding != ST_OOP_INVALID)
                    OM_store_pointer(ST_ASSOCIATION_VALUE, binding,
                                     st_vm.return_value);
            }
        }
    }

    /*
     *  The System Workspace, which nothing in the sources ever creates.
     *
     *  StringHolder class>>workspace answers the class variable Workspace,
     *  and StringHolder class>>initialize -- the only method that mentions
     *  it -- is ENTIRELY a comment:
     *
     *      "The class variables were initialized once, and subsequently
     *      filled with information.  Re-executing this method is therefore
     *      dangerous.
     *
     *      workSpace  _ StringHolder new."
     *
     *  So it was filled by hand in 1983 and carried by every snapshot after,
     *  and an image built from these sources has it nil.  Choosing `system
     *  workspace' from the desktop menu then hands nil to StringHolderView
     *  and the notifier says "Message not understood: contents", which names
     *  the symptom and not one word of the cause.  Same family as Sensor,
     *  ScheduledControllers, Transcript and the top Project.
     *
     *  The text is OURS.  1983's System Workspace is a page of Xerox's prose
     *  and example expressions, and that is exactly the thing in this project
     *  that carries no licence -- so this one says what is true of THIS
     *  system instead, which is more use anyway.
     */
    {
        boot_class *holder = find_class("StringHolder");
        st_oop      cls = BOOT_global("StringHolder");
        st_oop      binding = holder ? class_variable_association(holder,
                                           "Workspace", 0) : ST_OOP_INVALID;
        st_oop      make;

        if (OM_is_present(cls) && binding != ST_OOP_INVALID) {
            make = lookup_in_chain(OM_fetch_class(cls), "new");
            if (OM_is_present(make) && run_method_on(make, cls, 4000000)
             && OM_is_present(st_vm.return_value)) {
                st_oop  workspace = st_vm.return_value;
                st_oop  text = BOOT_make_string(
        /*
         *  Carriage returns, not newlines.  A Smalltalk-80 line ends with
         *  Character cr, which is 13, and the composition scanner breaks on
         *  that and on nothing else -- a 10 is just another character with no
         *  glyph, so the whole thing composes as one long paragraph.
         */
        "Smalltalk-2026.\r"
        "\r"
        "Select an expression and choose `do it' or `print it' from the\r"
        "yellow button menu.\r"
        "\r"
        "3 + 4 factorial\r"
        "(1 to: 10) inject: 0 into: [:a :b | a + b]\r"
        "Smalltalk at: #Transcript\r"
        "\r"
        "Display extent\r"
        "Display fill: (40@40 corner: 200@120) rule: 3 mask: Form black\r"
        "ScheduledControllers restore\r"
        "\r"
        "BrowserView openOn: SystemOrganization\r"
        "StringHolderView open\r"
        "\r"
        "This system runs bytecodes on every core.  Read doc/CONCURRENCY.md\r"
        "before writing anything that forks:\r"
        "\r"
        "Processor activeProcess\r"
        "[Transcript show: 'from a process'; cr] fork\r", NULL);

                if (OM_is_present(text)) {
                    st_oop  setter = lookup_in_chain(OM_fetch_class(workspace),
                                                     "contents:");

                    if (OM_is_present(setter))
                        run_method_with(setter, workspace, &text, 1, 2000000);
                }
                OM_increase_ref(workspace);
                OM_store_pointer(ST_ASSOCIATION_VALUE, binding, workspace);
            }
        }
    }

    /*
     *  The top project, made AFTER the things it is supposed to hold.
     *
     *  Project class>>initialize runs with the other class initializers, and
     *  the method it calls captures
     *
     *      projectWindows _ ScheduledControllers
     *
     *  -- which at that point is nil, because ScheduledControllers is made a
     *  few lines above here and not before.  Project>>initialProject also
     *  never assigns projectTranscript at all: 1983 left the top project's
     *  transcript nil, there being no way to have entered it from anywhere.
     *
     *  Both bite the instant somebody chooses `exit project' from the
     *  desktop menu.  Project>>exit is `projectHolder enter', the top
     *  project holds ITSELF, and Project>>enter does
     *
     *      TextCollector newTranscript: projectTranscript.
     *      ControlManager newScheduler: projectWindows.
     *
     *  So exiting installs nil as the Transcript and nil as
     *  ScheduledControllers, and every process that then asks
     *  `ScheduledControllers activeController' or `Transcript cr' answers
     *  doesNotUnderstand -- for ever, several hundred times a second, with
     *  no way back.  The session is over, from one menu item.
     *
     *  So run the initializer again now that both exist, and fill in the
     *  transcript the 1983 method leaves out.  Exiting the top project then
     *  re-enters it, which is exactly what `projectHolder _ self' says it
     *  should do.
     */
    {
        boot_class *project = find_class("Project");
        st_oop      cls = BOOT_global("Project");

        if (project && OM_is_present(cls)) {
            st_oop  init = lookup_in_chain(OM_fetch_class(cls), "initialize");
            st_oop  current;

            if (OM_is_present(init))
                run_method_on(init, cls, 4000000);
            current = lookup_in_chain(OM_fetch_class(cls), "current");
            if (OM_is_present(current)
             && run_method_on(current, cls, 2000000)
             && OM_is_present(st_vm.return_value)
             && st_vm.return_value != ST_NIL) {
                st_oop      top = st_vm.return_value;
                st_oop      transcript = BOOT_global("Transcript");
                unsigned    i;

                for (i = 0; i < project->all_ivars.count; ++i) {
                    if (strcmp(project->all_ivars.items[i],
                               "projectTranscript") != 0)
                        continue;
                    if (OM_is_present(transcript))
                        OM_store_pointer(i, top, transcript);
                    break;
                }
            }
        }
    }
    return 1;
}

/*
 *  Wire the subclass graph.
 *
 *  Behavior has four instance variables -- superclass, methodDict, format,
 *  subclasses -- and the bootstrap filled the first three.  The fourth was
 *  left nil for every class in the image, so "Object subclasses" answered
 *  an empty Set, and with it allSubclasses, withAllSubclasses, and every
 *  piece of reflection that walks DOWN the hierarchy rather than up.
 *
 *  It is the same shape as the method-dictionary bug: the image looks
 *  somewhere the bootstrap never filled, and answers something empty rather
 *  than failing.  "TestCase allSubclasses" answering nothing at all, in an
 *  image with three TestCase subclasses in it, is what found this.
 *
 *  Done by sending addSubclass:, not by building Sets in C.  Behavior's own
 *  method makes the Set, checks the relationship, and hashes the entry the
 *  way the image hashes it -- three things that would each have to be
 *  reproduced here, and would each be a place to get it subtly wrong.
 */
static int
install_subclass_graph(void)
{
    st_oop      behavior = BOOT_global("Behavior");
    st_oop      add;
    unsigned    i;

    if (!OM_is_present(behavior))
        return 1;
    add = lookup_in_chain(behavior, "addSubclass:");
    if (!OM_is_present(add))
        return 1;

    for (i = 0; i < class_count; ++i) {
        boot_class *c = &classes[i];
        boot_class *super;
        st_oop      args[1];

        if (!OM_is_present(c->class_oop))
            continue;
        super = superclass_of(c);
        if (!super || !OM_is_present(super->class_oop))
            continue;
        args[0] = c->class_oop;
        run_method_with(add, super->class_oop, args, 1, 2000000);
        /*
         *  And the metaclass side, which has its own parallel chain:
         *  Foo class's superclass is Superclass class.
         */
        if (OM_is_present(c->metaclass_oop)
         && OM_is_present(super->metaclass_oop)) {
            args[0] = c->metaclass_oop;
            run_method_with(add, super->metaclass_oop, args, 1, 2000000);
        }
    }
    return 1;
}

/*
 *  Give every class an organization: its methods grouped by protocol.
 *
 *  The Browser's third pane lists them, and its fourth lists the selectors
 *  in the one chosen, so a class with no organization browses as empty however
 *  many methods it has.  Each class gets a ClassOrganizer and every selector
 *  is classified under the protocol its "methodsFor:" named.
 */
static int
install_class_organization(void)
{
    st_oop      org_class = BOOT_global("ClassOrganizer");
    st_oop      make;
    st_oop      classify;
    unsigned    i;

    if (!OM_is_present(org_class))
        return 1;
    make     = lookup_in_chain(OM_fetch_class(org_class), "new");
    classify = lookup_in_chain(org_class, "classify:under:");
    if (!OM_is_present(make) || !OM_is_present(classify))
        return 1;

    /*  One organizer per class and per metaclass, made on first use.  */
    for (i = 0; i < method_protocol_count; ++i) {
        boot_class *c = &classes[method_protocols[i].class_index];
        st_oop      target = method_protocols[i].class_side ? c->metaclass_oop
                                                            : c->class_oop;
        st_oop      organization;
        st_oop      args[2];

        if (!OM_is_present(target))
            continue;
        organization = OM_fetch_pointer(CLASS_ORGANIZATION, target);
        if (!OM_is_present(organization) || organization == ST_NIL) {
            if (!run_method_on(make, org_class, 2000000))
                continue;
            organization = st_vm.return_value;
            if (!OM_is_present(organization))
                continue;
            OM_store_pointer(CLASS_ORGANIZATION, target, organization);
        }
        args[0] = method_protocols[i].selector;
        args[1] = BOOT_make_string(method_protocols[i].protocol, NULL);
        run_method_with(classify, organization, args, 2, 2000000);
    }
    return 1;
}

/*
 *  Hand the collected sources to the image as SourceFiles.
 *
 *  CompiledMethod>>getSource reads a chunk from "SourceFiles at: n" at the
 *  position in its trailer, and RemoteString asks that stream only to
 *  position: and nextChunk.  A ReadWriteStream on a String answers both, so
 *  the sources need not be a file -- which is just as well, since this image
 *  has no file system yet.  Index 2 is the changes stream, empty and
 *  writable, which is where the image will put anything it compiles itself.
 */
static int
install_sources(void)
{
    st_oop      array_class = BOOT_global("Array");
    st_oop      stream_class = BOOT_global("ReadWriteStream");
    st_oop      string_class = BOOT_global("String");
    st_oop      files;
    st_oop      text;
    st_oop      arg;
    size_t      i;

    if (!OM_is_present(array_class) || !OM_is_present(stream_class)
     || !OM_is_present(string_class) || source_length == 0)
        return 1;

    /*
     *  Two entries unless a spill file was needed, which keeps the ordinary
     *  case exactly the shape the 1983 convention describes: 1 is .sources
     *  and 2 is .changes.
     */
    {
        unsigned    used = 2;
        unsigned    k;

        for (k = SOURCE_CHANGES + 1; k < SOURCE_FILES; ++k) {
            if (source_files[k].length)
                used = k + 1;
        }
        files = OM_instantiate_pointers(array_class, used);
    }
    if (!OM_is_present(files))
        return 0;
    OM_increase_ref(files);

    text = OM_instantiate_bytes(string_class, (uint32_t) source_length);
    if (!OM_is_present(text))
        return 0;
    for (i = 0; i < source_length; ++i)
        OM_store_byte((uint32_t) i, text, (uint8_t) source_text[i]);

    {
        /*
         *  "with:" rather than "on:": on: positions at the start and treats
         *  the collection as empty to be written over, which is right for
         *  the changes stream and wrong for one that already holds every
         *  method in the system.
         */
        st_oop  with = lookup_in_chain(OM_fetch_class(stream_class), "with:");
        st_oop  on = lookup_in_chain(OM_fetch_class(stream_class), "on:");
        st_oop  empty;

        if (!OM_is_present(on) || !OM_is_present(with))
            return 1;
        arg = text;
        if (!run_method_with(with, stream_class, &arg, 1, 4000000)
         || !OM_is_present(st_vm.return_value))
            return 1;
        OM_store_pointer(0, files, st_vm.return_value);

        empty = OM_instantiate_bytes(string_class, 0);
        arg = empty;
        if (run_method_with(on, stream_class, &arg, 1, 4000000)
         && OM_is_present(st_vm.return_value))
            OM_store_pointer(SOURCE_CHANGES, files, st_vm.return_value);

        /*  Any spill file gets a stream of its own, read the same way.  */
        {
            unsigned    k;

            for (k = SOURCE_CHANGES + 1; k < SOURCE_FILES; ++k) {
                st_oop      more;
                size_t      j;

                if (!source_files[k].length)
                    continue;
                more = OM_instantiate_bytes(string_class,
                                    (uint32_t) source_files[k].length);
                if (!OM_is_present(more))
                    return 0;
                for (j = 0; j < source_files[k].length; ++j)
                    OM_store_byte((uint32_t) j, more,
                                  (uint8_t) source_files[k].text[j]);
                arg = more;
                if (run_method_with(with, stream_class, &arg, 1, 4000000)
                 && OM_is_present(st_vm.return_value))
                    OM_store_pointer(k, files, st_vm.return_value);
            }
        }
    }
    define_global("SourceFiles", files);
    return 1;
}

/*
 *  Build SystemOrganization, the map from class categories to classes.
 *
 *  The Browser opens on it -- "BrowserView openOn: SystemOrganization" -- so
 *  without it there is nothing to browse.  Every class definition names its
 *  category, so the information has been going past all along; it is
 *  collected here and handed to the library's own organizer with
 *  classify:under:, which is how a class says where it belongs.
 */
static int
install_system_organization(void)
{
    st_oop      org_class = BOOT_global("SystemOrganizer");
    st_oop      make;
    st_oop      classify;
    st_oop      organization;
    unsigned    i;

    if (!OM_is_present(org_class))
        return 1;
    make = lookup_in_chain(OM_fetch_class(org_class), "new");
    if (!OM_is_present(make) || !run_method_on(make, org_class, 4000000))
        return 1;
    organization = st_vm.return_value;
    if (!OM_is_present(organization))
        return 1;
    OM_increase_ref(organization);

    classify = lookup_in_chain(org_class, "classify:under:");
    if (OM_is_present(classify)) {
        for (i = 0; i < class_count; ++i) {
            st_oop  args[2];

            if (!classes[i].category[0])
                continue;
            args[0] = BOOT_intern_symbol(classes[i].name, NULL);
            args[1] = BOOT_make_string(classes[i].category, NULL);
            run_method_with(classify, organization, args, 2, 2000000);
        }
    }
    define_global("SystemOrganization", organization);
    return 1;
}

/*
 *  Build the process scheduler and the process the image starts in.
 *
 *  ProcessorScheduler class>>new refuses on purpose -- "the integrity of the
 *  system depends on a unique scheduler" -- because in 1983 the one scheduler
 *  was made when the image was built and has been carried by every snapshot
 *  since.  Building from sources, this is where it is made.
 *
 *  The scheduler needs a list per priority and one runnable process.  That
 *  process's suspended context is a method compiled here from source, so what
 *  the image does when it starts is a string rather than something baked into
 *  C -- which is how a Smalltalk decides what to do on waking up.
 */
/*
 *  Hand the image the class variables the 1983 sources never assign.
 *
 *  SystemDictionary's SpecialSelectors is one, and it is not an oversight in
 *  the sources: a 1983 image was built from an older image that already had
 *  it, so nothing in the text ever needs to say what it is.  Built from
 *  nothing, it stays nil, and nothing complains -- it is read by
 *  "Smalltalk specialSelectorSize", which answers "nil size // 2" and
 *  therefore fails quietly wherever a size of zero is an acceptable answer.
 *
 *  What reads it is the compiler.  VariableNode class>>initialize builds
 *  StdSelectors from it, and StdSelectors is how the image decides that "+"
 *  is bytecode 176 rather than an ordinary send of a literal selector.  With
 *  it nil, every method compiled INSIDE the image came out in a different
 *  and larger dialect than the same source compiled in C -- which is exactly
 *  the disagreement the self-hosting check exists to catch.
 *
 *  The VM has the table already, because the interpreter needs it to run
 *  bytecodes 176 to 207 at all.  The image is given that same object rather
 *  than a copy, so the two cannot drift apart.
 */
static void
install_special_selectors(void)
{
    boot_class *dict = find_class("SystemDictionary");
    st_oop      assoc;

    if (!dict)
        return;
    assoc = class_variable_association(dict, "SpecialSelectors", 0);
    if (assoc == ST_OOP_INVALID || !OM_is_present(assoc))
        return;
    OM_store_pointer(ST_ASSOCIATION_VALUE, assoc, ST_SPECIAL_SELECTORS);
}

/*
 *  Give every class and metaclass a method dictionary, even an empty one.
 *
 *  One was made when the first method was installed, so a class with no
 *  methods on a side -- and most classes have no class-side methods at all
 *  -- was left with nil there.  The interpreter does not mind: lookup tests
 *  for a dictionary before walking it and moves on up the chain.
 *
 *  Everything in the image does mind.  Behavior>>selectors is
 *  "^methodDict keys", so it is a doesNotUnderstand on nil, and the Browser
 *  asks for exactly that as soon as the class side of such a class is
 *  selected.  An empty dictionary answers an empty set and the Browser shows
 *  an empty list, which is the truth.
 */
static void
install_empty_method_dictionaries(void)
{
    unsigned    i;

    for (i = 0; i < class_count; ++i) {
        boot_class *c = &classes[i];
        unsigned    side;

        for (side = 0; side < 2; ++side) {
            st_oop  target = side ? c->metaclass_oop : c->class_oop;
            st_oop  dict;

            if (!OM_is_present(target))
                continue;
            dict = OM_fetch_pointer(CLASS_METHOD_DICT, target);
            if (dict == ST_NIL || !OM_is_object(dict))
                OM_store_pointer(CLASS_METHOD_DICT, target,
                                 make_method_dictionary(4));
        }
    }
}

/*
 *  Give every class a classPool holding its class variables.
 *
 *  A class variable is reached two ways.  A method already compiled holds
 *  the Association itself in its literal frame and reads its value field, so
 *  everything the C compiler built works whether or not any dictionary
 *  exists.  A method compiled LATER has to find the binding by name, and the
 *  only place to look is the class's classPool -- Encoder>>lookupInPools:
 *  asks each class up the chain for one.
 *
 *  Ours was nil on every class, so the image could compile a method that
 *  named a class variable and would quietly bind it to nil instead.  The
 *  method compiled, ran, and answered the wrong thing.  That is what the
 *  Browser does every time someone accepts a method, and roughly a third of
 *  the 1983 library names a class variable somewhere.
 *
 *  Filled here rather than as each binding is made, because the bindings are
 *  created lazily while methods compile and are only all present afterwards.
 */
static void
install_class_pools(void)
{
    st_oop      dict_class = BOOT_global("Dictionary");
    st_oop      new_with;
    st_oop      add_to;
    unsigned    i;

    if (!OM_is_present(dict_class))
        return;
    new_with = lookup_in_chain(OM_fetch_class(dict_class), "new:");
    add_to   = lookup_in_chain(dict_class, "add:");
    if (!OM_is_present(new_with) || !OM_is_present(add_to))
        return;

    for (i = 0; i < class_count; ++i) {
        boot_class *c = &classes[i];
        st_oop      pool;
        st_oop      arg;
        unsigned    k;
        unsigned    present = 0;

        if (!OM_is_present(c->class_oop) || c->cvars.count == 0)
            continue;
        for (k = 0; k < c->cvars.count; ++k)
            if (c->cvar_assoc[k] != 0)
                ++present;
        if (present == 0)
            continue;

        /*  Room to spare: a hashed collection that fills up stops working. */
        arg = OM_int_oop((st_int) (c->cvars.count * 4 + 8));
        if (!run_method_with(new_with, dict_class, &arg, 1, 2000000))
            continue;
        pool = st_vm.return_value;
        if (!OM_is_present(pool))
            continue;

        for (k = 0; k < c->cvars.count; ++k) {
            st_oop  arg2;

            if (c->cvar_assoc[k] == 0)
                continue;
            /*
             *  add:, so the dictionary holds the very Association the
             *  compiled methods already point at.  at:put: would make a
             *  SECOND binding of the same name, and then a method compiled
             *  now and a method compiled at bootstrap would be reading and
             *  writing two different variables that happened to be spelled
             *  alike.  Which is worse than not finding it at all.
             */
            arg2 = c->cvar_assoc[k];
            run_method_with(add_to, pool, &arg2, 1, 2000000);
        }
        OM_store_pointer(CLASS_POOL, c->class_oop, pool);
    }
}

/*
 *  The scheduler object itself, and the name Processor for it.
 *
 *  Separated from the startup process because the class initializers need
 *  it: Delay class>>initialize forks its timing process at Processor
 *  timingPriority, so with no Processor it stopped there and the image was
 *  built with no timing process at all -- every Delay would have waited
 *  forever.  It asked nil for a priority and said so, which is the only
 *  sign there was.
 *
 *  Idempotent, because both the initializers and BOOT_install_scheduler ask
 *  for it and neither knows which of them is first.
 */
static st_oop
install_processor_object(void)
{
    st_oop      sched_class = BOOT_global("ProcessorScheduler");
    st_oop      process_class = BOOT_global("Process");
    st_oop      list_class = BOOT_global("LinkedList");
    st_oop      array_class = BOOT_global("Array");
    st_oop      existing = OM_fetch_pointer(ST_ASSOCIATION_VALUE,
                                            ST_SCHEDULER_ASSOCIATION);
    st_oop      scheduler;
    st_oop      lists;
    st_oop      process;
    unsigned    i;

    if (OM_is_present(existing))
        return existing;
    if (!OM_is_present(sched_class) || !OM_is_present(process_class)
     || !OM_is_present(list_class) || !OM_is_present(array_class))
        return ST_NIL;

    /*  Eight priorities, 1 to 8, each with an empty list of waiters.  */
    lists = OM_instantiate_pointers(array_class, 8);
    if (!OM_is_present(lists))
        return ST_NIL;
    for (i = 0; i < 8; ++i) {
        /*
         *  Built here rather than with LinkedList new.  An empty list is two
         *  nil fields, and going through the image for it means going
         *  through Behavior>>new and whatever else is not ready yet.
         */
        st_oop  list = OM_instantiate_pointers(list_class, 2);

        if (!OM_is_present(list))
            return ST_NIL;
        OM_store_pointer(i, lists, list);
    }

    scheduler = OM_instantiate_pointers(sched_class, 2);
    if (!OM_is_present(scheduler))
        return ST_NIL;
    OM_increase_ref(scheduler);
    OM_store_pointer(ST_SCHEDULER_PROCESS_LISTS, scheduler, lists);

    /*
     *  A process to be active, before there is anything for one to do.
     *
     *  Processor activePriority is activeProcess priority, and an initializer
     *  that forks asks for it.  At the top priority the forks can only queue,
     *  which is what an image being built wants: nothing runs until it is
     *  resumed with a real process in BOOT_install_scheduler.
     */
    process = OM_instantiate_pointers(process_class, 4);
    if (!OM_is_present(process))
        return ST_NIL;
    OM_increase_ref(process);
    OM_store_pointer(ST_PROCESS_PRIORITY, process, OM_int_oop(8));
    OM_store_pointer(ST_PROCESS_MY_LIST, process, ST_NIL);
    OM_store_pointer(ST_SCHEDULER_ACTIVE_PROCESS, scheduler, process);

    OM_store_pointer(ST_ASSOCIATION_VALUE, ST_SCHEDULER_ASSOCIATION, scheduler);
    define_global("Processor", scheduler);
    return scheduler;
}

int
BOOT_install_scheduler(const char *startup_source)
{
    st_oop      process_class = BOOT_global("Process");
    st_oop      scheduler = install_processor_object();
    st_oop      process;
    st_oop      context;
    st_compile_context  ctx;
    st_compile_result   res;
    char        source[2048];

    if (!OM_is_present(scheduler) || !OM_is_present(process_class)) {
        fprintf(stderr, "st80: no scheduler: Processor=%d Process=%d\n",
                (int) OM_is_present(scheduler),
                (int) OM_is_present(process_class));
        return 1;
    }

    /*  The startup method, compiled from source like any other.  */
    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_array         = BOOT_make_array;
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;
    snprintf(source, sizeof source, "startUp %s", startup_source);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        boot_fail("cannot compile the startup: %s", res.error);
        return 0;
    }
    OM_increase_ref(res.method);

    context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT,
                                      ST_LARGE_CONTEXT_SLOTS);
    if (!OM_is_present(context))
        return 0;
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, res.method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int)
                        (BOOT_method_initial_ip(res.method) + 1)));
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, res.method))));

    process = OM_instantiate_pointers(process_class, 4);
    if (!OM_is_present(process))
        return 0;
    OM_increase_ref(process);
    OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, process, context);
    OM_store_pointer(ST_PROCESS_MY_LIST, process, ST_NIL);
    /*
     *  Highest priority for now, and its real one further down.
     *
     *  What follows resumes processes, and resuming one that outranks the
     *  active process transfers to it -- which here would nominate a process
     *  no interpreter is running to collect, and leave this one both active
     *  and on a run queue.  At the top priority every resume can only queue.
     */
    OM_store_pointer(ST_PROCESS_PRIORITY, process, OM_int_oop(8));
    OM_store_pointer(ST_SCHEDULER_ACTIVE_PROCESS, scheduler, process);

    /*
     *  InputSensor class>>install makes the InputState that reads the
     *  hardware, keeps it in a class variable, forks the process that drains
     *  the event queue, and hands the VM the semaphore to signal -- which is
     *  what primitive 93 is for, and the only way a key or a mouse button
     *  ever reaches the image.
     *
     *  It runs here, and not with the other initialisers, because all three
     *  of those steps need a scheduler: the process it forks takes its
     *  priority from Processor activePriority, so with no Processor -- or a
     *  Processor with no active process -- the method stops at its fourth
     *  line and the semaphore is never installed.  Nothing announces that;
     *  input simply never arrives, which is a hard thing to go looking for.
     */
    {
        st_oop  sensor_class = BOOT_global("InputSensor");

        if (OM_is_present(sensor_class)) {
            st_oop  m = lookup_in_chain(OM_fetch_class(sensor_class),
                                        "install");

            if (OM_is_present(m))
                run_method_on(m, sensor_class, 4000000);
        }
    }

    /*  Its real priority: the one user code runs at.  */
    OM_store_pointer(ST_PROCESS_PRIORITY, process, OM_int_oop(4));
    return 1;
}

/*
 *  Build the system font and text style.
 *
 *  A StrikeFont is one Form holding every glyph side by side, plus an xTable
 *  giving where each starts: the width of a character is the distance to the
 *  next one, which is why the table has an entry past the last glyph.  Every
 *  glyph here is the same width, so the table is arithmetic -- but the shape
 *  is the 1983 one, so the scanning primitive and CharacterScanner's own
 *  Smalltalk both read it without knowing that.
 *
 *  Slots are reserved for all 128 codes rather than only the printable ones,
 *  so an ascii out of range indexes a blank rather than off the end.
 */
#define FONT_CODES      ST_FONT_CODES
#define FONT_STRIKE_W   ST_FONT_STRIKE_WIDTH

static st_oop
build_strike_font(void)
{
    st_oop      font_class = BOOT_global("StrikeFont");
    st_oop      form_class = BOOT_global("Form");
    st_oop      word_array = BOOT_global("WordArray");
    st_oop      array_class = BOOT_global("Array");
    st_oop      glyphs;
    st_oop      bits;
    st_oop      xtable;
    st_oop      font;
    st_oop      origin;
    uint32_t    raster = FONT_STRIKE_W / 16;
    unsigned    row;
    unsigned    i;

    if (!OM_is_present(font_class) || !OM_is_present(form_class)
     || !OM_is_present(word_array) || !OM_is_present(array_class))
        return ST_OOP_INVALID;

    bits = OM_instantiate_words(word_array, raster * ST_FONT_HEIGHT);
    if (!OM_is_present(bits))
        return ST_OOP_INVALID;

    /*
     *  The strike goes across as it is: two bytes to a word, the first of
     *  them the high half, because bit 15 of a word is its leftmost pixel
     *  and so is bit 7 of the byte the generator wrote.
     */
    for (row = 0; row < ST_FONT_HEIGHT; ++row)
        for (i = 0; i < raster; ++i)
            OM_store_word(row * raster + i, bits,
                          (uint16_t) ((ST_FONT_STRIKE[row][i * 2] << 8)
                                      | ST_FONT_STRIKE[row][i * 2 + 1]));

    origin = OM_instantiate_pointers(ST_CLASS_POINT, 2);
    if (!OM_is_present(origin))
        return ST_OOP_INVALID;
    OM_store_pointer(0, origin, OM_int_oop(0));
    OM_store_pointer(1, origin, OM_int_oop(0));

    glyphs = OM_instantiate_pointers(form_class, 4);
    if (!OM_is_present(glyphs))
        return ST_OOP_INVALID;
    OM_store_pointer(ST_FORM_BITS,   glyphs, bits);
    OM_store_pointer(ST_FORM_WIDTH,  glyphs, OM_int_oop(FONT_STRIKE_W));
    OM_store_pointer(ST_FORM_HEIGHT, glyphs, OM_int_oop(ST_FONT_HEIGHT));
    OM_store_pointer(ST_FORM_OFFSET, glyphs, origin);

    /*
     *  One entry per code and one past the end, which is what a proportional
     *  strike needs -- StrikeFont>>widthOf: answers
     *
     *      (xTable at: ascii + 2) - (xTable at: ascii + 1)
     *
     *  -- and then ONE more, because that method first does
     *  `ascii min: maxAscii + 1', so a character past the end asks for entry
     *  maxAscii + 3.  The generator's table stops at the honest end; the
     *  extra entry repeats it, which makes that character zero wide rather
     *  than a read off the end of the array.
     */
    xtable = OM_instantiate_pointers(array_class, ST_FONT_CODES + 2);
    if (!OM_is_present(xtable))
        return ST_OOP_INVALID;
    for (i = 0; i < ST_FONT_CODES + 2; ++i)
        OM_store_pointer(i, xtable,
                         OM_int_oop((st_int) ST_FONT_XTABLE[
                             i <= ST_FONT_CODES ? i : ST_FONT_CODES]));

    font = OM_instantiate_pointers(font_class, 16);
    if (!OM_is_present(font))
        return ST_OOP_INVALID;
    /*  xTable glyphs name stopConditions type minAscii maxAscii maxWidth
     *  strikeLength ascent descent xOffset raster subscript superscript
     *  emphasis  */
    OM_store_pointer(0,  font, xtable);
    OM_store_pointer(1,  font, glyphs);
    OM_store_pointer(2,  font, BOOT_make_string("Smalltalk2026", NULL));
    {
        /*
         *  The scanner's stop-condition table, indexed by character code and
         *  by two values past the end: EndOfRun is 257 and CrossedX is 258,
         *  which is why it is not merely 256 long.  CharacterScanner>>
         *  setStopConditions fills it; it only has to exist, and be big
         *  enough that filling it does not run off the end.
         */
        st_oop  stops = OM_instantiate_pointers(array_class, 258);

        if (!OM_is_present(stops))
            return ST_OOP_INVALID;
        OM_store_pointer(3, font, stops);
    }
    OM_store_pointer(4,  font, OM_int_oop(0));  /*  type                      */
    OM_store_pointer(5,  font, OM_int_oop(0));  /*  minAscii                  */
    OM_store_pointer(6,  font, OM_int_oop(FONT_CODES - 1));
    OM_store_pointer(7,  font, OM_int_oop(ST_FONT_MAX_WIDTH));
    OM_store_pointer(8,  font, OM_int_oop(FONT_STRIKE_W));
    /*
     *  The face's own metrics, and the whole of the system's text layout:
     *  TextStyle>>gridForFont:withLead: answers `font height + lead' for the
     *  line grid and `font ascent' for the baseline, and Font>>height is
     *  ascent + descent.  Guessing them here is how text ends up squashed.
     */
    OM_store_pointer(9,  font, OM_int_oop(ST_FONT_ASCENT));
    OM_store_pointer(10, font, OM_int_oop(ST_FONT_DESCENT));
    OM_store_pointer(11, font, OM_int_oop(0));                   /*  xOffset  */
    OM_store_pointer(12, font, OM_int_oop((st_int) raster));
    OM_store_pointer(13, font, OM_int_oop(0));
    OM_store_pointer(14, font, OM_int_oop(0));
    OM_store_pointer(15, font, OM_int_oop(0));
    OM_increase_ref(font);
    return font;
}

/*
 *  Give the image a font and a default text style.
 *
 *  TextStyle class>>fontArray: is the library's own constructor, so the
 *  style is built the way the library expects rather than field by field.
 */
static int
install_text_style(void)
{
    st_oop      font = build_strike_font();
    st_oop      style_class = BOOT_global("TextStyle");
    st_oop      array_class = BOOT_global("Array");
    st_oop      font_array;
    unsigned    i;
    st_oop      make;
    st_oop      arg;
    st_oop      style;

    if (!OM_is_present(font) || !OM_is_present(style_class)
     || !OM_is_present(array_class))
        return 1;
    define_global("DefaultFont", font);

    /*
     *  Four faces: basal, bold, italic, bold italic.
     *
     *  They draw the same glyphs -- there is one face here, not four -- but
     *  they must be four distinct StrikeFonts whose emphasis fields differ,
     *  because that is how the library tells them apart.  Text>>
     *  makeSelectorBoldIn: asks the style for the bold face of the current
     *  one and gets nil if no font claims to be bold, and then sends it
     *  #name.  Sharing one object four times looks tidier and does not work.
     */
    font_array = OM_instantiate_pointers(array_class, 4);
    if (!OM_is_present(font_array))
        return 1;
    OM_store_pointer(0, font_array, font);
    for (i = 1; i < 4; ++i) {
        st_oop      face = OM_instantiate_pointers(OM_fetch_class(font), 16);
        unsigned    k;

        if (!OM_is_present(face))
            return 1;
        for (k = 0; k < 16; ++k)
            OM_store_pointer(k, face, OM_fetch_pointer(k, font));
        OM_store_pointer(15, face, OM_int_oop((st_int) i));   /*  emphasis  */
        OM_increase_ref(face);
        OM_store_pointer(i, font_array, face);
    }

    make = lookup_in_chain(OM_fetch_class(style_class), "fontArray:");
    if (!OM_is_present(make))
        return 1;
    arg = font_array;
    if (!run_method_with(make, style_class, &arg, 1, 4000000))
        return 1;
    style = st_vm.return_value;
    if (!OM_is_present(style))
        return 1;
    OM_increase_ref(style);
    define_global("DefaultTextStyle", style);

    /*
     *  And make the style fit the face.
     *
     *  TextStyle>>newFontArray: takes its line grid and baseline from
     *  TextConstants -- DefaultLineGrid 16 and DefaultBaseline 12, which
     *  Text class>>initialize sets and which are 1983's numbers for 1983's
     *  font.  Any face taller than sixteen rows is then CLIPPED by the
     *  default style, and the baseline lands six rows above where the glyphs
     *  were drawn: a bare `Hi' asParagraph lost the dot off its i and a row
     *  off its H, silently, because the strike is fine and only the frame
     *  around it is wrong.
     *
     *  Lists escaped it because TextList class>>initialize does this for
     *  itself.  So do it here, for the style everything else copies, with
     *  the image's own method rather than by assigning the two fields --
     *  gridForFont:withLead: is what a list does and what the numbers mean.
     */
    {
        st_oop  grid = lookup_in_chain(OM_fetch_class(style),
                                       "gridForFont:withLead:");
        st_oop  args[2];

        args[0] = OM_int_oop(1);
        args[1] = OM_int_oop(0);
        if (OM_is_present(grid))
            run_method_with(grid, style, args, 2, 2000000);
    }
    return 1;
}

/*
 *  Build the pool dictionaries and fill them with the bindings the compiler
 *  already handed out.
 *
 *  The dictionary is made by the image, with Dictionary new:, and each
 *  binding is put in with Dictionary>>add:, which stores the Association it
 *  is given rather than making one of its own.  Doing it through the image
 *  rather than by hand in C means the hashing is the library's, so the
 *  pool's own at: finds what we put there.
 */
static int
install_pools(void)
{
    st_oop      dict_class = BOOT_global("Dictionary");
    st_oop      new_with;
    st_oop      add_to;
    unsigned    i;
    unsigned    k = 0;

    if (!OM_is_present(dict_class))
        return 1;               /*  a kernel without Dictionary: nothing to do */
    new_with = lookup_in_chain(OM_fetch_class(dict_class), "new:");
    add_to   = lookup_in_chain(dict_class, "add:");
    if (getenv("ST_BOOT_LOG"))
        fprintf(stderr, "  pools: %u bindings, new:=%d add:=%d\n",
                pool_binding_count, (int) OM_is_present(new_with),
                (int) OM_is_present(add_to));
    if (!OM_is_present(new_with) || !OM_is_present(add_to))
        return 1;

    for (i = 0; i < pool_binding_count; ++i) {
        const char *name = pool_bindings[i].pool;
        st_oop      pool = BOOT_global(name);
        st_oop      arg;

        /*  The pool itself is a global; make it the first time it is named. */
        if (!OM_is_present(pool)) {
            arg = OM_int_oop(64);
            if (!run_method_with(new_with, dict_class, &arg, 1, 2000000))
                continue;
            pool = st_vm.return_value;
            if (!OM_is_present(pool))
                continue;
            define_global(name, pool);
        }
        arg = pool_bindings[i].association;
        if (run_method_with(add_to, pool, &arg, 1, 2000000))
            ++k;
    }
    if (getenv("ST_BOOT_LOG"))
        fprintf(stderr, "  pools: %u of %u bindings added\n", k,
                pool_binding_count);
    return 1;
}

/*
 *  Send #initialize to every class that defines one.
 *
 *  Called twice, with the text style built in between, because the order is
 *  forced from both ends: TextStyle cannot be built until Text class>>
 *  initialize has filled TextConstants, and TextList class>>initialize wants
 *  "DefaultTextStyle copy" -- so whichever runs first, something it needs is
 *  nil.  A second pass settles it, and these methods are written to be
 *  re-run: each carries its own "Text initialize" in a comment, which is an
 *  invitation to do exactly that.
 */
/*
 *  Initializers that must not be run, and why each one.
 *
 *  Every entry here was found by running it: each raised an error whose real
 *  cause was that the method cannot work in an image being built, and each
 *  is already served some other way.  Running them anyway achieved nothing
 *  and reported a fault in the wrong place -- two of them by way of
 *  Object>>error:, which draws its message at Sensor cursorPoint and so
 *  failed a second time on the way to complaining about the first.
 */
static const struct { const char *class_name; const char *why; }
never_initialize[] = {
    /*
     *  Asks the user to confirm resetting every dependency in the system.
     *  There is nobody to ask, so confirm: reaches a nil Sensor.  Its two
     *  halves -- initializeDependentsFields and initializeErrorRecursion --
     *  are called directly instead, which is the whole of what a yes does.
     */
    { "Object", "asks the user to confirm; its two halves are called directly" },
    /*
     *  Interns 128 one-character symbols, and interning reads the very table
     *  it is building.  seed_symbol_table closes that circle in C.  The
     *  method also cannot run as written: it does "a at: 1 put: i - 1" on a
     *  String, and String>>at:put: takes a Character, so the 1983 source is
     *  itself at fault here.
     */
    { "Symbol", "the symbol table is seeded in C; see seed_symbol_table" },
    /*
     *  Searches for primes with good hashing properties, and does not
     *  finish: two hundred million bytecodes is not enough to get through
     *  the search under this interpreter, so `sizes' stayed nil, sizeFor:
     *  answered nil, and every collection made through new: was built with
     *  a nil capacity.  lib/Collections-Compat writes the table down
     *  instead -- a constant that takes forty seconds to recompute on every
     *  image build is a constant, not a computation -- so nothing consults
     *  this class and running it would only cost the forty seconds again.
     */
    { "HashTableSizes", "the size table is a literal in "
                        "lib/Collections-Compat; see sizeFor:" },
    /*
     *  Reads its button images from Xerox .form files with Form class>>
     *  readFrom:.  There is no file system here and those files are not
     *  ours to ship, so the read cannot succeed.  The class works; its
     *  cached menu images are absent.
     */
    { "FormMenuView", "reads Xerox .form files that are not shipped" }
};

static int
skip_initializer(const char *class_name, const char **why)
{
    unsigned    i;

    for (i = 0; i < sizeof never_initialize / sizeof never_initialize[0]; ++i) {
        if (strcmp(never_initialize[i].class_name, class_name) == 0) {
            *why = never_initialize[i].why;
            return 1;
        }
    }
    return 0;
}

static void
run_class_initializers(st_boot_init_report *out)
{
    unsigned    i;

    out->defined = 0;
    out->ran = 0;
    out->unfinished = 0;
    out->unfinished_names[0] = '\0';
    out->skipped = 0;
    for (i = 0; i < class_count; ++i) {
        boot_class *c = &classes[i];
        st_oop      dict;
        st_oop      method;
        const char *why;

        if (!OM_is_present(c->metaclass_oop))
            continue;
        dict   = OM_fetch_pointer(CLASS_METHOD_DICT, c->metaclass_oop);
        method = method_in_dictionary(dict, "initialize");
        if (!OM_is_present(method))
            continue;

        ++out->defined;
        if (skip_initializer(c->name, &why)) {
            ++out->skipped;
            if (getenv("ST_BOOT_LOG"))
                fprintf(stderr, "  %s class>>initialize skipped: %s\n",
                        c->name, why);
            continue;
        }
        if (run_method_on(method, c->class_oop, 20000000)) {
            ++out->ran;
        }  else  {
            if (!out->first_unfinished[0])
                snprintf(out->first_unfinished,
                         sizeof out->first_unfinished, "%.63s", c->name);
            {
                size_t  used = strlen(out->unfinished_names);

                if (used + strlen(c->name) + 2 < sizeof out->unfinished_names)
                    snprintf(out->unfinished_names + used,
                             sizeof out->unfinished_names - used, "%s%s",
                             used ? " " : "", c->name);
            }
            ++out->unfinished;
        }
        /*
         *  Each initializer's working objects are unreachable once it
         *  returns, and there are dozens of them.  Collecting between keeps
         *  a runaway from starving the ones that follow.
         */
        OM_collect();
    }
}

int
BOOT_run_initializers(st_boot_init_report *out)
{
    memset(out, 0, sizeof *out);
    if (!seed_symbol_table(out))
        return -1;
    if (!install_system_dictionary())
        return -1;
    if (!install_pools())
        return -1;

    /*
     *  The first pass is expected to fail in places, so it says nothing.
     *
     *  Roughly a third of the initializers want a text style, and the style
     *  cannot be built until Text class>>initialize has filled
     *  TextConstants -- which is itself one of the initializers.  So the
     *  first pass runs to fill the constants, the style is built, and the
     *  second pass runs with everything the first was missing.
     *
     *  Reporting the first pass meant printing ten failures that the second
     *  pass then fixed, which is worse than saying nothing: a real fault had
     *  to be picked out of a list of ten that did not matter, and the input
     *  semaphore hid in exactly that list for months.  Only failures that
     *  survive the last pass are printed now.
     */
    /*
     *  A Processor before anything asks for one -- Delay class>>initialize
     *  forks its timing process at Processor timingPriority.
     */
    install_processor_object();

    /*
     *  And the special selectors, before VariableNode class>>initialize
     *  builds the compiler's table out of them.
     */
    install_special_selectors();
    install_empty_method_dictionaries();
    install_class_pools();

    ST_set_error_reporting(0);
    run_class_initializers(out);
    install_text_style();
    ST_set_error_reporting(1);
    run_class_initializers(out);

    install_user_interface();
    install_subclass_graph();
    install_class_organization();
    install_sources();
    install_system_organization();
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

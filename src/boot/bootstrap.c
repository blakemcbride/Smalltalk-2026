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
#include "gfx.h"
#include "font.h"
#include "st_sched.h"

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
    char        pools[4][64];
    unsigned    pool_count;

    char        cvars[MAX_IVARS][64];
    st_oop      cvar_assoc[MAX_IVARS];
    unsigned    cvar_count;

    /*  Instance variables including every inherited one, in frame order.  */
    char        all_ivars[MAX_IVARS][64];
    unsigned    all_ivar_count;
    /*  The same for the metaclass side, which has its own parallel chain.  */
    char        all_class_ivars[MAX_IVARS][64];
    unsigned    all_class_ivar_count;
    int         resolved;
} boot_class;

static boot_class   classes[MAX_CLASSES];
static unsigned     class_count;

/*
 *  Whether the library's symbol table exists yet.  Until it does there is
 *  nowhere to put a symbol; afterwards every symbol interned has to go in,
 *  or "#foo == 'foo' asSymbol" answers false -- the compiler would hold one
 *  Symbol and the image would make itself another.
 */
static int          symbol_table_ready;
static st_oop       symbol_table;

static st_oop       symbols[MAX_SYMBOLS];
static unsigned     symbol_count;

static st_oop       smalltalk;          /*  the SystemDictionary  */
static st_oop       globals_values;
static unsigned     global_count;
#define MAX_GLOBALS 1024

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
static size_t remember_source(const char *text);
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
    for (i = 0; i < symbol_count; ++i) {
        size_t  k;

        if (OM_fetch_byte_length(symbols[i]) != n)
            continue;
        for (k = 0; k < n; ++k) {
            if (OM_fetch_byte((uint32_t) k, symbols[i]) != (uint8_t) text[k])
                break;
        }
        if (k == n)
            return symbols[i];
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
                if (symbol_count < MAX_SYMBOLS)
                    symbols[symbol_count++] = candidate;
                return candidate;
            }
        }
    }

    s = OM_instantiate_bytes(BOOT_global("Symbol"), (uint32_t) n);
    if (!OM_is_object(s))
        return ST_NIL;
    for (i = 0; i < n; ++i)
        OM_store_byte(i, s, (uint8_t) text[i]);
    if (symbol_count < MAX_SYMBOLS) {
        symbols[symbol_count] = s;
        ++symbol_count;
        if (result)
            ++result->symbols_interned;
    }
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
     *  A Smalltalk-80 Float is IEEE 754 SINGLE precision: two 16-bit words,
     *  most significant first.  That is what Chapter 30 specifies, what the
     *  1983 image contains, and what the interpreter's own make_float emits
     *  for every computed result.
     *
     *  This used to store the host's double in native word order, which was
     *  wrong twice over.  The size disagreed, so a literal and a computed
     *  value of the same number were different shapes; and the order
     *  disagreed with the reader, which takes the words most significant
     *  first.  The visible effect was that 3.5 exponent answered -1060: the
     *  bits were being read as a completely different number.
     */
    union { float f; uint32_t u; } bits;
    st_oop  p = OM_instantiate_words(BOOT_global("Float"), 2);

    (void) user;
    if (!OM_is_object(p))
        return ST_NIL;
    bits.f = (float) value;
    OM_store_word(0, p, (uint16_t) (bits.u >> 16));
    OM_store_word(1, p, (uint16_t) (bits.u & 0xFFFF));
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
    if (global_count >= MAX_GLOBALS) {
        boot_fail("more than %d globals", MAX_GLOBALS);
        return ST_NIL;
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
} method_protocols[6000];
static unsigned     method_protocol_count;

/*
 *  Every method's source, in one buffer, in chunk format.
 *
 *  Smalltalk-80 does not keep source in the image: a CompiledMethod carries
 *  a position into a sources file, and the Browser reads the chunk there.
 *  Nothing says the stream has to be a file -- RemoteString asks it only to
 *  position: and nextChunk, which any PositionableStream does -- so the
 *  sources live in a String and the image reads them the ordinary way.
 */
static char        *source_text;
static size_t       source_length;
static size_t       source_capacity;

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
    for (i = 0; i < c->cvar_count; ++i) {
        if (strcmp(c->cvars[i], name) != 0)
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
    {
        st_oop      assoc = define_global(name, ST_NIL);
        boot_class *c = (boot_class *) user;

        /*
         *  A capitalised name first seen while compiling a class that shares
         *  a pool is almost certainly one of that pool's.  Remember it so the
         *  binding can be put where the pool's own initializer will find it.
         */
        if (c && c->pool_count > 0 && assoc != ST_OOP_INVALID
         && pool_binding_count < 512
         && (name[0] >= 'A' && name[0] <= 'Z')) {
            pool_bindings[pool_binding_count].association = assoc;
            snprintf(pool_bindings[pool_binding_count].pool, 64, "%.63s",
                     c->pools[0]);
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
        /*
         *  Metaclasses form their own chain, parallel to the classes': the
         *  metaclass of a subclass inherits from the metaclass of its
         *  superclass.  So a class method of Form sees the class-side
         *  instance variables of DisplayMedium, Object and so on.
         */
        for (i = 0; i < super->all_class_ivar_count; ++i) {
            snprintf(c->all_class_ivars[c->all_class_ivar_count], 64, "%.63s",
                     super->all_class_ivars[i]);
            ++c->all_class_ivar_count;
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
    for (i = 0; i < c->class_ivar_count
             && c->all_class_ivar_count < MAX_IVARS; ++i) {
        snprintf(c->all_class_ivars[c->all_class_ivar_count], 64, "%.63s",
                 c->class_ivars[i]);
        ++c->all_class_ivar_count;
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

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            ++p;
        if (!*p)
            break;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r'
            && n + 1 < 64)
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
    while (p > text && (p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\r'
                     || p[-1] == '\t'))
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
    if (quoted_after(text, "classVariableNames:", ivars, sizeof ivars))
        split_words(ivars, c->cvars, &c->cvar_count, MAX_IVARS);
    if (quoted_after(text, "poolDictionaries:", ivars, sizeof ivars))
        split_words(ivars, c->pools, &c->pool_count, 4);
    if (quoted_after(text, "category:", ivars, sizeof ivars))
        snprintf(c->category, sizeof c->category, "%.63s", ivars);

    ++class_count;
    if (result)
        ++result->classes_created;
    return 1;
}

/*
 *  The metaclass side of a class definition:
 *
 *      Form class
 *          instanceVariableNames: 'whiteMask blackMask'
 *
 *  These are instance variables of the metaclass, so they are in scope in
 *  class methods and nowhere else.  Form class>>initialize assigns to them.
 */
static int
parse_class_side_definition(const char *text)
{
    const char *at = strstr(text, " class");
    boot_class *c;
    char        name[64];
    char        ivars[512];
    size_t      n = 0;
    const char *p;

    if (!at || !strstr(text, "instanceVariableNames:"))
        return 0;
    /*  Anything else on the line means this is not a bare "Foo class".  */
    if (strstr(text, "subclass:"))
        return 0;

    p = text;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        ++p;
    while (p < at && (isalnum((unsigned char) *p) || *p == '_')
        && n + 1 < sizeof name)
        name[n++] = *p++;
    name[n] = '\0';
    if (!name[0] || p != at)
        return 0;

    c = find_class(name);
    if (!c)
        return 0;
    if (quoted_after(text, "instanceVariableNames:", ivars, sizeof ivars))
        split_words(ivars, c->class_ivars, &c->class_ivar_count, MAX_IVARS);
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
                  int *class_side, char *category, size_t catlen)
{
    const char *at = strstr(text, "methodsFor:");
    const char *p  = text;
    size_t      n  = 0;

    *class_side = 0;
    if (category && catlen)
        category[0] = '\0';
    if (!at)
        return 0;
    /*  The protocol this run of methods belongs to, for the Browser.  */
    if (category && catlen)
        quoted_after(at, "methodsFor:", category, catlen);
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
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
    return CLASS_FIXED_FIELDS + c->all_class_ivar_count;
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
                         make_format(class_object_size(c), 1, 0, 0));
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
            for (i = 0; i < shape->all_ivar_count && n < MAX_IVARS; ++i)
                ivar_pointers[n++] = shape->all_ivars[i];
        }
        for (i = 0; i < c->all_class_ivar_count && n < MAX_IVARS; ++i)
            ivar_pointers[n++] = c->all_class_ivars[i];
        ctx.instance_variables      = ivar_pointers;
        ctx.instance_variable_count = n;
    }  else  {
        for (i = 0; i < c->all_ivar_count; ++i)
            ivar_pointers[i] = c->all_ivars[i];
        ctx.instance_variables      = ivar_pointers;
        ctx.instance_variable_count = c->all_ivar_count;
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
    ctx.make_character     = BOOT_make_character;
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
    /*
     *  The source pointer, in the three bytes the compiler left for it.
     *  Chapter 27 reads them from the end: the last holds the top six bits
     *  of the position and, above those, the file number.
     */
    {
        size_t      position = remember_source(source);
        uint32_t    size = OM_fetch_byte_length(res.method);

        if (size >= 3 && position <= 0x3FFFFF) {
            OM_store_byte(size - 3, res.method, (uint8_t) (position & 0xFF));
            OM_store_byte(size - 2, res.method,
                          (uint8_t) ((position >> 8) & 0xFF));
            OM_store_byte(size - 1, res.method,
                          (uint8_t) ((position >> 16) & 0x3F));
        }
    }

    /*  Remember where it belongs, so the class can be organized later.  */
    if (protocol && protocol[0] && method_protocol_count < 6000) {
        unsigned    slot = method_protocol_count++;

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

static int
read_source(const char *path, int definitions_only)
{
    st_chunk_reader    *reader;
    st_chunk            chunk;
    char                err[256];
    char                class_name[64];
    char                protocol[64] = "";
    int                 class_side = 0;
    boot_class         *current = NULL;
    int                 in_methods = 0;

    reader = CHUNK_open(path, err, sizeof err);
    if (!reader) {
        boot_fail("%s", err);
        return 0;
    }
    while (CHUNK_next(reader, &chunk)) {
        /*
         *  A chunk with nothing to compile closes the method category.  That
         *  is the empty chunk of the "! !" idiom, and equally the comment
         *  the markbush sources use in its place.
         */
        if (!chunk.has_code) {
            in_methods = 0;
            current    = NULL;
            continue;
        }
        if (chunk.is_reader) {
            if (parse_methods_for(chunk.text, class_name, sizeof class_name,
                                  &class_side, protocol, sizeof protocol)) {
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
                              CHUNK_line(reader), protocol)) {
                CHUNK_close(reader);
                return 0;
            }
            continue;
        }
        if (definitions_only) {
            if (!parse_class_side_definition(chunk.text))
                parse_class_definition(chunk.text);
        }
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
            p = OM_instantiate_pointers(ST_NIL, CLASS_FIXED_FIELDS
                                                + CLASS_PLACEHOLDER_EXTRA);
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

static int
boot_build_locked(const char *const *paths, unsigned path_count,
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
        if (!read_source(paths[i], 0))
            return -1;
    }

    return 0;
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
BOOT_build(const char *const *paths, unsigned path_count,
           st_bootstrap_result *out)
{
    int status = boot_build_locked(paths, path_count, out);

    result = NULL;
    /*  Nothing C holds survives a collection unless the walk can see it.  */
    ST_interp_install_roots(BOOT_provide_roots);
    return status;
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
 *  Append one method's source in chunk format and answer where it started.
 *
 *  A bang inside the text is doubled, which is the chunk format's own escape
 *  and what nextChunk undoes on the way back out.
 */
static size_t
remember_source(const char *text)
{
    size_t  start = source_length;
    size_t  i;
    size_t  n = strlen(text);

    for (i = 0; i <= n; ++i) {
        char    c = (i < n) ? text[i] : '!';
        int     doubled = (i < n && c == '!');

        if (source_length + 3 > source_capacity) {
            size_t  want = source_capacity ? source_capacity * 2 : 65536;
            char   *grown = (char *) realloc(source_text, want);

            if (!grown)
                return 0;
            source_text = grown;
            source_capacity = want;
        }
        source_text[source_length++] = c;
        if (doubled)
            source_text[source_length++] = c;
    }
    return start;
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
        for (k = 0; k < classes[i].cvar_count; ++k)
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
     *  A TextCollector is normally made with a view; this one writes into a
     *  WriteStream on a String, which is what an image with no window on the
     *  Transcript yet can offer, and it means the text can be read back.
     */
    {
        st_oop  collector_class = BOOT_global("TextCollector");
        st_oop  stream_class = BOOT_global("WriteStream");
        st_oop  string_class = BOOT_global("String");

        if (OM_is_present(collector_class) && OM_is_present(stream_class)
         && OM_is_present(string_class)) {
            st_oop  on = lookup_in_chain(OM_fetch_class(stream_class), "on:");
            st_oop  buffer = OM_instantiate_bytes(string_class, 0);
            st_oop  collector;

            if (OM_is_present(on) && OM_is_present(buffer)
             && run_method_with(on, stream_class, &buffer, 1, 2000000)
             && OM_is_present(st_vm.return_value)) {
                st_oop  stream = st_vm.return_value;

                collector = OM_instantiate_pointers(collector_class, 3);
                if (OM_is_present(collector)) {
                    /*  contents, isLocked, then entryStream.  */
                    OM_store_pointer(2, collector, stream);
                    OM_increase_ref(collector);
                    define_global("Transcript", collector);
                }
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

    text = OM_instantiate_bytes(string_class, (uint32_t) source_length);
    if (!OM_is_present(text))
        return 0;
    for (i = 0; i < source_length; ++i)
        OM_store_byte((uint32_t) i, text, (uint8_t) source_text[i]);

    files = OM_instantiate_pointers(array_class, 2);
    if (!OM_is_present(files))
        return 0;
    OM_increase_ref(files);

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
            OM_store_pointer(1, files, st_vm.return_value);
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

        if (!OM_is_present(c->class_oop) || c->cvar_count == 0)
            continue;
        for (k = 0; k < c->cvar_count; ++k)
            if (c->cvar_assoc[k] != 0)
                ++present;
        if (present == 0)
            continue;

        /*  Room to spare: a hashed collection that fills up stops working. */
        arg = OM_int_oop((st_int) (c->cvar_count * 4 + 8));
        if (!run_method_with(new_with, dict_class, &arg, 1, 2000000))
            continue;
        pool = st_vm.return_value;
        if (!OM_is_present(pool))
            continue;

        for (k = 0; k < c->cvar_count; ++k) {
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
#define FONT_CODES      128
#define FONT_STRIKE_W   (FONT_CODES * ST_FONT_WIDTH)

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
    unsigned    code;
    unsigned    row;
    unsigned    i;

    if (!OM_is_present(font_class) || !OM_is_present(form_class)
     || !OM_is_present(word_array) || !OM_is_present(array_class))
        return ST_OOP_INVALID;

    bits = OM_instantiate_words(word_array, raster * ST_FONT_HEIGHT);
    if (!OM_is_present(bits))
        return ST_OOP_INVALID;

    /*  Paint each glyph into its slot, eight pixels wide.  */
    for (code = ST_FONT_FIRST; code <= ST_FONT_LAST; ++code) {
        unsigned    x = code * ST_FONT_WIDTH;

        for (row = 0; row < ST_FONT_HEIGHT; ++row) {
            uint8_t     glyph = ST_FONT_GLYPHS[code - ST_FONT_FIRST][row];
            uint32_t    index = row * raster + x / 16;
            uint16_t    word  = OM_fetch_word(index, bits);
            unsigned    shift = (x % 16 == 0) ? 8 : 0;

            word = (uint16_t) (word | ((uint16_t) glyph << shift));
            OM_store_word(index, bits, word);
        }
    }

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

    /*  One entry per code, plus one past the end for the last width.  */
    xtable = OM_instantiate_pointers(array_class, FONT_CODES + 2);
    if (!OM_is_present(xtable))
        return ST_OOP_INVALID;
    for (i = 0; i < FONT_CODES + 2; ++i) {
        unsigned    x = i * ST_FONT_WIDTH;

        OM_store_pointer(i, xtable,
                         OM_int_oop((st_int) (x > FONT_STRIKE_W
                                              ? FONT_STRIKE_W : x)));
    }

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
    OM_store_pointer(7,  font, OM_int_oop(ST_FONT_WIDTH));
    OM_store_pointer(8,  font, OM_int_oop(FONT_STRIKE_W));
    OM_store_pointer(9,  font, OM_int_oop(ST_FONT_HEIGHT - 1));  /*  ascent   */
    OM_store_pointer(10, font, OM_int_oop(1));                   /*  descent  */
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
    install_class_pools();

    ST_set_error_reporting(0);
    run_class_initializers(out);
    install_text_style();
    ST_set_error_reporting(1);
    run_class_initializers(out);

    install_user_interface();
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

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The image bootstrap: source files in, a runnable image out.
 *
 *  Every Smalltalk-80 image in existence descends by mutation from the one
 *  Xerox shipped in 1983; nobody has built a Blue Book image from text since.
 *  Pharo and Cuis bootstrap, but they run on top of an existing Smalltalk and
 *  target the Spur format.  Here the host is C.
 *
 *  ----------  The circularity  ----------
 *
 *  A class is an object, so it has a class.  The class of a class is its
 *  metaclass; the class of a metaclass is Metaclass; and the class of
 *  Metaclass is "Metaclass class", whose class is Metaclass again.  The
 *  graph closes on itself, so it cannot be built in dependency order --
 *  there is no order.
 *
 *  The way through is to build in three passes.  First allocate every class
 *  and metaclass object with its class field left nil, which is legal
 *  because nothing reads it yet.  Then patch the class fields, which closes
 *  every loop at once.  Only then compile methods, by which point every
 *  name a method might mention already resolves.
 *
 *  ----------  The fixed pointers  ----------
 *
 *  The interpreter names certain objects by numeric pointer -- nil is 2,
 *  Array is 16 -- so those objects must land on exactly those pointers.
 *  They are allocated first, in order, before anything else can take an
 *  index.
 */

#ifndef ST_BOOTSTRAP_H
#define ST_BOOTSTRAP_H

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char        error[512];
    unsigned    error_line;
    char        error_file[256];

    unsigned    classes_created;
    unsigned    methods_compiled;
    unsigned    symbols_interned;
} st_bootstrap_result;

/*
 *  Read every .st file named in `paths` (in order), build the image, and
 *  leave it in the object memory.  Returns 0 on success.
 */
int     BOOT_build(const char *const *paths, unsigned path_count,
                   st_bootstrap_result *out);

/*  Look up a global by name in the bootstrapped SystemDictionary.  */
st_oop  BOOT_global(const char *name);

/*  The SystemDictionary itself, which is also the guaranteed pointer 18.  */
st_oop  BOOT_smalltalk(void);

/*
 *  The literal factories the compiler calls back into, exposed so that an
 *  expression can be compiled against a bootstrapped image after the fact.
 */
st_oop  BOOT_intern_symbol(const char *text, void *user);
st_oop  BOOT_make_string(const char *text, void *user);
st_oop  BOOT_make_float(double value, void *user);
st_oop  BOOT_make_large_integer(int64_t value, void *user);
st_oop  BOOT_make_array(st_oop *elements, unsigned count, void *user);
st_oop  BOOT_make_character(unsigned code, void *user);

/*
 *  Globals the sources referred to but never defined.  Fills `names` with up
 *  to `max` of them and answers how many there are in total.
 */
unsigned BOOT_undeclared(const char **names, unsigned max);

/*  How many of those began with a lower-case letter -- probable bugs.  */
unsigned BOOT_undeclared_lowercase(void);

/*
 *  What running the class-side initializers did.
 */
typedef struct {
    unsigned    defined;        /*  classes with a class-side initialize  */
    unsigned    ran;            /*  ... that finished                     */
    unsigned    unfinished;     /*  ... that exceeded the bytecode budget */
    char        first_unfinished[64];
    unsigned    symbols_seeded;     /*  entries placed in the library table */
    unsigned    symbols_total;
} st_boot_init_report;

/*
 *  Send #initialize to every class that defines one, in definition order.
 *
 *  This is the step an image build performs and a fileIn does not: the
 *  library's class variables -- CharacterTable, the text constants, the
 *  Symbol table -- are set by these methods and are nil without them.
 *
 *  Each initializer gets its own bytecode budget, so one that cannot
 *  complete (because it wants a Display, or a file) costs one budget rather
 *  than the whole build.  Answers 0 always: a class that will not initialise
 *  is a fact to report, not a reason to refuse the image.
 */
int BOOT_run_initializers(st_boot_init_report *out);

/*
 *  String>>hash as the 1983 library computes it, in C.
 *
 *  A deliberate duplicate, used to place symbols in the library's own hash
 *  table without interpreting 3601 sends.  tests/unit/test_image.c asserts
 *  it agrees with the image, which is what keeps a copy from drifting.
 */
uint32_t BOOT_string_hash(st_oop string);

/*
 *  Visit everything the bootstrap holds from C: the globals, the symbols,
 *  the class and metaclass objects and the class-variable bindings.
 *
 *  BOOT_build installs this as the interpreter's extra root provider.  A
 *  caller that installs one of its own must call this from it, or the image
 *  the bootstrap just built will be collected out from under it.
 */
void BOOT_provide_roots(om_visit_fn visit);
st_oop  BOOT_lookup_global(const char *name, void *user);

/*  Byte offset of a method's first bytecode, past the header and literals. */
uint32_t BOOT_method_initial_ip(st_oop method);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_BOOTSTRAP_H  */

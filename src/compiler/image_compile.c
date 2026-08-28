/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  ----------  Compiling inside a running image  ----------
 *
 *  WHY THIS EXISTS.  There were two compilers.  This one, in C, built the
 *  image; the other, 1983's, in Smalltalk, ran when the Browser accepted a
 *  method, when a workspace evaluated an expression, and when TonelReader
 *  loaded a service file into a running server.  They did not agree.  The
 *  first gives blocks real closures; the second gives them Chapter 27's
 *  BlockContext, which reads a variable as it is NOW rather than as it was.
 *
 *  So `(1 to: 3) collect: [:i | [i]]' answered 1, 2, 3 when it was compiled
 *  into the image and 3, 3, 3 when the same text was recompiled inside it.
 *  A Tonel service file worked when the image was built and could quietly
 *  mean something else after the hot reload the REST server exists for.
 *  That is the same shape as the eighty-six methods the image could not
 *  re-parse, except that one was about READING what the C compiler wrote
 *  and this is about WRITING what it writes.
 *
 *  The answer is not a second implementation of closures in Smalltalk.  Two
 *  implementations of one semantics are two things to keep in step, and the
 *  eighty-six methods are what keeping them in step looks like when it
 *  fails.  There is one code generator now, and this is the door into it.
 *
 *  WHAT THE COMPILER NEEDED THAT IT DID NOT HAVE.  compiler.c builds nothing
 *  itself: literals come from factories in an st_compile_context and names
 *  are resolved through its lookup_global.  The bootstrap's context reads
 *  the bootstrap's own C tables, which is correct while the image is being
 *  made and useless afterwards -- a class defined at run time is in the
 *  image's Smalltalk and in no C array.  So the whole of this file is one
 *  lookup_global that reads the IMAGE: the class's own class variables,
 *  then its pools, then Smalltalk, walking the superclass chain as the
 *  image's own Encoder does.
 *
 *  The instance variables are NOT resolved here.  They arrive as an Array of
 *  Strings that the caller got from `allInstVarNames', so what is in scope
 *  has one definition and not two -- which is the whole point of the file.
 */

#include "image_compile.h"
#include "bootstrap.h"
#include "interp.h"
#include "census.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  Class fields past the three the interpreter names in interp.h.  Their
 *  numbers are fixed by the class definitions in sources/Kernel-Classes and
 *  are the same ones bootstrap.c lays out.
 */
#define IMGC_CLASS_INSTANCE_VARS    4
#define IMGC_CLASS_POOL             7
#define IMGC_CLASS_SHARED_POOLS     8
#define IMGC_METACLASS_THIS_CLASS   6

/*  The most instance variables one class chain may declare.  */
#define IMGC_MAX_IVARS              256

typedef struct {
    st_oop      class_oop;          /*  whose class variables are in scope  */
    st_oop      guard;              /*  a rooted Array; see imgc_hold  */
    uint32_t    held;
} imgc_scope;

/*
 *  Keep what the compile has built alive until the method holds it.
 *
 *  compiler.c collects literals in a C array and hands them over only at the
 *  end, when the CompiledMethod is made.  A C array is not a root.  Under
 *  the bootstrap that never mattered -- one thread, and the collector runs
 *  when allocation asks it to, which is rarely there -- but a run-time
 *  compile happens on a worker with thirty others allocating beside it, so
 *  a collection in the middle of one would free a String or an Array that
 *  exists so far only in `code.literals[]', and the method would be built
 *  around a freed object.
 *
 *  So every object a factory makes is also stored into an Array the CALLER
 *  has made reachable -- it pushes it on the Smalltalk stack, and the
 *  collector marks a context's stack up to its stack pointer.  The guard is
 *  dropped as soon as the method exists, because the method then holds its
 *  own literals.
 *
 *  Overflow is not a failure: a method may reference at most 64 literals
 *  (six bits in the Blue Book header), so the room here is several times
 *  what any method can use, and running out means the compile was going to
 *  be refused anyway.
 */
static st_oop
imgc_hold(imgc_scope *scope, st_oop object)
{
    if (scope && OM_is_object(object) && OM_is_object(scope->guard)
     && scope->held < OM_fetch_word_length(scope->guard)) {
        OM_store_pointer(scope->held, scope->guard, object);
        ++scope->held;
    }
    return object;
}

/*
 *  The factories, each the bootstrap's with the object held.
 *
 *  Symbols are not held and do not need to be: interning refcounts them and
 *  puts them in the symbol table, which is a root, so a Symbol outlives any
 *  collection from the moment it exists.  Characters are the same -- they
 *  are fetched from CharacterTable rather than made.
 */
static st_oop
imgc_make_string(const char *text, void *user)
{
    return imgc_hold((imgc_scope *) user, BOOT_make_string(text, user));
}

/*
 *  And from bytes, for a string literal holding a NUL -- which is data in
 *  a String and the end of the text to make_string.  Bugs3 B28.
 */
static st_oop
imgc_make_string_n(const char *bytes, size_t length, void *user)
{
    return imgc_hold((imgc_scope *) user,
                     BOOT_make_string_n(bytes, length, user));
}

static st_oop
imgc_make_float(double value, void *user)
{
    return imgc_hold((imgc_scope *) user, BOOT_make_float(value, user));
}

static st_oop
imgc_make_large_integer(int64_t value, void *user)
{
    return imgc_hold((imgc_scope *) user, BOOT_make_large_integer(value, user));
}

static st_oop
imgc_make_large_integer_digits(const char *digits, unsigned radix,
                               int negative, void *user)
{
    return imgc_hold((imgc_scope *) user,
                     BOOT_make_large_integer_digits(digits, radix, negative,
                                                    user));
}

static st_oop
imgc_make_array(st_oop *elements, unsigned count, void *user)
{
    return imgc_hold((imgc_scope *) user,
                     BOOT_make_array(elements, count, user));
}

static st_oop
imgc_make_byte_array(const uint8_t *bytes, unsigned count, void *user)
{
    return imgc_hold((imgc_scope *) user,
                     BOOT_make_byte_array(bytes, count, user));
}

static st_oop
imgc_make_method_state(st_oop pragmas, void *user)
{
    return imgc_hold((imgc_scope *) user,
                     BOOT_make_method_state(pragmas, user));
}

/*
 *  Whether that object is a class.
 *
 *  Asked exactly: the class of a class is a metaclass, and the class of a
 *  metaclass is Metaclass.  Guessing from the shape -- "it has a method
 *  dictionary at field 1" -- would also accept a Metaclass's own instance
 *  and anything else with a Dictionary in the right place.
 */
static int
is_a_class(st_oop p)
{
    st_oop  metaclass;

    if (!OM_is_object(p))
        return 0;
    metaclass = BOOT_global("Metaclass");
    if (!OM_is_present(metaclass))
        return 0;
    if (!OM_is_object(OM_fetch_class(p)))
        return 0;
    return OM_fetch_class(OM_fetch_class(p)) == metaclass;
}

/*
 *  Whose class variables a method compiled into that class can see.
 *
 *  For a class, its own.  For a METACLASS, the class's -- which is what
 *  Behavior>>classPool answers for one, and the reason a class-side method
 *  may say `MacroSelectors' at all.  A metaclass has neither classPool nor
 *  sharedPools of its own; it has seven fields and they stop at thisClass.
 *
 *  Missing this compiled `MessageNode class>>shMacros ^MacroSelectors' into
 *  a method that answered nil, because the name fell through to Smalltalk,
 *  was not there either, and became an Undeclared binding.
 */
static st_oop
pool_owner(st_oop cls)
{
    st_oop  metaclass;

    if (!OM_is_object(cls) || !OM_pointer_bit(cls))
        return cls;
    metaclass = BOOT_global("Metaclass");
    if (OM_is_present(metaclass) && OM_fetch_class(cls) == metaclass
     && OM_fetch_word_length(cls) > IMGC_METACLASS_THIS_CLASS)
        return OM_fetch_pointer(IMGC_METACLASS_THIS_CLASS, cls);
    return cls;
}

/*
 *  The pools a class shares, which are two different things wearing one name.
 *
 *  1983 pools are Dictionary globals -- TextConstants is one.  A Pharo pool
 *  is a SharedPool SUBCLASS whose class variables are the pool.  Both appear
 *  in sharedPools and both have to be read; bootstrap.c's own lookup makes
 *  the same distinction and says so at greater length.
 */
static st_oop
association_in_pool(st_oop pool, const char *name)
{
    if (is_a_class(pool))
        return BOOT_image_association(
                    OM_fetch_pointer(IMGC_CLASS_POOL, pool), name);
    return BOOT_image_association(pool, name);
}

static st_oop
imgc_lookup_global(const char *name, void *user)
{
    imgc_scope *scope = (imgc_scope *) user;
    st_oop      cls;
    st_oop      found;

    /*
     *  Class variables and pools, up the superclass chain, which is the
     *  order Encoder>>lookupInPools: reads them in.
     *
     *  Every field is reached only after its object has been asked whether
     *  it HAS one.  This walk is given an object by a caller in the image
     *  and follows it upward, so `it is a class, classes have nine fields'
     *  is an assumption about somebody else's argument: a fixed OOP that
     *  the bootstrap allocated as a three-slot placeholder is enough to
     *  read past the end of, which AddressSanitizer caught at the
     *  superclass fetch.
     */
    cls = scope ? scope->class_oop : ST_NIL;
    while (OM_is_object(cls) && OM_pointer_bit(cls)
        && OM_fetch_word_length(cls) > ST_CLASS_SUPERCLASS) {
        st_oop      owner = pool_owner(cls);
        st_oop      pools;
        uint32_t    slots;
        uint32_t    i;
        st_oop      next = OM_fetch_pointer(ST_CLASS_SUPERCLASS, cls);

        if (!OM_is_object(owner) || !OM_pointer_bit(owner)
         || OM_fetch_word_length(owner) <= IMGC_CLASS_SHARED_POOLS) {
            cls = next;
            continue;
        }
        found = BOOT_image_association(
                    OM_fetch_pointer(IMGC_CLASS_POOL, owner), name);
        if (found != ST_OOP_INVALID)
            return found;
        pools = OM_fetch_pointer(IMGC_CLASS_SHARED_POOLS, owner);
        if (!OM_is_object(pools) || !OM_pointer_bit(pools))
            continue;
        slots = OM_fetch_word_length(pools);
        for (i = 0; i < slots; ++i) {
            st_oop  pool = OM_fetch_pointer(i, pools);

            if (!OM_is_object(pool))
                continue;
            found = association_in_pool(pool, name);
            if (found != ST_OOP_INVALID)
                return found;
        }
        cls = next;
    }

    /*
     *  Then the image's own Smalltalk, which is where a global defined at
     *  RUN time lives -- and the reason the bootstrap's lookup cannot serve
     *  here: it reads a C array that stopped being the whole truth the
     *  moment the image started running.
     */
    found = BOOT_image_association(ST_SMALLTALK, name);
    if (found != ST_OOP_INVALID)
        return found;

    /*
     *  And then Undeclared, which is where a name the image has been asked
     *  to compile before -- but which nothing has defined yet -- is kept.
     *
     *  1983's Encoder puts an unknown name there bound to nil and carries
     *  on, which is what makes a forward reference compile; the binding is
     *  live the moment something assigns to the name.  This side does not
     *  write it, because writing a Smalltalk Dictionary from C is the
     *  duplication this file exists to remove -- the caller declares the
     *  name and compiles again, and the second compile finds it here.
     */
    found = BOOT_image_association(
                BOOT_global_value_named("Undeclared"), name);
    if (found != ST_OOP_INVALID)
        return found;
    return ST_NIL;                  /*  the compiler reports it undeclared  */
}

/*
 *  A copy of that String object's bytes, terminated for the callers that
 *  want a C string, with the byte count in `*length' for the one that
 *  must not trust the terminator.
 *
 *  The source of a compile is the second kind.  A String may hold a NUL,
 *  and this copy used to be handed to the compiler as a C string, so the
 *  compile stopped at the first NUL in silence: `3 + 4 <NUL> + 100'
 *  answered 7, and a method compiled from such text installed with the
 *  prefix as its code and the whole text as its source.  Bugs3 B28.
 */
static char *
c_string_of(st_oop string, size_t *length)
{
    uint32_t    n;
    uint32_t    i;
    char       *text;

    if (!OM_is_object(string) || OM_pointer_bit(string))
        return NULL;
    n = OM_fetch_byte_length(string);
    text = (char *) malloc((size_t) n + 1);
    if (!text)
        return NULL;
    for (i = 0; i < n; ++i)
        text[i] = (char) OM_fetch_byte(i, string);
    text[n] = '\0';
    if (length)
        *length = n;
    return text;
}

int
IMGC_compile(st_oop source, st_oop class_oop, st_oop ivar_names,
             st_oop class_association, int no_pattern, int dialect,
             st_oop guard, st_compile_result *out)
{
    st_compile_context  ctx;
    imgc_scope          scope;
    char               *text;
    size_t              length = 0;
    char               *ivars[IMGC_MAX_IVARS];
    const char         *ivar_pointers[IMGC_MAX_IVARS];
    unsigned            ivar_count = 0;
    unsigned            i;
    int                 status;

    memset(out, 0, sizeof *out);
    out->method = ST_OOP_INVALID;

    text = c_string_of(source, &length);
    if (!text) {
        snprintf(out->error, sizeof out->error,
                 "the source to compile must be a String");
        return -1;
    }

    if (OM_is_object(ivar_names) && OM_pointer_bit(ivar_names)) {
        uint32_t    n = OM_fetch_word_length(ivar_names);

        for (i = 0; i < n && ivar_count < IMGC_MAX_IVARS; ++i) {
            char   *one = c_string_of(OM_fetch_pointer(i, ivar_names), NULL);

            if (!one)
                continue;
            ivars[ivar_count] = one;
            ivar_pointers[ivar_count] = one;
            ++ivar_count;
        }
    }

    scope.class_oop = class_oop;
    scope.guard     = guard;
    scope.held      = 0;

    memset(&ctx, 0, sizeof ctx);
    ctx.dialect                   = dialect;
    ctx.no_pattern                = no_pattern;
    ctx.instance_variables        = ivar_pointers;
    ctx.instance_variable_count   = ivar_count;
    ctx.lookup_global             = imgc_lookup_global;
    ctx.user                      = &scope;
    ctx.intern_symbol             = BOOT_intern_symbol;
    ctx.make_string               = imgc_make_string;
    ctx.make_string_n             = imgc_make_string_n;
    ctx.make_float                = imgc_make_float;
    ctx.make_large_integer        = imgc_make_large_integer;
    ctx.make_large_integer_digits = imgc_make_large_integer_digits;
    ctx.make_array                = imgc_make_array;
    ctx.make_byte_array           = imgc_make_byte_array;
    ctx.make_method_state         = imgc_make_method_state;
    ctx.make_character            = BOOT_make_character;
    ctx.method_class_association  = OM_is_object(class_association)
                                        ? class_association
                                        : ST_OOP_INVALID;

    status = COMPILE_method_n(text, length, &ctx, out);
    /*
     *  And the method itself, for the moment between building it and the
     *  caller storing it somewhere the collector can see.
     */
    if (status == 0)
        (void) imgc_hold(&scope, out->method);

    for (i = 0; i < ivar_count; ++i)
        free(ivars[i]);
    free(text);
    return status;
}

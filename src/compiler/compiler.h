/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Smalltalk-80 compiler: source text to a CompiledMethod.
 *
 *  It emits the Blue Book bytecode set, which the interpreter already
 *  reproduces against Xerox's own traces, so the two halves can be checked
 *  against each other: compile a method from the 1983 sources and the
 *  bytecodes should match the ones the 1983 compiler produced and left in
 *  the image.  That is the gate this compiler is built to pass.
 *
 *  Control flow is compiled to jumps rather than sends wherever the Blue
 *  Book compiler does the same.  ifTrue:, ifFalse:, and:, or: and the while
 *  loops are inlined; to:do: is not, because the reference traces show it
 *  arriving as a real send with a block argument.
 */

#ifndef ST_COMPILER_H
#define ST_COMPILER_H

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  What the compiler needs to know about the class it is compiling for:
 *  the names of instance variables, so a bare identifier can be resolved,
 *  and a way to look up globals.
 */
/*
 *  Which language to compile.
 *
 *  Blue Book is the default and is what the 1983 library, the self-hosting
 *  check and the trace oracle all use; the closure machinery is reached
 *  only when a caller asks for it, so none of them can be affected by it.
 */
#define ST_DIALECT_BLUE_BOOK    0
#define ST_DIALECT_CLOSURES     1

typedef struct {
    int                 dialect;
    const char *const  *instance_variables;
    unsigned            instance_variable_count;

    /*
     *  Resolve a global name to an Association.  The bootstrap supplies the
     *  system dictionary; a running image supplies its own.  Returning
     *  ST_NIL makes the compiler report an undeclared variable.
     */
    st_oop            (*lookup_global)(const char *name, void *user);
    void               *user;

    /*  Intern a Symbol, and make a String and a Float.  */
    st_oop            (*intern_symbol)(const char *text, void *user);
    st_oop            (*make_string)(const char *text, void *user);
    st_oop            (*make_float)(double value, void *user);
    st_oop            (*make_large_integer)(int64_t value, void *user);
    st_oop            (*make_array)(st_oop *elements, unsigned count,
                                    void *user);
    /*
     *  A literal ByteArray, for #[1 2 3].  Post-Blue-Book; a 1983 image never
     *  needed it because the syntax did not exist.  When NULL the literal
     *  answers nil, which is enough for -syntax to check the grammar without
     *  an object memory.
     */
    st_oop            (*make_byte_array)(const uint8_t *bytes, unsigned count,
                                         void *user);
    /*
     *  An AdditionalMethodState holding a method's pragmas, given them as
     *  an Array of Arrays -- #(#(#author 'Blake') #(#deprecated 'x')).
     *
     *  Identified in the literal frame by its CLASS rather than by
     *  position.  Pharo puts it next-to-last, and next-to-last here is
     *  where the Blue Book header extension goes when a method declares a
     *  primitive, so position is not available to borrow.  Scanning a
     *  frame of at most a few dozen literals for one object of a class
     *  nothing else instantiates costs nothing and cannot be confused.
     *
     *  NULL, or answering nil, when the profile has no such class -- which
     *  is the Blue Book case, and leaves its methods exactly as they were.
     */
    st_oop            (*make_method_state)(st_oop pragmas, void *user);
    /*
     *  A character literal.  Characters are unique per code point, so this
     *  is a lookup rather than a construction, and in an image it is a fetch
     *  from CharacterTable.  It goes through the context like every other
     *  literal so that the compiler needs no object memory of its own --
     *  which is what lets -syntax check source without building an image.
     *  When NULL the character table is read directly.
     */
    st_oop            (*make_character)(unsigned code, void *user);

    /*
     *  An Association whose value is the class this method is being compiled
     *  into -- the metaclass, for a class-side method.
     *
     *  A super send takes its lookup class from the method's LAST literal:
     *  bytecodes 133 and 134 carry only the selector, and the interpreter
     *  reads literal (count - 1), expects an Association, and starts the
     *  lookup at the superclass of its value.  So a method containing a
     *  super send must carry this, and one that does not need not.  Xerox
     *  used the class's own global binding on the instance side
     *  (#SmallInteger -> SmallInteger) and a keyless one on the class side
     *  (nil -> IdentityDictionary class); all the interpreter reads is the
     *  value, so either shape works.
     */
    st_oop              method_class_association;
} st_compile_context;

typedef struct {
    st_oop      method;             /*  ST_OOP_INVALID on failure  */
    char        selector[256];
    unsigned    argument_count;
    unsigned    temporary_count;
    unsigned    primitive;
    char        error[256];
    unsigned    error_line;
} st_compile_result;

/*
 *  Compile one method.  The text is a complete method: the pattern line,
 *  optional temporaries, and the body.
 */
int     COMPILE_method(const char *source, const st_compile_context *ctx,
                       st_compile_result *out);

/*
 *  Compile to raw bytecodes without building a CompiledMethod, which is how
 *  the compiler is tested against the bytecodes already in an image.
 */
typedef struct {
    uint8_t     bytecodes[4096];
    unsigned    length;
    st_oop      literals[256];
    unsigned    literal_count;
    unsigned    argument_count;
    unsigned    temporary_count;
    unsigned    primitive;
    int         needs_large_context;
    char        selector[256];
    char        error[256];
    unsigned    error_line;
} st_compiled_code;

int     COMPILE_to_bytecodes(const char *source, const st_compile_context *ctx,
                             st_compiled_code *out);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_COMPILER_H  */

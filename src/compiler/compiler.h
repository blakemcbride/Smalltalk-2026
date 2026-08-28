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
#include "lexer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  What the compiler needs to know about the class it is compiling for:
 *  the names of instance variables, so a bare identifier can be resolved,
 *  and a way to look up globals.
 */
/*
 *  Which language to compile.  The two constants live in lexer.h, because
 *  the choice has to be made before the first token: the underscore and
 *  the length of a binary selector both changed meaning after 1983.
 *
 *  Blue Book is the default and is what the 1983 library, the self-hosting
 *  check and the trace oracle all use; the closure machinery is reached
 *  only when a caller asks for it, so none of them can be affected by it.
 */

typedef struct {
    int                 dialect;
    /*
     *  Compile a body rather than a method: no pattern line, and the value
     *  of the LAST statement is what the method answers.
     *
     *  That is what a doIt is, and it is the one thing the image's own
     *  compiler could do that this one could not.  `Compiler evaluate:'
     *  takes text with temporaries and several statements and wants the
     *  last one's value; a method with no explicit return answers self.
     *  compile_statements already knows the difference -- it is the same
     *  distinction a block draws from a method -- so this only has to say
     *  which of the two is meant.
     *
     *  The selector of the result is `DoIt'.
     */
    int                 no_pattern;
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
    /*
     *  A String from BYTES, for a string literal that contains a NUL.
     *
     *  make_string takes a C string, which ends at the first NUL, and a
     *  literal may spell one: `(String with: (Character value: 0) with:
     *  $a) storeString' does.  A context that supplies this gets every
     *  string literal through it, with the literal's byte count; one that
     *  leaves it NULL gets make_string as before and cannot compile such
     *  a literal whole.  The image's seam supplies it; the bootstrap,
     *  whose source arrives from files as C strings, need not.  Bugs3 B28.
     */
    st_oop            (*make_string_n)(const char *bytes, size_t length,
                                       void *user);
    st_oop            (*make_float)(double value, void *user);
    st_oop            (*make_large_integer)(int64_t value, void *user);
    /*
     *  For a literal too wide for int64_t.  `digits' is the literal's own
     *  text in `radix'.  A context that does not supply this cannot compile
     *  such a literal, and the compiler says so rather than wrapping it.
     */
    st_oop            (*make_large_integer_digits)(const char *digits,
                                                   unsigned radix,
                                                   int negative, void *user);
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
    /*
     *  And where in the source, as a byte offset, so a caller with an editor
     *  can select the text rather than count lines to find it.  Zero when
     *  the failure has no position -- a method with too many literals is
     *  about the whole method.
     */
    size_t      error_offset;
    /*
     *  The name, when the failure was that a variable is not declared.
     *
     *  Named rather than left in the message, because a caller has to ACT
     *  on it and matching on prose is how a message becomes an interface.
     *  The image's own Encoder, asked to compile with no editor to ask a
     *  person, puts an unknown name in Undeclared bound to nil and carries
     *  on -- so an image-side caller declares this name and compiles again,
     *  which is that behaviour reproduced without teaching this compiler
     *  how to write a Smalltalk Dictionary.
     */
    char        undeclared[256];    /*  as long as a token's text  */
} st_compile_result;

/*
 *  Compile one method.  The text is a complete method: the pattern line,
 *  optional temporaries, and the body.
 */
int     COMPILE_method(const char *source, const st_compile_context *ctx,
                       st_compile_result *out);

/*
 *  The same, for source that is bytes rather than a C string: `length'
 *  is read, the terminator is not, and a NUL inside a string literal is
 *  part of the literal.  COMPILE_method measures with strlen and stopped
 *  at the first NUL, which ended the compile there with nothing said --
 *  see LEX_open_n in lexer.h.  Bugs3 B28.
 */
int     COMPILE_method_n(const char *source, size_t length,
                         const st_compile_context *ctx,
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
    /*
     *  Whether this method header can actually hold that number.
     *
     *  The Blue Book header extension gives the primitive index eight bits,
     *  so 255 is the ceiling of the format -- not of this implementation.
     *  Spur uses a different header and Pharo's SmallFloat64 declares 541
     *  through 559.  A number that cannot be encoded is compiled away and
     *  the method's Smalltalk body is kept, which is not a compromise: an
     *  unimplemented primitive fails and runs the body, and a primitive
     *  that cannot be written down is indistinguishable from one that
     *  always fails.
     */
    int         primitive_encodable;
    /*
     *  For a named primitive -- <primitive: 'fn' module: 'Mod'> -- the two
     *  strings, which primitive 117 alone does not tell you.  A report on
     *  what a body of source asks the VM for is worth very little if every
     *  module callout in it is spelled "117".
     */
    char        primitive_name[64];
    char        primitive_module[64];
    int         needs_large_context;
    /*
     *  The exact frame this method needs, temporaries plus stack.  Written
     *  into the method header's high bits so a context can be made to fit;
     *  see ST_header_frame_size in interp.h.
     */
    unsigned    frame_slots;
    char        selector[256];
    char        error[256];
    unsigned    error_line;
    size_t      error_offset;       /*  see st_compile_result  */
    char        undeclared[256];    /*  see st_compile_result  */
} st_compiled_code;

/*
 *  Answer the selector a method's source declares, without compiling it.
 *  0 on success; -1 if the text does not begin with a message pattern.
 */
int     COMPILE_selector_of(const char *source, char *out, size_t out_len);

int     COMPILE_to_bytecodes(const char *source, const st_compile_context *ctx,
                             st_compiled_code *out);
int     COMPILE_to_bytecodes_n(const char *source, size_t length,
                               const st_compile_context *ctx,
                               st_compiled_code *out);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_COMPILER_H  */

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The bytecode interpreter: Blue Book Chapters 27 and 28.
 *
 *  This file is written once and compiled against whichever object memory
 *  the build selected.  The 16-bit Blue Book memory lets it be checked
 *  against Xerox's own execution traces; the 64-bit threaded memory is what
 *  ships.  Nothing here may assume a pointer width or an allocation policy.
 */

#ifndef ST_INTERP_H
#define ST_INTERP_H

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

/*  ----------  Context layout (Blue Book Chapter 27)  ----------
 *
 *  MethodContext           BlockContext
 *      0   sender              0   caller
 *      1   instructionPointer  1   instructionPointer
 *      2   stackPointer        2   stackPointer
 *      3   method              3   blockArgumentCount
 *      4   unused              4   initialIP
 *      5   receiver            5   home
 *      6.. temporaries, stack  6.. temporaries, stack
 *
 *  The instruction pointer and stack pointer are stored as SmallIntegers and
 *  are one-relative in the image, which is why they are adjusted on the way
 *  in and out.
 */
#define ST_CTX_SENDER               0
#define ST_CTX_CALLER               0
#define ST_CTX_IP                   1
#define ST_CTX_SP                   2
#define ST_CTX_METHOD               3
#define ST_CTX_BLOCK_ARG_COUNT      3
#define ST_CTX_INITIAL_IP           4
#define ST_CTX_HOME                 5
#define ST_CTX_RECEIVER             5
#define ST_CTX_TEMP_FRAME_START     6

/*  The large-context flag in a method header picks between these.  */
#define ST_SMALL_CONTEXT_SLOTS      (ST_CTX_TEMP_FRAME_START + 12)
#define ST_LARGE_CONTEXT_SLOTS      (ST_CTX_TEMP_FRAME_START + 32)

/*  ----------  Class layout used by the interpreter  ----------  */

#define ST_CLASS_SUPERCLASS         0
#define ST_CLASS_METHOD_DICT        1
#define ST_CLASS_FORMAT             2

/*  Association, as used for global variables in literal frames.  */
#define ST_ASSOCIATION_KEY          0
#define ST_ASSOCIATION_VALUE        1

/*  Message, built when a send is not understood.  */
#define ST_MESSAGE_SELECTOR         0
#define ST_MESSAGE_ARGUMENTS        1

/*  Point, for primitive 18.  */
#define ST_POINT_X                  0
#define ST_POINT_Y                  1

/*  ----------  CompiledMethod  ----------
 *
 *  Word 0 is the header, a SmallInteger.  The Blue Book extracts its fields
 *  with bit 0 as the MOST significant bit of the 16-bit object pointer, and
 *  because a SmallInteger pointer carries its value shifted left one with a
 *  tag in the low bit, the shifts below operate on the raw pointer.
 *
 *      flag value      bits 0..2   ->  (p >> 13) & 7
 *      temp count      bits 3..7   ->  (p >>  8) & 31
 *      large context   bit  8      ->  (p >>  7) & 1
 *      literal count   bits 9..14  ->  (p >>  1) & 63
 *
 *  Flag values 0..4 mean "no primitive, this many arguments"; 5 means
 *  "return self"; 6 means "return an instance variable", whose index is in
 *  the temporary-count field; 7 means a header extension is present in the
 *  next-to-last literal, carrying the argument count and primitive index.
 */
#define ST_METHOD_HEADER_INDEX      0
#define ST_METHOD_LITERAL_START     1

static inline unsigned
ST_header_flag_value(st_oop header)
{
    return (header >> 13) & 7;
}

static inline unsigned
ST_header_temporary_count(st_oop header)
{
    return (header >> 8) & 31;
}

static inline unsigned
ST_header_large_context(st_oop header)
{
    return (header >> 7) & 1;
}

static inline unsigned
ST_header_literal_count(st_oop header)
{
    return (header >> 1) & 63;
}

/*  Header extension: argument count in bits 2..6, primitive in bits 7..14.  */
static inline unsigned
ST_extension_argument_count(st_oop ext)
{
    return (ext >> 9) & 31;
}

static inline unsigned
ST_extension_primitive_index(st_oop ext)
{
    return (ext >> 1) & 255;
}

/*  ----------  Interpreter state  ----------  */

typedef struct {
    st_oop      active_context;
    st_oop      home_context;
    st_oop      method;
    st_oop      receiver;
    uint32_t    instruction_pointer;    /*  byte index into the method  */
    uint32_t    stack_pointer;          /*  field index within context  */

    st_oop      message_selector;
    uint32_t    argument_count;
    st_oop      new_method;
    unsigned    primitive_index;
    int         success;                /*  primitive succeeded  */

    uint64_t    cycle;                  /*  bytecodes executed  */
    int         running;
    int         call_depth;
} st_interp;

extern st_interp    st_vm;

/*  ----------  API  ----------  */

/*
 *  Bring the interpreter up on a freshly loaded image: find the active
 *  process from the scheduler association and install its context.
 */
int         ST_interp_init(char *errbuf, size_t errlen);

/*  Execute at most `limit` bytecodes, or until halted.  Returns cycles run. */
uint64_t    ST_interp_run(uint64_t limit);

/*
 *  Write the interpreter's registers back into the active context, and make
 *  another context active.  The scheduler needs both to park one process and
 *  resume another; they are only valid at a bytecode boundary.
 */
void        ST_store_active_context(void);
void        ST_set_active_context(st_oop ctx);

/*  ----------  Tracing  ----------
 *
 *  Reproduces the format of the Xerox reference traces exactly, because the
 *  whole point is to diff against them.  See src/interp/trace.c.
 */
typedef enum {
    ST_TRACE_OFF = 0,
    ST_TRACE_BYTECODES,     /*  every bytecode, sends, returns: trace2  */
    ST_TRACE_SENDS          /*  sends and returns, indented: trace3     */
} st_trace_mode;

void    ST_trace_set(st_trace_mode mode, void *stream);

/*  Emitted by the interpreter at the points the Xerox tracer emitted them. */
void    ST_trace_bytecode(uint8_t code, st_oop method, uint32_t ip);
void    ST_trace_send(st_oop receiver, st_oop selector, uint32_t argc,
                      const st_oop *args);
void    ST_trace_return(st_oop value, int from_block);
void    ST_trace_primitive(unsigned index);

/*  Render an object the way the Xerox tracer did.  */
void    ST_print_object(st_oop p, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_INTERP_H  */

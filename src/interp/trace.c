/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The execution tracer, reproducing the format of the Xerox reference
 *  traces exactly so that our output can be diffed against them.
 *
 *  Two modes, matching the two traces on the 1983 tape:
 *
 *      trace2  every bytecode, plus sends, returns and primitives
 *      trace3  sends and returns only, indented one tab per call depth
 *
 *  Object printing follows the Xerox tracer, quirks included.  It prints
 *  SmallIntegers as decimal, nil as "nil" and false as "false" -- but it has
 *  no case for true, which therefore comes out as "aTrue" via the general
 *  article-plus-class-name rule.  That asymmetry is not a mistake here: bare
 *  "true" never appears in either reference trace and "aTrue" appears four
 *  times, so reproducing it is required.
 */

#include "interp.h"
#include "census.h"

#include <stdio.h>
#include <string.h>

static st_trace_mode    trace_mode;
static FILE            *trace_out;

void
ST_trace_set(st_trace_mode mode, void *stream)
{
    trace_mode = mode;
    trace_out  = stream ? (FILE *) stream : stdout;
}

void
ST_print_object(st_oop p, char *buf, size_t buflen)
{
    char    name[256];

    if (buflen == 0)
        return;
    if (OM_is_int(p)) {
        snprintf(buf, buflen, "%d", (int) OM_int_value(p));
        return;
    }
    if (p == ST_NIL) {
        snprintf(buf, buflen, "nil");
        return;
    }
    if (p == ST_FALSE) {
        snprintf(buf, buflen, "false");
        return;
    }
    if (!OM_is_object(p)) {
        snprintf(buf, buflen, "<16r%X>", (unsigned) p);
        return;
    }
    /*
     *  Everything else prints as "a" followed by the name of its class.  The
     *  Xerox tracer has no case for classes: a class object's class is a
     *  metaclass, and naming a metaclass yields "Metaclass", so every class
     *  receiver in the reference traces reads "aMetaclass" rather than its
     *  own name.  We reproduce that rather than improve on it.
     *
     *  No vowel-initial class name appears in this position in either trace,
     *  so there is no evidence Xerox ever wrote "an"; we do not invent it.
     */
    {
        st_oop  cls = OM_fetch_class(p);

        if (OM_is_object(cls) && OM_fetch_class(cls) == OM_metaclass())
            snprintf(buf, buflen, "aMetaclass");
        else if (OM_class_name_of(cls, name, sizeof name))
            snprintf(buf, buflen, "a%s", name);
        else
            snprintf(buf, buflen, "<16r%X>", (unsigned) p);
    }
}

/*  ----------  Bytecode names  ----------  */

/*
 *  The Blue Book's own descriptions, as the Xerox tracer printed them.  Note
 *  the two-byte jumps print a trailing space and no offset, while the
 *  one-byte jumps print their count; that is what the reference traces show.
 */
static const char *const arithmetic_selectors[16] = {
    "+", "-", "<", ">", "<=", ">=", "=", "~=",
    "*", "/", "\\\\", "@", "bitShift:", "//", "bitAnd:", "bitOr:"
};

static const char *const special_selectors[16] = {
    "at:", "at:put:", "size", "next", "nextPut:", "atEnd", "==", "class",
    "blockCopy:", "value", "value:", "do:", "new", "new:", "x", "y"
};

static void
describe_bytecode(uint8_t code, char *buf, size_t buflen)
{
    if (code <= 15) {
        snprintf(buf, buflen, "Push Receiver Instance Variable %u", code);
    }  else if (code <= 31) {
        snprintf(buf, buflen, "Push Temporary Variable %u", code - 16);
    }  else if (code <= 63) {
        snprintf(buf, buflen, "Push Literal Constant %u", code - 32);
    }  else if (code <= 95) {
        snprintf(buf, buflen, "Push Literal Variable %u", code - 64);
    }  else if (code <= 103) {
        snprintf(buf, buflen, "Pop and Store Instance Variable %u", code - 96);
    }  else if (code <= 111) {
        snprintf(buf, buflen, "Pop and Store Temporary Variable %u", code - 104);
    }  else if (code == 112) {
        snprintf(buf, buflen, "Push self");
    }  else if (code == 113) {
        snprintf(buf, buflen, "Push true");
    }  else if (code == 114) {
        snprintf(buf, buflen, "Push false");
    }  else if (code == 115) {
        snprintf(buf, buflen, "Push nil");
    }  else if (code == 116) {
        snprintf(buf, buflen, "Push -1");
    }  else if (code <= 119) {
        snprintf(buf, buflen, "Push %u", code - 117);
    }  else if (code == 120) {
        snprintf(buf, buflen, "Return self From Method");
    }  else if (code == 121) {
        snprintf(buf, buflen, "Return true From Method");
    }  else if (code == 122) {
        snprintf(buf, buflen, "Return false From Method");
    }  else if (code == 123) {
        snprintf(buf, buflen, "Return nil From Method");
    }  else if (code == 124) {
        snprintf(buf, buflen, "Return Stack Top From Method");
    }  else if (code == 125) {
        snprintf(buf, buflen, "Return Stack Top From Block");
    }  else if (code == 128) {
        snprintf(buf, buflen, "Extended Push");
    }  else if (code == 129) {
        snprintf(buf, buflen, "Extended Store");
    }  else if (code == 130) {
        snprintf(buf, buflen, "Extended Pop and Store");
    }  else if (code == 131) {
        snprintf(buf, buflen, "Single Extended Send");
    }  else if (code == 132) {
        snprintf(buf, buflen, "Double Extended Send");
    }  else if (code == 133) {
        snprintf(buf, buflen, "Single Send to Super");
    }  else if (code == 134) {
        /*  Xerox names 133 "Single Send to Super"; 134 does not occur in
             *  either reference trace, so this name is by symmetry only.  */
        snprintf(buf, buflen, "Double Send to Super");
    }  else if (code == 135) {
        snprintf(buf, buflen, "Pop Stack");
    }  else if (code == 136) {
        snprintf(buf, buflen, "Duplicate Stack Top");
    }  else if (code == 137) {
        snprintf(buf, buflen, "Push Active Context");
    }  else if (code <= 151) {
        snprintf(buf, buflen, "Jump %u", code - 143);
    }  else if (code <= 159) {
        snprintf(buf, buflen, "Pop and Jump on False %u", code - 151);
    }  else if (code <= 167) {
        snprintf(buf, buflen, "Jump ");
    }  else if (code <= 171) {
        snprintf(buf, buflen, "Pop and Jump on True ");
    }  else if (code <= 175) {
        snprintf(buf, buflen, "Pop and Jump on False ");
    }  else if (code <= 191) {
        snprintf(buf, buflen, "Send %s", arithmetic_selectors[code - 176]);
    }  else if (code <= 207) {
        snprintf(buf, buflen, "Send %s", special_selectors[code - 192]);
    }  else {
        static const char *const counts[3] = {
            "No Arguments", "One Argument", "Two Arguments"
        };
        unsigned    argc = (unsigned) (code - 208) / 16;

        snprintf(buf, buflen, "Send Literal Selector #%u With %s",
                 (unsigned) (code - 208) % 16, counts[argc]);
    }
}

void
ST_trace_bytecode(uint8_t code, st_oop method, uint32_t ip)
{
    char    desc[128];

    (void) method;
    (void) ip;
    if (trace_mode != ST_TRACE_BYTECODES)
        return;
    describe_bytecode(code, desc, sizeof desc);
    fprintf(trace_out, "Bytecode <%u> %s\n", (unsigned) code, desc);
}

/*
 *  A send prints as the receiver followed by the selector with its
 *  arguments interleaved for keyword messages, which is how the reference
 *  traces read: "aMetaclass displayHeight: 480".
 */
void
ST_trace_send(st_oop receiver, st_oop selector, uint32_t argc,
              const st_oop *args)
{
    char        line[1024];
    char        piece[256];
    char        sel[256];
    size_t      used;
    uint32_t    i;
    int         depth;

    if (trace_mode == ST_TRACE_OFF)
        return;

    ST_print_object(receiver, piece, sizeof piece);
    OM_string_of(selector, sel, sizeof sel);

    used = 0;
    used += (size_t) snprintf(line + used, sizeof line - used, "%s", piece);

    /*
     *  The selector is printed whole and the arguments follow it, so a
     *  keyword message reads "origin:extent: aPoint aPoint" rather than
     *  being interleaved.  That is what the reference traces show.
     */
    used += (size_t) snprintf(line + used, sizeof line - used, " %s", sel);
    for (i = 0; i < argc && used < sizeof line; ++i) {
        ST_print_object(args[i], piece, sizeof piece);
        used += (size_t) snprintf(line + used, sizeof line - used,
                                  " %s", piece);
    }

    if (trace_mode == ST_TRACE_BYTECODES) {
        fprintf(trace_out, "[cycle=%llu]  %s\n",
                (unsigned long long) st_vm.cycle, line);
        return;
    }
    for (depth = 0; depth < st_vm.call_depth; ++depth)
        fputc('\t', trace_out);
    fprintf(trace_out, "[cycle=%llu]  %s\n",
            (unsigned long long) st_vm.cycle, line);
}

void
ST_trace_return(st_oop value, int from_block)
{
    char    piece[256];
    int     depth;

    if (trace_mode == ST_TRACE_OFF)
        return;
    ST_print_object(value, piece, sizeof piece);
    if (trace_mode == ST_TRACE_SENDS) {
        for (depth = 0; depth < st_vm.call_depth; ++depth)
            fputc('\t', trace_out);
    }
    fprintf(trace_out, "^ (%s) of %s\n", from_block ? "block" : "method",
            piece);
}

/*
 *  In send mode the primitive line sits at the same depth as the send it
 *  belongs to, which is the current call depth: the primitive is attempted
 *  before any context is activated.
 */
void
ST_trace_primitive(unsigned index)
{
    int depth;

    if (trace_mode == ST_TRACE_OFF)
        return;
    if (trace_mode == ST_TRACE_SENDS) {
        for (depth = 0; depth < st_vm.call_depth; ++depth)
            fputc('\t', trace_out);
    }
    fprintf(trace_out, "Primitive #%u\n", index);
}

/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Census: walk a loaded image and describe what is in it.
 *
 *  This exists to be checked against the reference dumps Xerox shipped on
 *  the 1983 tape -- the reference-count histogram and the list of every
 *  class with its object pointer.  Reproducing those exactly is the gate
 *  for the object memory, and it catches byte-order, bit-numbering and
 *  addressing mistakes at the earliest possible moment.
 */

#include "census.h"

#include <stdio.h>
#include <string.h>

/*
 *  Metaclass has no guaranteed object pointer, so find it rather than
 *  hardcode it.  Array is guaranteed; the class of Array is the metaclass
 *  "Array class"; and the class of any metaclass is Metaclass itself.
 */
st_oop
OM_metaclass(void)
{
    st_oop  array_class = OM_fetch_class(ST_CLASS_ARRAY);

    if (!OM_is_object(array_class))
        return ST_OOP_INVALID;
    return OM_fetch_class(array_class);
}

/*
 *  Instance variable layout, taken from the version 2 sources themselves
 *  rather than from the Blue Book text:
 *
 *      Behavior            superclass methodDict format subclasses
 *      ClassDescription    + instanceVariables organization
 *      Class               + name classPool sharedPools
 *      Metaclass           + thisClass
 *
 *  Note the trap: Behavior in the shipped v2 image has FOUR instance
 *  variables.  The Blue Book describes three, omitting `subclasses`, which
 *  puts every subsequent field off by one and makes a class's name read as
 *  whatever object happens to sit in `organization`.  Field indices below
 *  follow the image, which is what we must load.
 */
#define FIELD_NAME          6
#define FIELD_THIS_CLASS    6

/*  Copy a Symbol or String out of the image into a C string.  */
size_t
OM_string_of(st_oop p, char *buf, size_t buflen)
{
    uint32_t    len;
    uint32_t    i;

    if (buflen == 0)
        return 0;
    buf[0] = '\0';
    if (!OM_is_object(p))
        return 0;
    len = OM_fetch_byte_length(p);
    if (len > buflen - 1)
        len = (uint32_t) (buflen - 1);
    for (i = 0; i < len; ++i)
        buf[i] = (char) OM_fetch_byte(i, p);
    buf[len] = '\0';
    return len;
}

int
OM_class_name_of(st_oop p, char *buf, size_t buflen)
{
    st_oop  metaclass = OM_metaclass();
    st_oop  cls;

    if (buflen == 0)
        return 0;
    buf[0] = '\0';
    if (!OM_is_object(p) || metaclass == ST_OOP_INVALID)
        return 0;
    cls = OM_fetch_class(p);

    if (cls == metaclass) {
        /*  p is a metaclass: its name is "<thisClass> class".  */
        st_oop  this_class = OM_fetch_pointer(FIELD_THIS_CLASS, p);
        size_t  n;

        if (!OM_is_object(this_class))
            return 0;
        n = OM_string_of(OM_fetch_pointer(FIELD_NAME, this_class), buf, buflen);
        if (n == 0)
            return 0;
        strncat(buf, " class", buflen - strlen(buf) - 1);
        return 1;
    }
    if (OM_is_object(cls) && OM_fetch_class(cls) == metaclass) {
        /*  p's class is a metaclass, so p is a class.  */
        return OM_string_of(OM_fetch_pointer(FIELD_NAME, p), buf, buflen) > 0;
    }
    return 0;
}

/*  ----------  Method dictionaries  ----------  */

uint32_t
OM_method_dict_capacity(st_oop dict)
{
    uint32_t    len;

    if (!OM_is_object(dict))
        return 0;
    len = OM_fetch_word_length(dict);
    if (len < ST_MD_FIRST_KEY)
        return 0;
    return len - ST_MD_FIRST_KEY;
}

st_oop
OM_method_dict_key(st_oop dict, uint32_t slot)
{
    if (slot >= OM_method_dict_capacity(dict))
        return ST_NIL;
    return OM_fetch_pointer(ST_MD_FIRST_KEY + slot, dict);
}

st_oop
OM_method_dict_value(st_oop dict, uint32_t slot)
{
    st_oop  values;

    if (slot >= OM_method_dict_capacity(dict))
        return ST_NIL;
    values = OM_fetch_pointer(ST_MD_VALUE_ARRAY, dict);
    if (!OM_is_object(values) || slot >= OM_fetch_word_length(values))
        return ST_NIL;
    return OM_fetch_pointer(slot, values);
}

uint32_t
OM_walk_methods(om_method_visitor visit, void *user)
{
    st_oop      p;
    uint32_t    total = 0;

    for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
        st_oop      dict;
        uint32_t    capacity;
        uint32_t    slot;
        char        name[256];

        /*  Only classes and metaclasses carry a method dictionary.  */
        if (!OM_class_name_of(p, name, sizeof name))
            continue;
        dict     = OM_fetch_pointer(ST_BEHAVIOR_METHOD_DICT, p);
        capacity = OM_method_dict_capacity(dict);
        for (slot = 0; slot < capacity; ++slot) {
            st_oop  selector = OM_method_dict_key(dict, slot);
            st_oop  method;

            if (selector == ST_NIL || !OM_is_object(selector))
                continue;
            method = OM_method_dict_value(dict, slot);
            if (!OM_is_object(method))
                continue;
            ++total;
            if (visit)
                visit(p, selector, method, user);
        }
    }
    return total;
}

void
OM_census(om_census *c)
{
    st_oop  p;

    memset(c, 0, sizeof *c);
    for (p = OM_first_object(); p != ST_OOP_INVALID; p = OM_next_object(p)) {
        unsigned    count = OM_count_bits(p);

        ++c->objects;
        if (count >= OM_HISTOGRAM_BUCKETS)
            count = OM_HISTOGRAM_BUCKETS - 1;
        c->refcount_histogram[count] += 1;
        c->total_refcounts += count;
        c->total_words     += OM_size_bits(p);
        if (OM_pointer_bit(p))
            ++c->pointer_objects;
        else
            ++c->nonpointer_objects;
        if (OM_odd_bit(p))
            ++c->odd_objects;
    }
#ifdef ST_OM_BB
    {
        uint32_t    entry;

        /*  Iterate in 32 bits; an st_oop counter would wrap at 65534.  */
        for (entry = 2; entry < st_om_ot_limit; entry += 2) {
            if (OM_free_bit((st_oop) entry))
                ++c->free_entries;
        }
    }
#else
    c->free_entries = OM_oops_left();
#endif
}

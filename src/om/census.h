/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Image census, for validation against the Xerox reference dumps.
 */

#ifndef ST_CENSUS_H
#define ST_CENSUS_H

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t    objects;
    uint32_t    free_entries;
    uint32_t    pointer_objects;
    uint32_t    nonpointer_objects;
    uint32_t    odd_objects;
    uint64_t    total_refcounts;
    uint64_t    total_words;
    uint32_t    refcount_histogram[ST_HUGE_SIZE];
} om_census;

void    OM_census(om_census *c);

/*  The Metaclass class itself, discovered rather than hardcoded.  */
st_oop  OM_metaclass(void);

/*  Copy a Symbol or String into a C string; returns the length copied.  */
size_t  OM_string_of(st_oop p, char *buf, size_t buflen);

/*
 *  If p is a class or a metaclass, write its printable name and return 1.
 *  Otherwise return 0.
 */
int     OM_class_name_of(st_oop p, char *buf, size_t buflen);

/*  ----------  Method dictionaries  ----------  */

/*
 *  A MethodDictionary's shape, from the version 2 sources:
 *
 *      Collection variableSubclass: #Set                 'tally'
 *      Set        variableSubclass: #Dictionary          ''
 *      Dictionary variableSubclass: #IdentityDictionary  'valueArray'
 *      IdentityDictionary variableSubclass: #MethodDictionary  ''
 *
 *  so field 0 is the tally, field 1 is the Array of CompiledMethods, and the
 *  indexed part from field 2 onward holds the selector Symbols, nil where a
 *  slot is empty.  Selector at indexed slot i pairs with the method at index
 *  i of the value array.
 *
 *  This is the layout method lookup walks, so getting it right here is worth
 *  more than the census alone: it is validated against Xerox's method.oops
 *  dump before the interpreter ever depends on it.
 */
#define ST_MD_TALLY         0
#define ST_MD_VALUE_ARRAY   1
#define ST_MD_FIRST_KEY     2

/*  Instance variable index of methodDict within any Behavior.  */
#define ST_BEHAVIOR_METHOD_DICT 1

uint32_t    OM_method_dict_capacity(st_oop dict);
st_oop      OM_method_dict_key(st_oop dict, uint32_t slot);
st_oop      OM_method_dict_value(st_oop dict, uint32_t slot);

/*
 *  Call back for every (class, selector, method) triple in the image.
 *  Returns the number of methods visited.
 */
typedef void (*om_method_visitor)(st_oop cls, st_oop selector, st_oop method,
                                  void *user);

uint32_t    OM_walk_methods(om_method_visitor visit, void *user);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_CENSUS_H  */

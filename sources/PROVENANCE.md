# Provenance

These are the Smalltalk-80 version 2 class sources from

    https://github.com/markbush/Smalltalk-80-Sources

vendored at the commit recorded below, under the MIT licence in `LICENSE`.

This matters. The Xerox `VirtualImage` and `Smalltalk-80.sources` in `oracle/`
carry **no licence grant from anyone** and are used only as a private
development oracle -- never copied from, never redistributed. See
`doc/LICENSING.md`. The image this system ships is built from the files in
*this* directory, which are MIT, so it carries no Xerox provenance.

## What was taken

Upstream stores each class three ways: a `.stClass` chunk-format fileIn, a
`.json` summary of the class definition, and one `.st` file per method under
`instance/` and `class/`. The `.stClass` files are the authoritative form and
contain everything the other two do, so only those are vendored, together with
`ClassHierarchy.txt` (which gives a load order that respects superclasses) and
the licence.

`.stClass` is ordinary Blue Book chunk format, which `src/compiler/chunk.c`
already reads.
Commit: 89b50640e335cc5aeca7c768570ca28e2725b10d
Date:   Wed Mar 27 20:10:13 2024 +0000

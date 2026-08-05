# Licensing and Provenance

## Our code

Everything under `src/`, `tests/` and `doc/` is original work,
Copyright (c) 2026 Blake McBride, BSD 2-Clause.

## The `oracle/` directory — never redistribute

`oracle/` holds the Xerox Smalltalk-80 version 2 virtual image, its sources
file, and the Xerox reference execution traces, unpacked from Mario Wolczko's
Manchester distribution.

**No host of these files asserts any license grant.** Not Wolczko's site, not
the archive.org mirror (`licenseurl = None`, `rights = None`), not dbanay's
repository (whose MIT license covers only his C++ code). Xerox released
Smalltalk-80 v2 in 1983 under a restrictive license and licensed it
royalty-free to Apple, HP, Tektronix and DEC. It has been treated as
abandonware for roughly two decades with no known enforcement, but there is no
affirmative permission anywhere.

Accordingly:

- `oracle/` is in `.gitignore` and must stay there.
- These files are used as a **private development oracle only** — to validate
  our interpreter against `trace2`/`trace3` and to inspect object layouts.
- No part of the image or its sources is copied into our code or into the
  shipping image.

## The shipping image

The bootstrapped image is built from
[`markbush/Smalltalk-80-Sources`](https://github.com/markbush/Smalltalk-80-Sources),
which is **MIT licensed** — the complete 1983 class library, exploded to one
file per method. This is what gives the product clean provenance.

Vendor it into `sources/`, retaining its LICENSE file.

## Reference material

| Work | Status |
|---|---|
| Blue Book (*Smalltalk-80: The Language and Its Implementation*) | Author-permitted free distribution via the INRIA RMoD and Ducasse mirrors. Not public domain, no CC grant |
| Blue Book Ch. 26–30 HTML (Dwight Hughes / mirandabanda.org) | Explicit permission from Goldberg and Robson recorded on the page; copyright retained by Adele Goldberg |
| Green Book (*Bits of History, Words of Advice*) | Same INRIA mirror, same status |
| Orange Book (*The Interactive Programming Environment*) | Same |
| ANSI Smalltalk draft rev 1.9 | Freely hosted on the Squeak wiki; the published INCITS 319-1998 standard is a paid document |

Reference implementations we read but do not copy from: `dbanay/Smalltalk`
(MIT, C++), `devhawala/ST80` (BSD-3, Java), `avwohl/smalltalk80-2026` (MIT,
C++). `rochus-keller/Smalltalk` is GPL — **read it for understanding only, and
do not copy code from it**, since we ship BSD.

# System-Time

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/System-Time` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-12 |

## Files taken

All 28 `.st` files, byte for byte.

## Local edits

None.

## Why this package

Chronology is Tier 1 in `doc/PLAN-PHARO.md`, and it is the concrete thing
standing between `Announcements-Core` and a completely green suite: five of
its tests fail on `Message not understood: seconds`, which is
`Integer>>seconds` answering a `Duration`.

It is a much larger bite than Announcements — twenty-eight classes, and
unlike Announcements it touches the outside world, since `DateAndTime now`
has to ask somebody what time it is.


## First load, 2026-08-12

It loads: 4,199 symbols, 46 class initializers. And the thing it was
imported for works —

    (3 seconds) class name        ->  Duration

Four gaps had to be closed to get that far, none by editing these files:

    SharedPool          ChronologyConstants subclasses it; nine of the
                        classes here declare `#pools : ['ChronologyConstants']`.
                        1983 has pool dictionaries but no class to subclass
                        for the purpose, so lib/Kernel/SharedPool.class.st
                        is ours.  Almost empty on purpose: the bootstrap
                        already resolves a pooled class's names and
                        remembers the binding (pool_bindings in
                        bootstrap.c); what was missing was something for the
                        pool class to BE.
    Date, Time          exist in 1983 too, and are SUPERSEDED rather than
                        excluded, because the rest of the package is built
                        on Pharo's.
    VirtualMachine      excluded: the extension describes a VM that is not
                        this one.  It is the only one of the five extension
                        files whose class we lack; the other four extend
                        Integer, Number, String and BlockClosure, which is
                        where `3 seconds` comes from.

STILL BROKEN, and classified rather than chased:

    generality          `3 seconds printString` fails here.  Number
                        coercion: Duration is a Magnitude, and something in
                        the arithmetic path asks it for a generality it does
                        not answer.
    findKeyOrNil:       raised repeatedly during class initialization.
    value: / 'only integers should be used as indices'
                        follow it, and are probably the same failure seen
                        downstream rather than three separate ones — but
                        that has NOT been checked, and saying so is cheaper
                        than assuming it.

This package is a much larger bite than Announcements-Core: 28 classes, and
the first import whose gaps include VM services rather than only missing
protocol.  The 1983 image still sends Time millisecondClockValue,
millisecondsToRun:, now, and Date today, newDay: — whether Pharo's
superseding classes answer those has not been tested yet and is the first
thing to check next.

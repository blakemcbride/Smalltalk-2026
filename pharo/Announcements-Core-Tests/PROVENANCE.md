# Announcements-Core-Tests

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/Announcements-Core-Tests` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-12 |

## Files taken

All 8 `.st` files, byte for byte.

## Local edits

None.

## Why these matter more than the package they test

They were written by somebody else.  Every other check in this system
tests what we believed was correct, which is the class of test that agreed
with every wrong diagnosis recorded in `doc/SCALING.md`.  A ported suite is
the ratchet's only external oracle, and the one that eventually lets the
Blue Book trace retire.

## First run, 2026-08-12

The package loads: 275 classes, 5072 methods with the tests included.
Individual tests run and pass, which is the first time a check written by
somebody else has been green on this VM:

    (AnnouncerTest selector: #testAnnounceClass) run
        1 run, 1 passed, 0 failed, 0 errors

Running the whole class stops after four.  Two gaps, both worth having:

`testAnnounceWorkWithinExceptionHandlerBlocks` HANGS.  It sends
`NotFound signal`, and NotFound is a Pharo exception class we do not have,
so the undeclared global is nil and `nil signal` happens inside an `on:do:`.
That hangs rather than raising, which is the "quiet failure" shape this
project keeps finding: a doesNotUnderstand inside an exception handler has
nowhere to go.  Worth fixing in our exception machinery regardless of
Pharo -- the VM should not be able to hang on a nil receiver.

`testAnnouncingReentrant` ERRORS.  It uses `when:do:for:`, which
Announcer does not define here; the subscription convenience API lives in
a package not yet imported.  An ordinary dependency.

Neither is a reason to edit these files.

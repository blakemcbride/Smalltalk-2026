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

## After fixing the hang, same day

The hang was ours and is fixed (Exception>>findHandlerFrom: was sending
#handles: to a nil guard).  All 29 of AnnouncerTest's tests now RUN:

    6 passed, 1 failed, 22 errors

That is the ratchet's real number for this package, and it is meant to
climb.  Most of the 22 errors are expected to be the same missing
subscription API -- when:do:for: and friends -- rather than 22 separate
problems; the next turn should classify them before fixing anything.

## Classified, and two methods later

Grouping the errors by message first was worth doing: the guess above was
wrong.  They were not one missing API but several small protocol gaps.

    11  includesBehavior:      Behavior, one line
     4  receiver:selector:     MessageSend, a class we do not have
     4  add:
     2  isNotNil               Object, one line
     1  signal                 the nil NotFound above
     1  FAIL assertion failed

Adding the two one-line methods took the package from 6 passed to 10.

What is left is more interesting than what was fixed:

    9  on:fork:               Pharo's exception-forking handler

on:fork: is implemented in Pharo over runUntilErrorOrReturnFrom: and its
context surgery -- exactly the process machinery doc/PLAN-PHARO.md's Phase
E declined on purpose, in favour of ANSI/early-Squeak semantics.  Nine of
these tests want it.  That is a real boundary of the port rather than a
gap to be closed casually, and it should be decided deliberately.

    4  MessageSend receiver:selector:

A small Kernel class we do not have; bounded work, and the obvious next
thing to add.

    1  FAIL assertion failed

Examined, and it was worth doing first: it led to MessageSend, which was
also the cause of four of the errors.

testSymbolIdentifier wraps its body in `on: MessageNotUnderstood do:' and
asks whether the selector not understood was #bar.  So every missing
method along the delivery path arrives here as a WRONG ANSWER rather than
as an error, and each one has to be read out rather than guessed.  Printing
the caught selector instead of comparing it named them one per run:

    receiver:selector:      MessageSend, added
    handlesAnnouncement:    Symbol, added (Pharo has it in
                            Collections-Strings, which we will not import)
    prepareForDelivery      Symbol, added
    on:fork:                the boundary below

So this test is no longer unexplained: it is blocked by on:fork: like the
twelve others, and its remaining failure is the same single decision
rather than a separate defect.


## Green, 2026-08-12

    AnnouncerTest          29 passed, 0 failed, 0 errors
    AnnouncementSetTest     2 passed, 0 failed, 0 errors
    WeakAnnouncerTest       0 passed, 0 failed, 5 errors

31 of 36.  The five that do not run fail on `Message not understood:
seconds' -- Chronology's Duration protocol, which we do not have.  NOT weak
references, which is what the class name suggests and what a guess would
have recorded; the tests use a delay to let a weak subscription be
collected, and never reach the weak part.  Chronology is Tier 1 and is a
ratchet turn of its own.

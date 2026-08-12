# System-Time-Tests

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/System-Time-Tests` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-12 |

## Files taken

All 24 `.st` files, byte for byte.

## Local edits

None.

## Why

Three times the size of `Announcements-Core-Tests`, and the largest body of
checks written by somebody else that this VM has been asked to satisfy.
The eight files of the Announcements suite found a VM hang, a loader rule
that left every Pharo class subclassing a 1983 collection half-built, an
arity bug and a scheduler race. This is twenty-four.


## First run, 2026-08-12

21 test classes load (24 files; three are extensions or manifests). Two
gaps had to be closed for that: `ClassTestCase`, which ten of them
subclass and which is ours rather than an import, and two extension files
for `ExceptionTest`/`ExceptionTester`, classes from a package not yet
imported, excluded in the profile.

Then they found a VM bug, which is what they are for.

### Heap corruption from a frame overflow

`DateAndTimeLeapTest>>testAsMonth` crashes the image:

    corrupted size vs. prev_size

ASAN names it exactly: `ST_push` (interp.c:110) writing past the end of a
MethodContext, from `OM_store_pointer`. The method needs more frame slots
than the largest context has, so the push runs off the object and into the
next one's header. glibc notices thousands of bytecodes later, in whatever
unrelated code touches the heap next.

`doc/PLAN.md` predicted this in as many words — *"emit a compiler error,
not the silent heap corruption"* — and the check was never written.

**And writing it is not as simple as the plan makes it sound.** The obvious
version, failing compilation when `temporary_count + max_stack_depth > 32`,
rejects **47 methods of the 1983 library** that have always worked,
`SortedCollection>>sort:to:` among them. They exceed the bound by 1 to 4
slots, which means `max_stack_depth` is a conservative upper bound and not
the depth actually reached. A gate on it would be a gate on an
over-estimate.

So the fix is one of:

1. make `max_stack_depth` exact, and then the plan's error is correct;
2. bounds-check `ST_push` — one comparison on the hottest path in the
   interpreter, so it wants measuring before adopting;
3. size contexts to the frame they need, rather than choosing between 12
   and 32. The two sizes are a 1983 format constraint and the `OM=mt`
   image format is ours.

(3) is the one that makes the problem go away rather than reporting it.

### Not yet run

`BlockClosureValueWithinDurationTest` and `BlockClosureValueWithinTest`
hang: `valueWithin:onTimeout:` needs `Delay`, and the run deadlocks with
"every process is blocked". Set aside rather than counted.

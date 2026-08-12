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

### The whole suite, run

One crashing class must not stop the others, so each is run in its own
process. Every one of the 21 test classes now has a result:

```
DateAndTimeDosEpochTest              40 passed,  2 failed, 21 errors
DateAndTimeEpochTest                 42 passed,  2 failed, 20 errors
DateAndTimeLeapTest                  CRASHED  (the frame overflow above)
DateAndTimeTest                      13 passed,  0 failed, 41 errors
DateAndTimeUnixEpochTest             41 passed,  2 failed, 20 errors
DateParsingTest                       0 passed,  0 failed, 20 errors
DateTest                              0 passed,  0 failed, 53 errors
DosTimestampTest                      0 passed,  0 failed,  3 errors
DurationTest                         48 passed,  4 failed, 15 errors
MonthTest                            10 passed,  0 failed,  7 errors
ScheduleTest                          1 passed,  0 failed,  9 errors
StopwatchTest                        no answer
TimespanDoSpanAYearTest               0 passed,  0 failed,  4 errors
TimespanDoTest                        0 passed,  0 failed,  8 errors
TimespanTest                         24 passed,  3 failed, 31 errors
TimeTest                              0 passed,  0 failed, 51 errors
WeekTest                              0 passed,  0 failed,  9 errors
YearMonthWeekTest                     6 passed,  1 failed,  1 error
YearTest                              5 passed,  0 failed,  2 errors
BlockClosureValueWithinDurationTest  no answer
BlockClosureValueWithinTest          no answer

                                    230 passed, 14 failed, 315 errors
```

**230 of Pharo's own Chronology tests pass on this VM.** That is the
ratchet's number for this package and it is meant to climb.

The three that answer nothing all involve waiting: `valueWithin:onTimeout:`
and `Stopwatch` need `Delay`, and the run deadlocks with "every process is
blocked". They are a `Delay` gap, not a Chronology one.

Several classes score zero — `DateTest`, `TimeTest`, `DateParsingTest`,
`WeekTest` — which usually means one missing method in `setUp` rather than
53 separate problems, exactly as `AnnouncerTest` had eleven errors from one
missing `includesBehavior:`. Classify before fixing: group the errors by
message first.

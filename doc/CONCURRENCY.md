# The Concurrency Contract

This document defines how Smalltalk-2026 differs from Smalltalk-80 in the one
place where it deliberately does. It is normative. Read it before writing any
Smalltalk code for this system.

## The short version

**Smalltalk-80 guarantees that a process is never preempted by another process
of the same priority. Smalltalk-2026 does not.**

Two processes at the same priority may execute simultaneously on different CPUs.
That is the entire point of the system.

## Why this matters more than it sounds

The Blue Book scheduler contract reads:

> The scheduler is cooperative between processes of the same priority, and
> preemptive between processes of different priorities.

In a green-threaded Smalltalk this is not merely a scheduling policy — it is an
**implicit mutual-exclusion primitive**, and the 1983 class library leans on it
everywhere. Every unsynchronized read-modify-write of an instance variable, a
class variable, or a shared collection is correct *only* because no other
process at the same priority can run between two bytecodes.

Code that raises its priority to get an atomic section is using the same trick
deliberately. Under true parallelism that trick silently stops working: raising
your priority no longer stops anyone else, because they are on another core.

This is why the class library must be audited rather than inherited. See
Pallas & Ungar, *Multiprocessor Smalltalk* (PLDI '88), whose taxonomy we adopt:
for each piece of shared state, choose **serialize** (lock it), **replicate**
(give each worker its own), or **reorganize** (remove the sharing).

## What is guaranteed

1. **Bytecode-level memory safety.** The object memory never tears. A field read
   yields some value previously written to that field, never a mixture. Object
   headers, the object table, and the class of an object are always consistent.

2. **`become:` is atomic.** Identity mutation is two object-table stores,
   done at a safepoint, so no thread ever observes a half-completed
   `become:`. It used to be two stores with every other worker still
   running, and between them both object pointers named the same body --
   which matters more here than in a green system, because `become:` is not
   exotic in the 1983 library: `Set` and `Dictionary` grow through one, and
   so does every global added to `Smalltalk` at run time. What keeping an
   object table buys is that the safepoint is O(1) rather than a
   stop-the-world scan of every reference in the heap.

3. **`Semaphore>>#signal` and `#wait` are atomic** with respect to each other
   and to process suspension and resumption.

4. **Garbage collection is invisible.** Collection happens at safepoints, which
   the interpreter polls at message sends and backward jumps. No Smalltalk code
   observes a partially collected heap.

5. **Method lookup is coherent.** A class recompiled on one worker becomes
   visible to all workers; sends in flight complete against a consistent method
   dictionary.

## What is NOT guaranteed

1. **Same-priority atomicity.** Gone. See above.

2. **Priority as a mutual-exclusion mechanism.** Priorities are scheduling
   hints. A higher-priority process is *preferred*, not *exclusive*.

3. **Thread safety of the base collections.** `OrderedCollection`, `Dictionary`,
   `Set`, `Bag` and friends are **not** synchronized. This follows Java's
   post-`Vector` lesson: paying for a lock on every access to serve the rare
   shared case is the wrong default. Use the explicitly shared variants, or
   guard them yourself.

4. **`Transcript` interleaving.** One send — one `show:`, one `cr` — is
   atomic, since the Transcript's entry stream is written under a lock
   (`lib/Concurrency/TextCollector.extension.st`); lines from different
   processes arrive in whatever order they were sent. Build a line in a
   `WriteStream` and `show:` it in one send if it must stay together.

   **Headless it goes to standard error.** A `-serve` image has no
   Transcript window, so `self changed: #appendEntry` reached nobody and
   every entry was built, locked and dropped — output that failed without a
   word. `endEntry` now writes the entry to standard error when there are no
   dependents, carriage returns translated to newlines, which is where
   `printNl` and `displayNl` already go. Redirect `2>`, not `>`.

## What replaces it

The base library adds:

| Class | Purpose |
|---|---|
| `Mutex` | Non-reentrant mutual exclusion. `aMutex critical: [ ... ]` |
| `Monitor` | Reentrant lock with condition variables. `waitWhile:`, `signalAll` |
| `SharedQueue` | Multi-producer, multi-consumer, blocking |
| `Promise` | A value another process will supply. `aPromise wait` |
| `Processor>>#forkParallel:` | Fork onto the worker pool explicitly |

`Semaphore` remains, with Blue Book semantics, now genuinely atomic.

## The thread map

```
Thread 0        Dedicated SDL pump. Owns the window, renderer and texture.
                Never executes Smalltalk bytecodes.

Threads 1..N    Smalltalk workers, N ~ CPU count. Run bytecodes, allocate from
                thread-local buffers, poll safepoints. Never call SDL video.

Delay timer     One request outstanding, armed by primitive 100 or 136. Sleeps
                on a condvar until the deadline, then posts to the async signal
                queue. Never executes Smalltalk bytecodes, never touches the
                object memory beyond one refcount on the semaphore it holds.
```

Thread 0 is not a worker by design. If it were, it could be parked in a GC
safepoint at the moment the window server needs a response, deadlocking the
compositor. SDL3's own rules also force this: `SDL_PumpEvents`, `SDL_WaitEvent`,
`SDL_CreateRenderer` and `SDL_LockTexture` are main-thread-only, and on macOS
"main thread" means the thread that entered `main()` — that is not negotiable.

Smalltalk `Process` objects remain green and cheap, multiplexed M:N over the
worker pool with per-worker ready queues and work stealing. `Processor
activeProcess` is per-worker state.

The timer has to be a thread and not a poll in the idle loop, and the reason is
the whole nature of a delay: it expires while nothing is running. With every
process waiting on one, every worker is in the idle loop, and a loop that only
looks at what is already ready will look forever. Something outside the
scheduler has to hold the clock.

Two rules came out of building it, both learned by getting them wrong:

- **A timer that has fired but not yet delivered is still pending.** Clearing
  the armed flag before posting the signal leaves a window — microseconds wide,
  perfectly reliable — in which a waiter asks "is anything still coming?", hears
  no, and gives up just before it arrives. Every `Delay` in the system
  deadlocked there.
- **A new producer turns latent races into real ones.** TSAN found two the
  moment the timer existed, both in the async signal queue and both older than
  the timer: `drain_async_signals` read `async_count` unlocked to skip an empty
  queue, and `async_lock_init` was lazy, so whichever thread posted first ran
  `pthread_mutex_init` on a mutex another thread might already be locking. Until
  the timer, every producer was a worker and neither could be caught. The count
  is now atomic — the unlocked shortcut stays, with a defined meaning: a stale
  zero costs one more pass and nothing else — and the lock is made on the thread
  that arms the timer, which always runs before the thread that delivers.
  **TSAN reports only races that actually executed**, and the code that executes
  is the code some test exercises; no test had ever waited on a delay.
- **There is one millisecond clock.** Primitive 99 read the monotonic counter
  and primitive 135 read milliseconds since 1901; both called themselves the
  millisecond clock and were eight hours apart. The image computed a deadline on
  one and the VM compared it against the other. `ST_time_ms_clock` is now the
  only source, and primitive 240 stays the wall clock that dates are read from.

## The safepoint's contract, and one way it was broken

While a safepoint is held, every worker other than the requester must be parked
or finished. That is what makes the collector's access to the object table
exclusive, and everything else in this document rests on it.

The hazard worth recording is how easily that contract admits a *third* state.
`WORKER_start` publishes `worker_count` before it creates any threads, so the
requester walks every slot; but each worker originally announced itself by
setting its own `running` flag as its first instruction. Between the two, a
worker that had been created and had not yet started read `running == 0` — the
same thing a worker that had already *finished* reads, and finished workers must
be skipped or the protocol never terminates. So the requester skipped it,
collected, and that worker's first act was to allocate into the table being
swept. The object it made had no references yet, so the collector reclaimed it
and handed its table slot to the next allocation, which freed the body its
creator was still initialising.

Two lessons generalise:

- **Not-yet-started and already-finished are opposite obligations, and one flag
  cannot carry both.** Workers now have `running` and `exited`, and are marked
  running before they are created.
- **Assert the invariant, not its consequences.** As a corrupted-memory symptom
  this reproduced about twice in twenty-five runs under ASAN, in a different
  place each time. `WORKER_unparked_count()` checks the property itself from
  inside the safepoint, and catches the same bug on every single run.

## Roots are the walk, not the count

A marking collection rebuilds every reference count from the root walk, so a
reference held only in C protects nothing — `interp.h` says exactly that, and
the bootstrap ignored it for months. It held 3601 symbols, every class and
metaclass object and every class-variable binding in C arrays, with
`OM_increase_ref` called on each, and none of it was visited.

The symptom was not a crash. It was that `BOOT_string_hash` answered two
different values for the same symbol depending on whether a collection had
happened in between: the symbol had been freed, its object-table slot handed
to something else, and the bytes read back belonged to a different string.
A crash would have been kinder. `BOOT_provide_roots` now visits all of it,
and a caller that installs its own provider must chain to it.

The same rule reaches into every primitive that makes more than one object,
because an allocation can run a full collection — the table fills, or another
worker's safepoint is entered by way of the allocator — and between two
allocations the first object is held by nothing the walk can see. The
directory-listing primitive made every name a String and then the Array to
hold them, keeping the Strings in a C array meanwhile; on thirty-one workers
with the collector forced along, a listing came back with a name that was
no longer an object. The order is: make the container first, `ST_push` it
so the active context's stack — a root — holds it, then make each element
and store it the moment it exists. `Smalltalk arguments` was written that
way; the directory listing and the ODBC column description are now too.

## A process belongs to nobody twice on its way to running

Scheduling a process is a sequence of handoffs, and at three points in it the
process is referred to by nothing at all. `removeFirstLinkOf:` unlinks it from
the list it was waiting on; the caller resumes it; resuming stores it on a run
queue or nominates it to run next. The list was the only thing holding it, so
the moment it is unlinked its count reaches zero and the collector is entitled
to it.

All three variants of this bug were found, and each looked like something else:

- **Written after being released.** Chapter 29 unlinks the link and then sends
  `nextLink: nil` to it. Where nothing is counting that is fine. Here the store
  landed in a freed body.
- **Used after being released.** `SCHED_synchronous_signal` was
  `SCHED_resume(SCHED_remove_first_link(semaphore))`. The removal returned a
  process that had already been reclaimed, `SCHED_resume` found it not present
  and returned, and the system reported that every process was blocked --
  pointing at the scheduler, which was not wrong and was not where the bug was.
- **Held only in C.** `SCHED_transfer_to` parks the nominated process in a C
  variable until the interpreter reaches a point where it can switch. A
  reference held only in C protects nothing (see below), so the nomination was
  not a reference; the process was collected between being chosen and being
  run, and its object-table slot was reused. The symptom was
  `aMethodContext does not understand #priority`, sent from the scheduler.

The fix is one rule: **`SCHED_remove_first_link` returns a held reference, and
every caller releases it only once something in the object memory holds the
process.** The nomination counts what it holds and the root walk visits it.

That last symptom is the useful one to recognise. A message sent to an object
of a class that has no business being there is rarely a wrong send -- it is
almost always a freed object whose slot has been handed to something else.

The REST server's gates found three more of the same family, months later:

- **A link in the middle of a list is held only by the link before it.**
  `SCHED_remove_first_link` cleared the removed link's `nextLink` before the
  list had taken hold of what it pointed at, so with three or more processes
  queued the second lost its only reference and the release cascaded down
  the chain. The list now takes hold of `next` first; `remove_link_from_list`
  the same.
- **One slot written by every worker.** The scheduler's `activeProcess`
  instance variable is stored on every switch by every worker, and
  `OM_store_pointer` is read-old, store-new, release-old as three steps. Two
  workers switching at once both read the same old occupant and both
  released it, and the value stored between was never released. The Delay
  timing process, which every worker runs in turn, lost a count that way
  about once a minute on 31 workers and was freed while linked on a
  semaphore. The slot is exchanged atomically now (`OM_exchange_pointer`);
  a slot that more than one worker writes must be.
- **A run that ends with a nominee.** `SCHED_check_process_switch` runs
  before the loop looks at `running`, so a worker whose process had just
  returned off the bottom could still drain, nominate and switch, then go
  home holding the nominee. A finished run neither drains nor switches, and
  `ST_interp_run` hands a nominee back to its ready list on the way out.
  `-serve` never ends a run; a test harness does.

And a fourth in a neighbouring family: `set_active_context` now reports an
oop that is not a context -- with the method, the chain and every worker --
and stops, instead of loading registers from a String's bytes and
dereferencing them.

ThreadSanitizer, run over the same gates, added two:

- **Instance enumeration walked the table while workers allocated.**
  `someInstance` / `nextInstance` read every header's flags and class, and a
  freed header is reused in place by the next allocation that takes its
  slot, so the walk could answer an object half of one thing and half of
  another. `Symbol rehash` walks every Symbol. With workers running the walk
  now happens at a safepoint; it is slow per instance, and enumeration is a
  tool, not a path.
- **The timer's once-init flag** was a plain int written by the first worker
  to arm a Delay and read by every idle worker asking whether a wait is
  pending. It is an atomic three-state now, and two workers arming at once
  no longer both initialise the lock.

## The library's implicit locks, and the first one found

The audit this document asks for -- serialize, replicate, or reorganize every
piece of shared state the 1983 library leans on -- has its first concrete
entry, and it is worth recording in full because of what it looked like.

`SmallInteger>>printOn:base:` writes each digit into `Digitbuffer`, a class
variable holding one `Array new: 32`, and reads them back in reverse. Under a
green scheduler that is correct: no process of the same priority can run
between the write and the read. Here eight workers printing at once write into
the same thirty-two slots and each reads back whichever digits landed last.

What it looked like was **anything but printing**. `i printString = i
printString` on eight workers was true 1,501 times in 16,000. A JSON document
written while other workers were writing their own came back with `00` where
`100` had been put. The diagnostics printed to explain it were themselves
wrong, because they printed integers to say so. And every tool said the code
was clean: ThreadSanitizer saw nothing, because a class variable read and
written by Smalltalk is not a data race in any C; an instrumented allocator
showed no object-table index ever handed out twice; and a build that never
recycled a freed slot, with an abort on any read of a freed object, still
failed at the same rate without ever aborting. Three days of plausible
theories about the collector were ruled out by that last experiment in one
run, which is the argument for building the experiment before the theory.

The fix is **replicate** in the taxonomy above: `lib/Concurrency` replaces the
method with one whose buffer is a temporary, sixty-four wide because this
system's SmallIntegers are 63 bits and `(2 raisedTo: 60) printStringRadix: 2`
had been overrunning the 1983 buffer on its own, with no second worker
needed. `tests/unit/test_parallel_lib.c` holds it: every worker prints every
integer from 1 to 2,000 and checks the length, the first digit and the last.

`Digitbuffer` has no other user. The other class variables in `sources/` that
hold mutable state -- `CachedClassNames`, `TempNameCache`, the `Symbol`
table, the `Transcript` -- are caches and structures rather than per-call
scratch space, and each is a separate question for the same audit.

## The audit, and what it found

The audit was run on 2026-08-25, against every class variable in `sources/`,
`lib/` and `pharo/` (245 of them, 71 written after class initialization, 7
mutated in place) and then against the image itself: a battery of kernels, each
run on eight and on thirty-one workers, each answering a number that only a
correct run can produce. `tests/unit/test_parallel_shared.c` is that battery,
kept. What it found in the library, and what was done:

| Shared state | What eight workers did to it | Taxonomy | Where |
|---|---|---|---|
| `Symbol class>>intern:` — `USTable`, read-modify-write | 1,170 of 2,400 symbols not identical across workers; 132 of 600 at two | serialize | `lib/Concurrency/Symbol.extension.st`, under `LibraryLocks` |
| `Smalltalk at:put:` — a `Dictionary` that grows by `become:` | 245 of 1,600 globals lost; and an unlocked *reader* mid-probe when the table was replaced missed a key that was there | serialize, readers too | `lib/System/SystemDictionary.extension.st` |
| `Object>>addDependent:` — `DependentsFields`, one `IdentityDictionary` for every model | 143 of 1,600 dependents lost; 18 of 400 at two | serialize; `dependents` answers a copy | `lib/Concurrency/Object.extension.st` |
| `Compiler evaluate:` — installs `#DoIt` in the receiver's class, sends it, removes it | 138 of 800 answers nil, each a worker whose `DoIt` another had removed | reorganize: primitive 188, `withArgs:executeMethod:` | `lib/Concurrency/Compiler.extension.st`, `src/interp/prim.c` |
| `Delay` — the timing process, `initSignals`, a cancelled timer | two workers hung every run | reorganize: a stale signal is a look at the clock | `lib/Concurrency/Delay.extension.st` |
| `ProcessorScheduler>>activePriority`, `terminateActive` — read the `activeProcess` *variable* | a process forked with another worker's priority; a `yield` helper terminating another worker's process | replicate: ask this worker | `lib/Concurrency/ProcessorScheduler.extension.st` |
| `ProcessorScheduler>>yield` — a helper process per call, its block context shared with the caller | one run in two failed at thirty-one workers after everything else was fixed | reorganize: primitive 167 | same, `src/sched/st_sched.c` |
| `CompiledMethod>>setTempNamesIfCached:`, `SystemDictionary>>classNames` — a cache read twice | could tear; not seen to | replicate: read once | `lib/Concurrency`, `lib/System` |
| `FileDirectory` — `ExternalReferences` | add and remove with nothing between | serialize | `lib/Files-Fixes/FileDirectory.extension.st` |
| `Behavior>>addSelector:withMethod:` — a method dictionary | not exercised; the same find-then-write | serialize the write | `lib/Concurrency/Behavior.extension.st` |
| `Transcript` — one `WriteStream` on one `String`, grown by `become:` | eight workers' lines in each other's bytes; ThreadSanitizer saw the freed `String` reused under a writer | serialize each send | `lib/Concurrency/TextCollector.extension.st` |

`LibraryLocks` holds the six locks, one `Mutex` each, in `lib/Concurrency`,
because a class in `sources/` cannot be given a class variable from `lib/`.
Its `holding...:` methods run their block unlocked while the lock does not yet
exist — that is the bootstrap, single-threaded by construction — and never
after.

Two things the audit left alone, on purpose. The GUI's globals — the
`ParagraphEditor` clipboard, the lazily-built menus, `Cursor`, `Project` — are
shared by design under one active controller, and a race there produces a
second identical menu. And a `WriteStream` shared by every worker loses
characters (14,217 of 16,000 at eight), which is the contract above: base
collections and streams are yours to guard.

## Six faults in the scheduler, found by the same audit

The Delay and yield kernels did not fail in the library. They failed in
`src/sched/st_sched.c`, and each fault looked like something else until an
invariant was written down and checked. The invariant is: **a process runs on
one worker at a time, and a worker holds what it runs.** Every symptom below
— a frame overflowed, a `Delay` "already waiting", a mutual-exclusion
`Semaphore` signalled more often than taken, a `SortedCollection` whose compare
answered nil, a `Process` that came back as a `MethodContext` — was that
invariant broken, seen late. The scheduler now refuses to nominate a process
that is still on a list, and says so, rather than letting the symptom arrive.

1. **An idle worker never looked at its own nomination.** The timer's signal
   is drained by whichever worker drains it, and resuming a waiter that
   outranks that worker's parked process *nominates* it rather than queueing
   it. The idle loop only looked at the ready lists, went on idling with the
   process in its hand, and the other workers — every worker idle, no timer
   armed because it had just fired — declared every process blocked and
   stopped. Two workers waiting on one-millisecond delays hung every run; one
   never did, because the single-worker path checked exactly this.
2. **A process was linked onto its semaphore before it was parked.** One list
   operation wide, and twenty-four workers found it in seconds: a signaller
   on another worker took the process off the list and ran it from the
   context it had been parked with *last* time. Parked first now, under the
   semaphore's lock.
3. **A preempted process went onto the ready list while it was still
   running.** Chapter 29 sleeps the active process and switches later, which
   is fine when only one thread can run it. Parked first now — and a process
   this worker has already parked and handed on is not requeued at all, which
   is how a terminated `yield` helper was being run a second time.
4. **The timer's signal did not take the semaphore's lock.** `wait` reads the
   count and links under it; a signal between the two saw an empty list,
   spent itself as an excess, and left the timing process linked for ever
   with the count at one. One worker never showed it: the drain and the wait
   were the same thread.
5. **A running process was kept alive by nothing but the last worker's
   switch.** The switch released the nomination's count once the process was
   active, trusting the image's `activeProcess` variable to hold one — one
   slot, for N workers. The next switch on any worker stored over it, and a
   process executing on some core was freed. The Delay timing process came
   back as a `MethodContext`, with its semaphore pointing at it. A worker now
   keeps the count for as long as the process is active, which is also what
   the collector counts when it visits each worker's active process.
6. **The safepoint wrote a parked process's registers over a running one's.**
   Every worker stores its registers into its active context before a
   collection, idle workers included — and an idle worker is one whose last
   process was parked and taken. `ST_store_active_context` is now a no-op for
   a disowned process.

And one hazard that is not a fault but a property: `Set>>grow` makes a bigger
copy and `become:`s it, so an unlocked reader that has already read the old
`basicSize` probes the new table with the old arithmetic. That is why
`Smalltalk`'s *readers* are locked as well as its writers, and why a
collection shared between workers wants a lock around its reads too, not just
its writes.

## Stopping a process from outside it

The third audit (Bugs3 B3, B9, B13, B16, B17) found the one question Chapter
29 never had to answer: *is that process running somewhere?* With one thread
a process that is not the active process is on a list or suspended, and its
`suspendedContext` is where it stands. On a pool, a process on another core
is on no list and its real stack is in that core's registers. Every 1983
method that touches another process assumed the first world: `terminate`
unlinked it from nothing and cleared a field nobody read, so the loop kept
counting; `suspend` said it was not active; `signalException:` gave up on the
nil it found; and `resume` -- of the active process, or of a process another
worker was running -- queued it, so an idle worker ran it from a stale
context and two threads were inside one context.

Two tables in `st_sched.c` answer the question and act on the answer, and
the shape of each is the argument for it:

- **What each worker has in its hands.** Every worker publishes, in a static
  row that outlives its thread, the process it is executing (`held`), the
  one it will switch to (`nominee`), and one it has taken off a list and not
  yet put anywhere (`taken`). Three atomic words, written only by the owner,
  read by anyone. The discipline that makes them exact is that a process is
  written into the slot it is moving *to* before it is cleared from the one
  it is moving *from*, and cleared from `held` only once it has *landed* --
  the list holds it, or it is free on purpose. A reader that sees a process
  in no row and on no list has seen it free, *provided* nothing can make it
  un-free behind the reader's back.

- **Processes that must not run.** That proviso is the second table: a
  worker that wants a process stopped names it there, and every place a
  process can start or continue running honours the name -- a worker
  executing a named process parks it onto its ready list at its next
  bytecode and goes to find something else, a named nominee is released
  to its ready list instead of switched to, no signal and no idle worker
  takes a named process off a list, and `resume` refuses it. A named process
  can only move towards being free. So `primDetach:` names the process, then
  loops: take it off its list under that list's lock if it is on one, or
  wait a moment if some row names it, or -- neither -- conclude. The
  conclusion is sound because the exits are closed, and it is what
  `terminate`, `suspend` and `signalException:` in `lib/Concurrency` go on
  from.

`terminate` then runs the process's `ensure:` blocks, innermost first, *on
the terminating process*, the way `Exception>>unwindTo:` and Squeak's
terminate before `runUntilErrorOrReturnFrom:` ran them -- the only model
this system's machinery supports, and enough for what an `ensure:` is for.
A terminated process has a nil `suspendedContext`, primitive 87 refuses to
resume one, and the switch drops one that a racing signal delivers anyway
rather than stopping the image on it.

The same rule applied to every process at once is a **freeze**, which is
what a snapshot on a pool takes: every other worker parks what it runs onto
its ready list and idles, the image is written with every process on a list
a reloaded worker can take it from and the snapshotting process published as
the scheduler's `activeProcess`, and the workers are thawed. Not a
safepoint, though it looks like one, because the image writer takes a
safepoint of its own for the collection it runs first, and a safepoint
inside a safepoint is a requester waiting for itself. Signals keep flowing
during a freeze -- a waiter taken off a semaphore lands on its ready list --
because a signal spent as an excess while its waiter sits on the list is a
semaphore that never wakes.

One residual, inherent and recorded rather than solved: a process terminated
in the instant between a semaphore signal being handed to it and its running
again consumes that signal. Squeak's `<criticalSection>` pragma exists for
exactly this; `Mutex>>critical:` here is exposed to it as every mutex built
on a semaphore is.


## The VM's own connections to the image

Two links from the VM to the image live in C rather than in any instance
variable: the semaphore `primInputSemaphore:` (primitive 93) installs for
input, and the Form `beDisplay` (primitive 102) makes the screen. A snapshot
stores objects, so both were dropped by a save and reload, and a reloaded image
came up unable to be told about a key or a mouse button. The events queued and
the semaphore they signalled was nobody's.

Smalltalk-80 reconnects by sending `Smalltalk install` on resume -- that is
what `SystemDictionary>>install` is for, and its comment says so: *"Get
connected back up to the hardware after a snapshot or quit."* But that is the
image putting the VM back together, and it cannot run before the VM can run it.
The snapshot now carries both connections as VM state, and `ST_interp_init`
restores them when nothing is connected yet.

The same input semaphore taught a second lesson about ordering. `InputSensor
class>>install` forks the process that drains the event queue, and that process
takes its priority from `Processor activePriority` -- so it needs not just a
Processor but one with an active process. Run with the other initialisers, it
stopped at that line, and the last line, the one that installs the semaphore,
never ran. Nothing announced it. Input simply never arrived, which is a hard
thing to go looking for. It now runs inside `BOOT_install_scheduler`, after the
startup process exists and before that process is given its real priority --
the startup process is briefly at the highest priority so that resuming the
input process can only queue it, never transfer to it.

## Status

The contract is settled; the implementation lands in Phase 7. Phases 0 through 6
build a correct single-threaded system whose object memory, safepoint polling
and allocator are already shaped for it.

## Phase H4 was measured, and deferred

`doc/PLAN-PHARO.md` asks that work-stealing deques be "justified by
measurement, not adopted on reputation". They were measured, and they are not
justified yet.

Profiled on the most lock-heavy workload this system has —
`test_parallel_lib` at eight workers, 62,000 `Mutex` acquisitions and 62,000
items through one `SharedQueue`, sampled after the bootstrap:

| | |
|---|---|
| `SCHED_wake_highest_priority` — the ready-list walk under `ready_lock` | 1.42% |
| `pthread_mutex_lock` + unlock | 0.93% |
| futex | 0.28% |
| `SCHED_check_process_switch` + `SCHED_suspend_active` | 0.81% |

Under 4% between them, on the workload built to contend. Splitting the ready
lists per worker and stealing from a victim's tail would be a real amount of
delicate code to buy a fraction of that.

**Where the time actually goes**, in the same profile:

| | |
|---|---|
| `OM_method_dict_key` | 18–33% |
| `OM_is_object` | 17–22% |
| `lookup_method` | 7–13% |
| reference counting | ~15% |

**Method lookup is 40–60% of the run, and there is no method cache** —
primitive 89, `flushCache:`, is a no-op because there is nothing to flush.
That is the next scaling work, and it is worth an order of magnitude more
than per-worker ready queues.

The design is already written down in `doc/PLAN-PHARO.md`'s audit table:
per-worker cache, a global epoch bumped on publish, and method dictionaries
that become immutable once published so a stale entry is impossible rather
than unlikely. Designing it now costs nothing; retrofitting it later costs a
week of confusing bugs.

## The method cache was re-justified, and declined

It was filed as the next scaling work when `OM_method_dict_key` was 18–33% of
every profile. That was because `lookup_method` scanned every slot of every
method dictionary in the chain instead of probing from the selector's hash.
Once it probed, the justification had to be re-taken from scratch — a task
filed on a measurement that has stopped being true is worse than no task.

**What a perfect cache would save**, measured as the *inclusive* cost of
`lookup_method` — the honest ceiling, since a cache that never missed would
remove exactly that subtree:

| workload | `lookup_method` inclusive |
|---|---|
| steady-state kernel (`intervals`, sends and blocks) | **0.10%** |
| bootstrap + both Pharo packages, 6,202 methods compiled and initialised | **8.40%** |

The second is the friendly case: an image build is the most polymorphic,
most send-heavy thing this system does, and it is what every `-eval`, every
test run and every ratchet turn pays. Even there a *perfect* cache — no
misses, no invalidation cost, free lookups — buys 8.4%.

For that we would take on a per-worker cache, a global epoch bumped on
publish, and method dictionaries made immutable-once-published. That is
delicate, concurrency-sensitive machinery whose failure mode is a stale
method silently answering an old body.

**Declined.** For comparison, in the same steady-state profile:

| | |
|---|---|
| `OM_decrease_ref_object` + `OM_increase_ref_object` | **25.6%** |
| `OM_store_pointer` | 9.3% |
| allocation (`instantiate` + `OM_instantiate_pointers`) | ~10% |
| `lookup_method` | 0.1% |

Reference counting is three times the *ceiling* of a method cache and has had
no work since the immortal-object fix. That is where the next measurement
should go — and it should be a measurement, because `doc/SCALING.md` records
that refcounting was once 99.81% of cross-core stalls and was fixed by not
counting at all rather than by counting faster.

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

2. **`become:` is atomic.** Identity mutation is a single atomic swap of two
   object-table entries. No thread ever observes a half-completed `become:`.
   This is the payoff for keeping an object table.

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

4. **`Transcript` interleaving.** Output from concurrent processes may
   interleave unless you hold the Transcript's monitor across an entry.

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
```

Thread 0 is not a worker by design. If it were, it could be parked in a GC
safepoint at the moment the window server needs a response, deadlocking the
compositor. SDL3's own rules also force this: `SDL_PumpEvents`, `SDL_WaitEvent`,
`SDL_CreateRenderer` and `SDL_LockTexture` are main-thread-only, and on macOS
"main thread" means the thread that entered `main()` — that is not negotiable.

Smalltalk `Process` objects remain green and cheap, multiplexed M:N over the
worker pool with per-worker ready queues and work stealing. `Processor
activeProcess` is per-worker state.

## Status

The contract is settled; the implementation lands in Phase 7. Phases 0 through 6
build a correct single-threaded system whose object memory, safepoint polling
and allocator are already shaped for it.

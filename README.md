# Smalltalk-2026

A Smalltalk-80 system written in C that runs Smalltalk bytecodes on multiple
native CPU threads simultaneously.

Every production Smalltalk — Squeak, Pharo, Cuis, VisualWorks, GNU Smalltalk —
uses green threads on a single OS thread. `Process`, `ProcessorScheduler` and
`Semaphore` are real, but they buy concurrency, never parallelism. This project
keeps the Blue Book language, class library and MVC interface, and replaces the
object memory and the scheduler with ones that use every core.

- **Graphics:** SDL3
- **Platforms:** Linux, macOS, Windows
- **Language:** C11
- **License:** BSD 2-Clause

## Status

Phases 0-2, 4, 6 and 7 complete; 3 and 5 partial. See `doc/PLAN.md` for the roadmap.

| Phase | State |
|---|---|
| 0 — Skeleton, portability layer | done |
| 1 — Blue Book object memory | done |
| 2 — Interpreter, primitives, `trace2` gate | done |
| 3 — BitBlt, SDL3, first light | in progress — see below |
| 4 — 64-bit object memory | done |
| 5 — Compiler and image bootstrap | compiler done; bootstrap not started |
| 6 — macOS and Windows | done, unconfirmed on those hosts |
| 7 — Native threads | done |
| 8 — MVC under parallelism | not started |

## Building

```sh
make            # build ./st80
make test       # build and run the unit tests
make help       # targets and variables
```

Variables:

| Variable | Meaning |
|---|---|
| `OM=bb` | 16-bit Blue Book object memory (default) — the validation harness |
| `OM=mt` | 64-bit threaded object memory — the real system |
| `TSAN=1` | thread sanitizer build (needs `libtsan`) |
| `ASAN=1` | address + undefined-behaviour sanitizer build |

Sanitizer builds go to their own directory (`build/bb-tsan`, `build/bb-asan`)
so an instrumented binary can never be linked against stale uninstrumented
objects.

### Phase 3 status

Implemented and building: BitBlt (Blue Book Chapter 18, all sixteen
combination rules, clipping, skew, halftone, overlap reversal), the SDL3 front
end (streaming texture, damage tracking, event translation), the green
process scheduler (Blue Book control primitives 85-88), and the marking
garbage collector (Chapter 30).

The image boots, runs, and tells us its display is 640x480 by way of
`beDisplay`. It does not yet reach a usable System Browser: it exhausts the
Blue Book object table -- 32,767 entries, a hard limit of this object memory --
after about 2.5 million bytecodes. Four collections reclaim progressively
less (1030, 69, 6, 0), so the objects still standing are genuinely reachable
and something is retaining them. That is the open question for Phase 3.

## Validation

The 1983 Xerox tape carried reference dumps and execution traces alongside the
image. We check against all of them, which is far sharper than any test we
could write ourselves:

| Oracle | Result |
|---|---|
| `ref-count-distribution` | 18,391 objects, all 124 histogram buckets exact |
| `class.oops` | 446 / 446 classes exact — pointer, octal, hex and name |
| `method.oops` | 4,494 / 4,494 methods exact — pointer, class and selector |
| **`trace2`** | **611 / 611 lines byte for byte** — every bytecode, send, return and primitive of the image's startup |
| `trace3` | 482-line prefix exact; see `tests/unit/test_trace.c` for the one documented divergence |

And for the parallel half, where no 1983 oracle exists, the thread
sanitizer is the judge:

| Check | Result |
|---|---|
| 31 native threads mutating one shared object memory | 398 checks, **0 races under TSAN** |
| Collections running while those threads mutate | every slot intact afterwards |
| Reference counts under contention | balanced traffic returns to exactly one holder |

## Design in one page

**Two object memories, one interpreter.** `src/om/om.h` defines the
object-memory operations as *macros*, and exactly one implementation is
compiled in, so the hot path inlines fully in either build.

- `om_bb.c` — Blue Book Chapter 30 verbatim: 16-bit object pointers, object
  table, reference counting. 32,768 objects, 2 MB heap. Never shipped. It
  exists so the interpreter can load the original 1983 Xerox image and
  reproduce Xerox's own `trace2`/`trace3` execution traces byte for byte.
- `om_mt.c` — 64-bit object table, tagged SmallIntegers, per-thread allocation
  buffers, parallel collection.

The interpreter is written once. Passing `trace2` therefore validates the same
code the parallel system runs. That is the central trick of the project.

**Why keep an object table**, when Spur and every modern VM dropped it? Because
under threading the trade inverts: `become:` becomes one atomic swap instead of
a stop-the-world heap scan, pinning for SDL and FFI is free, and compaction
touches one entry per object rather than every reference. We pay one
indirection to get atomic identity mutation.

**The semantic break.** Smalltalk-80 guarantees a process is never preempted by
another of the same priority, and the 1983 class library uses that as its
implicit lock. We break it deliberately and supply `Mutex`, `Monitor`,
`SharedQueue` and `Promise` in its place. This is normative and documented in
`doc/CONCURRENCY.md` — read it before writing Smalltalk for this system.

**Thread map.** Thread 0 is a dedicated SDL pump and never executes Smalltalk;
threads 1..N are Smalltalk workers and never call SDL video. SDL3's
main-thread-only rules and macOS's Cocoa run loop force this shape.

## Layout

```
src/port/       threads, mutexes, condition variables, TLS, time, atomics
src/om/         object memory (bb and mt), image readers, GC
src/interp/     bytecode interpreter, contexts, method lookup, primitives
src/gfx/        BitBlt, display, SDL3 main thread
src/sched/      Process, Semaphore, the M:N scheduler
src/compiler/   Smalltalk compiler in C, chunk-format reader
src/boot/       image bootstrap
sources/        the 1983 class library (MIT), vendored
oracle/         PRIVATE, gitignored -- see doc/LICENSING.md
```

## Provenance

The shipping image is bootstrapped from the MIT-licensed
`markbush/Smalltalk-80-Sources`. The Xerox image in `oracle/` carries no
license grant from any host and is used only as a private development oracle;
it is never redistributed and never copied from. See `doc/LICENSING.md`.

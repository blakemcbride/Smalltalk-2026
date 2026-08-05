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

Phases 0-7 complete; Phase 8 has its foundation. See `doc/PLAN.md` for the roadmap.

| Phase | State |
|---|---|
| 0 — Skeleton, portability layer | done |
| 1 — Blue Book object memory | done |
| 2 — Interpreter, primitives, `trace2` gate | done |
| 3 — BitBlt, SDL3, first light | done |
| 4 — 64-bit object memory | done |
| 5 — Compiler and image bootstrap | done |
| 6 — macOS and Windows | done, unconfirmed on those hosts |
| 7 — Native threads | done |
| 8 — MVC under parallelism | the 1983 library loads and runs; MVC not started |

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

### First light

The 1983 Xerox image boots to its own desktop:

```sh
$ ./st80 -screenshot screen.pbm -run oracle/VirtualImage 30000000
st80: display is 640x480
st80: wrote screen.pbm, 59898 of 307200 pixels are ink
```

What it draws is the System Workspace ("The Smalltalk-80 System Version 2,
Copyright (c) 1983 Xerox Corp."), the System Transcript showing the snapshot
timestamp of 31 May 1983, and a System Browser listing its class categories --
rendered by our BitBlt, through our text scanner, at 640x480.

It had appeared to leak contexts, exhausting the object table after ~3M
bytecodes. It was not a leak. Five primitives were missing, `perform:` and
the Floats among them; the image failed, tried to report which method had
failed, and that reporting recursed. The sender chain was 10,275 frames deep
at the point of death. With those primitives implemented the image runs
50 million bytecodes with three quarters of the heap still free.

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
| **31 threads interpreting Smalltalk on 32 CPUs** | **11,160 expressions, every answer correct, 0 races under TSAN** |
| Collections running while those threads interpret | answers still correct |
| 31 native threads mutating one shared object memory | 398 checks, 0 races under TSAN |
| Reference counts under contention | balanced traffic returns to exactly one holder |

## The bootstrapped image

Every Smalltalk-80 image in existence descends by mutation from the one
Xerox shipped in 1983. This one does not:

```sh
$ ./st80 -bootstrap kernel/Kernel.st -o st80.image -eval '3 + 4'
st80: 36 classes, 69 methods, 102 symbols
7
```

`kernel/Kernel.st` is our own source, so the image carries no Xerox
provenance. The bootstrap runs in three passes because the metaclass graph
has no valid build order — a class's class is its metaclass, whose class is
Metaclass, whose class is its own metaclass, whose class is Metaclass again.
Allocating every class object with a nil class field, then patching, closes
every loop at once.

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

**The whole 1983 class library loads and runs.** All 4517 methods of the 226
MIT classes in `sources/` compile, bootstrap into a live image in 92ms, and
compute:

```sh
$ ./st80 -bootstrap -manifest sources/MANIFEST -eval '3 factorial'
st80: 226 classes, 4517 methods, 3488 symbols
6
```

`3 factorial`, `100 gcd: 75`, `(1 to: 5) collect: [:i | i * i]`,
`(3/4) + (1/4)`, `42 printString`, `(0@0 corner: 10@20) area` and
`(Dictionary new at: 1 put: 2; yourself) printString` are all answered by
Xerox's own methods, not by anything in this project.

The bootstrap also runs the step a fileIn does not and an image build does:
it sends `initialize` to the 45 classes that define one, which is what sets
the class variables the library reads. 40 of the 45 complete.

`tests/unit/test_library.c` gates on the library compiling;
`tests/unit/test_image.c` gates on it running, and prints on every run the
frontier that does not work yet — chiefly `Symbol` interning, where the table
is seeded correctly but `hasInterned:ifTrue:` does not return from a scan.

**The language stops at the Blue Book.** The grammar this compiler accepts is
the 1983 one, deliberately: dynamic arrays, general pragmas, block-local
temporaries, byte-array and scaled-decimal literals and named primitives are
all later Squeak/Pharo additions and none of them are implemented.
`doc/LanguageExtensions.md` surveys them and records where each stands; it is
a discussion to have once the nine phases are done, not before.

## Layout

```
src/port/       threads, mutexes, condition variables, TLS, time, atomics
src/om/         object memory (bb and mt), image readers, GC
src/interp/     bytecode interpreter, contexts, method lookup, primitives
src/gfx/        BitBlt, display, SDL3 main thread
src/sched/      Process, Semaphore, the M:N scheduler
src/compiler/   Smalltalk compiler in C, chunk-format reader
src/boot/       image bootstrap
sources/        the 1983 class library (MIT), vendored -- 226 classes
oracle/         PRIVATE, gitignored -- see doc/LICENSING.md
```

## Provenance

The shipping image is bootstrapped from the MIT-licensed
`markbush/Smalltalk-80-Sources`. The Xerox image in `oracle/` carries no
license grant from any host and is used only as a private development oracle;
it is never redistributed and never copied from. See `doc/LICENSING.md`.

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
| 8 — MVC under parallelism | a System Browser renders on the bootstrapped image; interaction next |

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
st80: wrote screen.pbm, 59792 of 307200 pixels are ink
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
the class variables the library reads. All 45 complete.

Symbols are unique, which is what makes `==` mean anything for them:
`#printString == 'printString' asSymbol` is true, whether the symbol was made
by the compiler while building the library or by the image afterwards.

Printing works throughout, which is the deepest path the library has:
`printOn:` runs Stream, WriteStream, String, Symbol, Character and — for a
Float — LargeInteger division, all at once. So does mixed-mode arithmetic,
in both directions: `3 + 1.5` and `3.5 >= 0` both coerce to the higher
generality and retry.

**The bootstrapped image draws.** It is given a 640x480 `DisplayScreen` — a
1983 image inherits one from the image it was built from, so a first has to
be made — and then draws on it with Xerox's own `Form` and `BitBlt` through
primitive 96:

```sh
$ ./st80 -bootstrap -manifest sources/MANIFEST -screenshot screen.pbm \
      -eval 'Display fill: (40@40 corner: 200@120) rule: 3 mask: Form black.
             Display fill: (60@60 corner: 180@100) rule: 3 mask: Form white.
             Display fill: (240@40 corner: 400@120) rule: 3 mask: Form gray.
             ^Display width'
st80: wrote screen.pbm, 14400 of 307200 pixels are ink
```

A black frame with white knocked out of it, and a correctly dithered gray —
the counts are exact, because a fill covers precisely the rectangle asked for
and gray covers half of it.

**And browsing works.** Category to class to protocol to selector to source:
`Kernel-Objects` lists its five classes, `Boolean` its four protocols, and
`controlling` its six selectors — and `Boolean sourceCodeAt: #not` answers
the method's real text. Smalltalk-80 keeps no source in the image; a
`CompiledMethod` carries a position into a sources file and the Browser reads
the chunk there. The bootstrap writes every method's source into one String
and hands it over as `SourceFiles`, because nothing says that stream has to
be a file — `RemoteString` asks it only to `position:` and `nextChunk`.

**And it browses itself.** A System Browser — the Browser model on
`SystemOrganization`, five list views and a code view, all the library's own
— renders on the bootstrapped image, listing the categories the image was
built from. It is built with its window set rather than swept out, because
`open` calls `view resize`, which asks the user to drag a rectangle and there
is nobody headless to drag one.

**And the image boots.** Saved and resumed with `-run`, it wakes into a
process of its own and runs the MVC control loop:

```sh
$ ./st80 -bootstrap -manifest sources/MANIFEST -o mvc.image -startup '...'
$ ./st80 -screenshot screen.pbm -run mvc.image 3000000
st80: wrote screen.pbm, 121199 of 307200 pixels are ink
```

What it draws is its own desktop — the gray background and the scheduled
windows over it — and then it waits for input. Processes fork and the
scheduler switches between them; the Sensor answers; a controller under the
cursor says it wants control, and `searchForActiveController` gives it. `ProcessorScheduler class>>new` refuses on purpose, because
in 1983 the one scheduler was made when the image was built and carried by
every snapshot after; an image built from sources has to be given one, and a
process to wake up in, whose method is compiled from `-startup`.

**And it schedules windows.** `ScheduledControllers` is a real
`ControlManager` holding a screen controller; scheduling two views and asking
it to restore redraws its gray background and both windows over it. `Sensor`
is a real `InputSensor`. Both are made when an image is built and carried by
every snapshot after, so an image built from sources has to be given them.

**And it puts a window on the screen.** A `StandardSystemView` with a label
displays itself — the tab, the border and the body — through the library's
own `View` code, the windowing transformation and the text it composes. That
is the substrate a Browser or an Inspector lives in.

**And it composes and draws text.** `'hello world' asParagraph` composes to
one line 88 pixels wide and displays on the screen, which runs Paragraph, the
CompositionScanner, the font and BitBlt together — the whole path a view
uses.

**And it draws text.** The 1983 sources are code and carry no font data —
fonts lived in the image, which is the one thing here that has no licence —
so the system ships an 8x8 face of its own in `src/gfx/font8x8.c`, built into
a `StrikeFont` and a `TextStyle` the library made itself. Pool dictionaries
work, so `TextConstants` holds the bindings every text class compiled
against, and `Smalltalk` is a real `SystemDictionary` rather than a
placeholder.

`tests/unit/test_library.c` gates on the library compiling and
`tests/unit/test_image.c` on it running.

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

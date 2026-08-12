# Smalltalk-2026: A Parallel Smalltalk-80 in C

## Context

Smalltalk-80 is a complete, coherent programming system whose one disqualifying
limitation in 2026 is that it cannot use more than one CPU. Every production
Smalltalk — Squeak, Pharo, Cuis, VisualWorks, GNU Smalltalk — uses green threads
multiplexed onto a single OS thread. `Process`, `ProcessorScheduler` and
`Semaphore` are real, but they buy concurrency, never parallelism.

This project builds a new Smalltalk-80 from scratch in C that runs Smalltalk
bytecodes on multiple native OS threads simultaneously, with SDL3 for graphics,
portable to Linux, macOS and Windows, preserving the Blue Book language, class
library and MVC interface.

### The one place the requirements collide

Requirement 1 (native threads) and requirement 5 (Standard Smalltalk-80) conflict
at exactly two points, and both are resolved deliberately below:

1. **The object memory.** Blue Book Chapter 30 specifies 16-bit object pointers:
   32,768 objects maximum, a 2 MB heap. That is not a system you can do parallel
   work in.

2. **The scheduler contract.** The Blue Book guarantees that a process is never
   preempted by another process of the same priority. The entire 1983 class
   library uses this as its implicit mutual-exclusion primitive — every
   unsynchronized read-modify-write in `Collection`, `Symbol` interning, `Delay`,
   and `Transcript` is correct *only* because of it. True parallelism voids this
   guarantee.

The resolution: keep the Blue Book **language, bytecode set, class library and
MVC interface**; replace the **object memory** and **explicitly, publicly break
the same-priority atomicity rule**, supplying real `Mutex`/`Monitor` in its place.

### Precedent

Eliot Miranda co-authored a 2024 survey ("Multi-threaded OpenSmalltalk VM:
Choosing a Strategy for Parallelization", PX/24) that recommended *against* this
design for Squeak. Its four stated reasons were: small VM team, live reflection
complicating object-space synchronization, Morphic being single-threaded, and
back-compatibility with a 30-year-old image. **Building from scratch eliminates
the last two outright** and lets us design the class library for parallelism
rather than retrofit it.

The existence proof is Pallas & Ungar, *Multiprocessor Smalltalk* (PLDI '88):
Berkeley Smalltalk-80 on a DEC Firefly, shared mutable heap, speedups > 2.0 on 5
processors, median 48% efficiency — with 1988 tools. Their methodology
(*serialize, replicate, reorganize*) is the class-library audit framework for
Phase 7.

---

## Decisions locked

| Decision | Choice |
|---|---|
| Object memory | 64-bit object table for the real system; a faithful 16-bit Blue Book OM built alongside it purely as a validation harness |
| Concurrency | Shared heap; Smalltalk `Process` green, M:N over a pool of native worker threads |
| Image origin | Bootstrapped in C from `markbush/Smalltalk-80-Sources` (MIT). The Xerox image is a private development oracle only |
| UI | Full Blue Book MVC, phased: graphics and Transcript/Workspace early, browsers/debugger hardened after threading |
| Build | Hand-written GNU Makefile (+ `Makefile.msvc`), matching Dynace/VMEM/ViewFS. Not CMake, not BLD (BLD is Java-only) |
| Threading API | Own shim over pthreads / Win32. **Not** C11 `<threads.h>` — Apple does not ship it |
| Atomics | C11 `<stdatomic.h>` behind `st_atomic.h`, restricted to pointer- and int-sized naturally-aligned types |

---

## Local assets to use

These are already on disk and materially change the schedule.

| Asset | Path | Use |
|---|---|---|
| Wolczko Manchester distribution | `/home/blake/Backup/BlueBookSmalltalk/` | Unextracted. `image.tar.gz` → Xerox `VirtualImage`, `Smalltalk-80.sources`, and **`trace2`/`trace3`** |
| `trace2` / `trace3` | inside `image.tar.gz` | **The single most valuable asset.** Xerox reference execution traces. Phase 2's gate is reproducing them byte-for-byte |
| Snapshot format spec | `manual.pdf.gz` (15 MB) | The *Smalltalk-80 Virtual Image Version 2* booklet — the only specification of the image file format; the Blue Book omits it |
| Blue Book VM sources | `VMsrc.shar.Z` | Wolczko's re-keyed machine-readable Chapter 27–30 Smalltalk sources. Far better than the OCR'd PDF, which corrupts `_` and `^` |
| Your SDL3 port | `/home/blake/GitHub.blakemcbride/Acme/src/cmd/acme-sdl3/drawbridge.c` | Working in-process SDL3 bridge. Its `Makefile` already handles `pkg-config sdl3` across Linux/macOS/MSYS2 — **copy that structure directly** |
| Raster engine reference | `/home/blake/GitHub.blakemcbride/Acme/src/libmemdraw/` | `draw.c`, `line.c`, `fillpoly.c` — a working software compositor to read while writing BitBlt |
| Dynace GC | `/home/blake/GitHub.blakemcbride/Dynace/kernel/kernel.c` | `Dynace_cm_gMarkObject` (~line 1685) is a non-recursive pointer-reversal marker — directly reusable |
| Makefile template | `/home/blake/GitHub.blakemcbride/ViewFS/Makefile` | Your best modern C makefile: `-std=c11 -Wall -Wextra -Wpedantic`, pkg-config, per-subdir build dirs |
| Test harness style | `/home/blake/GitHub.blakemcbride/ViewFS/tests/unit/*.c` | Plain assert-and-count, no framework. Match this |

Extract the Wolczko tarballs into `oracle/`, and **`.gitignore` that directory** —
see Licensing below.

---

## Architecture

### Repository layout

```
Smalltalk-2026/
  Makefile              GNU make, Linux/macOS
  Makefile.msvc         Windows/MSVC
  src/
    port/               st_port.h/.c    threads, mutex, cond, TLS
                        st_atomic.h     atomics behind our own names
                        st_time.c, st_file.c
    om/                 om.h            THE object-memory interface (macros)
                        om_bb.c         faithful 16-bit Blue Book (validator)
                        om_mt.c         64-bit threaded (the real one)
                        image_bb.c      Xerox snapshot reader (big-endian)
                        image_mt.c      native snapshot reader/writer
                        gc.c            TLABs, safepoints, scavenge, mark-sweep
    interp/             interp.c        bytecode loop
                        contexts.c, lookup.c, cache.c
                        prim_*.c        Chapter 29 primitives, grouped
    gfx/                bitblt.c        Chapter 18
                        display.c       Form <-> SDL3 texture
                        sdl_main.c      thread 0: window, event pump
    sched/              process.c, semaphore.c, scheduler.c
    compiler/           lexer.c, parser.c, emit.c, chunk.c
    boot/               bootstrap.c     sources tree -> image
  sources/              markbush/Smalltalk-80-Sources (MIT), vendored
  oracle/               GITIGNORED. Xerox image, trace2/trace3
  tests/
  doc/                  CONCURRENCY.md is mandatory reading
```

### Object memory: one interface, two implementations

`src/om/om.h` defines the object-memory operations the interpreter uses —
`OM_fetch_pointer`, `OM_store_pointer`, `OM_fetch_class`, `OM_instantiate`,
`OM_become`, and so on. **These are macros, not function pointers**, and exactly
one implementation is selected at compile time (`make OM=bb` / `make OM=mt`) so
the hot path is fully inlined in both builds.

This gives the project its central trick: **the interpreter is written once, and
the 16-bit build proves it correct.**

- **`om_bb.c`** — Chapter 30 verbatim. 16-bit OOPs, low bit tags SmallInteger
  (15-bit signed), object table of 32,767 two-word entries, 8-bit reference
  counts with `HugeSize` saturation, segment/location addressing, pointer-reversal
  marking. Single-threaded, never shipped, exists to load the Xerox image and
  reproduce `trace2`.

- **`om_mt.c`** — 64-bit object table. Retaining the table (rather than direct
  pointers, as Spur and Squeak do) is a deliberate choice justified by threading:

  | Operation | Object table | Direct pointers |
  |---|---|---|
  | `become:` | One atomic double-word swap, no safepoint | Full safepoint + heap-wide forwarder cleanup |
  | Pinning for SDL/FFI | Free — the table entry is the identity | Requires explicit pin lists |
  | Compaction | Move the object, update one entry | Update every referring slot |
  | Per-object metadata (lock word, shared flag) | Natural home | Must widen every object header |
  | Field access cost | One extra indirection | Direct |

  We pay one indirection per dereference and get atomic identity mutation. In a
  system where a developer recompiles a class *while eight threads execute its
  methods*, that trade is architectural, not micro-optimization. It is also the
  design you already know from VMEM's `VMbase` handle array.

### Concurrency model

```
Thread 0  ── dedicated SDL pump. Owns window, renderer, texture, event queue.
             NEVER executes Smalltalk bytecodes.
Threads 1..N ── Smalltalk workers. N ≈ core count. Run bytecodes, allocate
             from thread-local buffers, poll safepoints. Never call SDL video.
```

Thread 0 must not be a worker: if it were, it could be parked in a GC safepoint
when the window server needs it, deadlocking the compositor. SDL3's documented
rules force this shape — `SDL_PumpEvents`, `SDL_WaitEvent`, `SDL_CreateRenderer`,
`SDL_LockTexture` and `SDL_UpdateWindowSurface` are all main-thread-only, and on
macOS "main thread" means the thread that ran `main()`, non-negotiably. Workers
reach thread 0 via `SDL_PushEvent` (explicitly thread-safe) plus
`SDL_RunOnMainThread`, paired with `SDL_WaitEventTimeout` so queued callbacks
cannot stall.

Smalltalk `Process` objects stay green and cheap, multiplexed M:N over the worker
pool with per-worker ready queues and work stealing. `Processor activeProcess`
becomes per-worker state.

### The semantic break — `doc/CONCURRENCY.md`

This document is a deliverable, not documentation debt. It must state plainly:

- **Same-priority atomicity is gone.** Two processes at the same priority may run
  simultaneously on different cores. Priorities become scheduling hints.
- Base collections (`OrderedCollection`, `Dictionary`, `Set`) are **not**
  thread-safe — matching the post-`Vector` lesson from Java. Explicitly shared
  variants are provided.
- New base classes: `Mutex`, `Monitor`, `SharedQueue`, `Promise`, and
  `Processor>>#forkParallel:`.
- `Semaphore>>#signal`/`#wait` become genuinely atomic primitives over an
  atomically-managed queue.

### Garbage collection

Follow OCaml 5's design (*Retrofitting Parallelism onto OCaml*, ICFP 2020) — the
best-documented case of a small team shipping a production parallel GC:

1. **Per-thread allocation buffers.** Bump allocation, no atomics on the fast
   path. Allocation is Smalltalk's hottest operation; this is where the multicore
   win actually lands.
2. **Stop-the-world *parallel* young-generation scavenge.** All workers collect
   their own nursery simultaneously, promoting survivors. OCaml benchmarked this
   against a concurrent minor GC and chose it on both throughput and latency.
3. **Mostly-concurrent mark-sweep for the old generation** — deferred to after
   Phase 7 ships.

**Safepoints: a polled atomic flag in the interpreter dispatch loop**, checked at
message sends and backward jumps. Not signals, not `mprotect` guard pages. Reasons:
it is identical on Windows (which has no signal-based thread suspension), it costs
almost nothing relative to interpreter dispatch, and — decisively — it yields
**precise roots**, since we know exactly which VM stack slots hold OOPs. Precise
roots are what make a moving, compacting collector possible at all.

Explicitly rejected:
- **Boehm GC** — conservative and non-moving by design; kills compaction and cheap
  bump allocation. Acceptable only as a temporary crutch in Phase 4.
- **STM** — RSqueak/VM tried it (IWST'16) and *lost* performance; their Mandelbrot
  benchmark ran slower with threads than without.
- **Ravenbrook MPS** — genuinely strong (BSD-2, moving, exact, generational,
  multi-threaded), but a single arena lock serializes MPS entry, its page-protection
  barriers collide with debuggers and sanitizers, and Emacs's multi-year `igc`
  integration shows the learning curve. Worth a Phase 7 spike; not the default.

### Graphics

Everything in Smalltalk-80 graphics goes through BitBlt — text, lines, cursors,
menus, scrolling, window damage. Implement Chapter 18 natively: all 16 combination
rules, clipping, skew/rotate, halftone masking, and source/destination overlap
direction reversal. Primitive 96 is the entry point; 103 (`scanCharactersFrom:`)
and 104 (`drawLoopX:Y:`) are accelerations.

SDL3 surface area stays deliberately tiny — a single full-window
`SDL_TEXTUREACCESS_STREAMING` texture. Each frame thread 0 does `SDL_LockTexture`
on the dirty rect → row-by-row copy honoring the returned pitch (which will *not*
equal `width * 4`) → `SDL_UnlockTexture` → `SDL_RenderTexture` →
`SDL_RenderPresent`. `SDL_UpdateTexture` is documented as slow for
frequently-changing content. The SDL3 GPU API is over-engineered for a 2D
framebuffer and adds a synchronization model that would have to be reasoned about
alongside the GC — skip it. `SDL_GetWindowSurface` is the ~30-line fallback for
headless/CI and remote X11.

The authoritative pixels live in the Smalltalk `Form`; the texture is a pure sink
(`SDL_LockTexture` is write-only and may hand back garbage). `DisplayScreen`
already tracks damage rectangles — use them.

---

## Phases

Each phase has a hard exit criterion. Do not advance without it.

### Phase 0 — Skeleton and portability
Repo layout, `Makefile` modeled on `ViewFS/Makefile`, `src/port/st_port.h`
(thread/mutex/cond/TLS over pthreads and Win32), `st_atomic.h`, ViewFS-style test
harness. Extract the Wolczko tarballs into gitignored `oracle/`.

**Exit:** `make && make test` green on Linux; the shim compiles clean under
`-Wall -Wextra -Wpedantic`.

### Phase 1 — Object memory, Blue Book implementation
`om.h` interface. `om_bb.c` per Chapter 30. `image_bb.c` reads the Xerox snapshot:
big-endian, header gives object-space and object-table word lengths, object space
at offset 512, object table as the trailing `otLength * 2` bytes, floats needing
byte-swapping (a documented Xerox errata).

**Exit:** load `VirtualImage`; object-table census matches the bundled
`class.oops`, `method.oops` and `ref-count-distribution` reference dumps.

### Phase 2 — Interpreter and primitives — **the critical gate**
All 248 assigned bytecodes (Ch. 28). Contexts as real objects, so `thisContext`
and the debugger work naturally. All three primitive dispatch paths: numbered
primitives from the method header extension, the arithmetic/special-selector
bytecodes 176–207 that bypass lookup entirely, and the flag-5/flag-6 quick
returns. Chapter 29 primitives 1–116.

**Exit: `trace2` reproduced byte-for-byte, then `trace3`.** This is a genuine
oracle for the hardest-to-debug component in the project, and it is why the 16-bit
object memory is worth building. Both dbanay and avwohl gate on exactly this.

### Phase 3 — BitBlt, SDL3, and first light
`bitblt.c` (Ch. 18). `sdl_main.c` structured after `Acme/src/cmd/acme-sdl3/drawbridge.c`.
Thread 0 pump, streaming texture, keyboard/mouse into the VM's input queue and
Smalltalk's input semaphores.

**Exit:** the 1983 Xerox image boots to its own desktop, and its System Browser is
usable with mouse and keyboard. **This is the "it's alive" milestone** — a genuine
Smalltalk-80, single-threaded, running on your machine.

### Phase 4 — The 64-bit object memory
`om_mt.c`: 64-bit object table, tagged SmallIntegers, object header carrying class
index, identity hash, GC bits and a lock word. Per-thread allocation buffers and
single-threaded generational mark-sweep to start. Versioned little-endian native
snapshot format.

**Exit:** `make OM=mt` runs the same interpreter, passing the same unit tests,
against a hand-built minimal image.

**Status: complete.** Verified rather than asserted:

| | |
|---|---|
| `make OM=mt` | builds; 13 unit suites, 0 failures |
| a hand-built image | `-bootstrap -manifest sources/MANIFEST` writes one; 65,536 table entries, 15,599 live objects, 4,521 methods reachable |
| the same interpreter | `OM=bb` still reproduces Xerox's `trace2` (611 lines) and `trace3` (482) byte-for-byte |
| under sanitizers | ASAN 13 suites, TSAN 13 suites, no races |

Two things this section describes were **not** built, and both were superseded
deliberately rather than forgotten. They are recorded here because a reader who
finds the prose and not the code will otherwise assume an omission.

**No lock word in the object header.** The header carries `class_oop`, `size`,
`flags`, `refcount` and `hash`, and nothing else. Phase 7 needed per-object
mutual exclusion and got it from **64 stripe locks keyed on the identity hash**
instead — no object grows by a word, no header format changes, and the hash was
already stable across collection and snapshot. A lock word would have cost eight
bytes on every object in the image to serve the few that are ever contended.

**The collector is not generational.** It is mark-and-recount at a safepoint,
plus epoch-based reclamation so that a zero refcount can be freed without
stopping the world. That combination was reached by measurement, not by plan:
`doc/SCALING.md` records the collection pause falling from 128 ms to 16 ms, and
`intervals` from 0.88× to 3.18×, on those two changes. Generational collection
remains available as a later optimisation and should be justified by a
measurement, not by its reputation.

The exit criterion is what gates the phase; both divergences are improvements on
what the prose above imagined, made later with evidence the prose did not have.

### Phase 5 — Compiler in C, and bootstrap
`compiler/` — Blue Book grammar: scanner (note `_` is assignment and `^` is
return in 1983 sources), backtracking parser, bytecode emitter, literal frame and
method-header construction. `chunk.c` — the fileIn format: `!`-terminated chunks,
`!!` un-doubling, reader chunks (`!ClassName methodsFor: 'cat'!`) dismissed by an
empty chunk, CR line endings.

`boot/bootstrap.c` builds the object graph in memory from `sources/`, installs the
special-objects array and globals, compiles every method, and writes the snapshot.
This is the piece **no public project has done for Blue Book format** — Pharo's
`PharoBootstrap` and Cuis's Bootstrap are the architectural models but target Spur.
`markbush/Smalltalk-80-CompilerLib` (MIT, Swift) is a validated reference algorithm
worth reading.

**Exit:** `st80 -bootstrap sources/ -o st80.image` produces a working image, and
the self-hosting check passes — the image's own `Compiler` classes, running inside
the VM, emit the same bytecodes as the C compiler.

**Status: complete.** The compiler and bootstrap had been working for a long
time, but the criterion above was written with a bare directory and only
`-manifest sources/MANIFEST` worked: `-bootstrap sources/` handed the directory
to the reader as if it were a file and failed with *"short read on sources/"*.
A directory argument now expands to every source beneath it, recursively and
sorted, so the phase is met as written rather than as approximated.

| | |
|---|---|
| the command in the criterion | `st80 -bootstrap sources/ -o st80.image` writes an image |
| equivalence | `st80 -bootstrap sources/ kernel/Bootstrap.st` produces a **method dictionary identical** to the manifest route's |
| self-hosting | `test_self_hosting` compiles methods with the C compiler and with the image's own 1983 `Compiler`, and compares bytecodes |
| the whole suite | `OM=mt` 13 suites, `OM=bb` 8 suites, `trace2` 611 lines and `trace3` 482 byte-for-byte |

Two details worth keeping, because both look like bugs and are not.

**A directory is not the whole manifest.** `sources/MANIFEST` also names
`kernel/Bootstrap.st`, which lives outside the tree, so `-bootstrap sources/`
alone produces an image three methods short — `PositionableStream>>readOnly`,
`readWrite` and `readWriteShorten`. Bare arguments accumulate, so naming both
gives exactly the manifest's image; sorting at every level keeps the result
independent of `readdir` order, which would otherwise differ between machines.

**Self-hosting is checked where the two compilers can agree.** They diverge in
two places, and `test_self_hosting` says so rather than skipping quietly: the
1983 compiler has a one-byte form for short jumps and ours always emits the
two-byte form, so an inlined `ifTrue:ifFalse:` comes out two bytes longer with
the same instructions in the same order; and the two number the literal frame
differently, ours at emit time and 1983's during parsing, so pushes that name a
literal disagree on the index while naming the same literal. Same instructions,
different numbering — the check covers everything else, including sends to
`super`, cascades and backward jumps.

### Phase 6 — macOS and Windows
`Makefile.msvc` (or clang-cl, which gives full GCC/Clang atomics). Honor SDL3's
main-thread rules on macOS. Reuse the platform branches already working in
`Acme/Makefile`.

**Exit:** identical test suite green on all three platforms.

### Phase 7 — Native threads — **the point of the project**
- Safepoint polling at sends and backward jumps.
- Parallel STW scavenge across worker nurseries.
- M:N scheduler: worker pool, per-worker ready queues, work stealing.
- Atomic `become:` via object-table swap. Atomic `Semaphore`. Per-worker method
  caches with a coherent global invalidation path (primitive 89, `flushCache`).
- **Class-library audit using Pallas & Ungar's taxonomy** — for each shared
  structure decide: *serialize* (lock it), *replicate* (per-worker copy), or
  *reorganize* (remove the sharing). Priority targets: `Symbol` interning,
  `Transcript`, class `methodDict` mutation during live recompilation, `Delay`,
  weak-reference finalization.
- Ship `Mutex`, `Monitor`, `SharedQueue`, `Promise`, `forkParallel:` and
  `doc/CONCURRENCY.md`.

**Exit:** an embarrassingly-parallel Smalltalk benchmark scales measurably across
cores; a mixed-mutation stress test runs clean for hours under `-fsanitize=thread`.

### Phase 8 — MVC under parallelism
Bring the bootstrapped image's MVC up: `StandardSystemView`/`Controller`,
Transcript, Workspace, System Browser, Inspector, Debugger. Confine UI processes
to a designated worker, guard the display `Form`, wire damage tracking to the
texture upload.

**Exit:** browse, edit, compile, inspect and debug inside the bootstrapped image
while parallel worker processes run in the background.

### Phase 9 — Performance (optional, later)
Context-to-stack mapping, polymorphic inline caches, mostly-concurrent old-generation
GC, and only then a JIT.

---

## Verification

| Level | Method |
|---|---|
| Object memory | Census against `class.oops` / `method.oops` / `ref-count-distribution`; allocate/free/compact torture tests |
| Interpreter | **`trace2` and `trace3` byte-for-byte** — the primary correctness oracle |
| Primitives | Per-primitive unit tests, ViewFS style; failure-path coverage (every primitive must fall back to its Smalltalk body correctly) |
| BitBlt | Render all 16 combination rules across clip/skew/overlap cases; compare against the Blue Book's own `BitBltSimulation` running in the oracle image |
| Compiler | Bytecode-for-bytecode diff against the 1983 image's compiled methods; self-hosting round-trip |
| Threading | `-fsanitize=thread` on Linux; long-running mutation stress; parallel scaling benchmarks; `become:`-under-load tests |
| Portability | Full suite on Linux, macOS, Windows each phase from 6 onward |
| End to end | Boot the bootstrapped image, open a Browser, edit and compile a method, fork parallel processes, snapshot, reload |

---

## Risks

| Risk | Mitigation |
|---|---|
| **Bootstrap is the genuinely novel part** — no prior art for Blue Book format | Phase 3 gets a fully working system on the Xerox image first, so bootstrap failure does not block everything else. Read `markbush/Smalltalk-80-CompilerLib` |
| Class-library atomicity assumptions are undocumented and pervasive | Pallas & Ungar's audit taxonomy in Phase 7; thread sanitizer; expect this to be the longest phase |
| Object-table indirection costs single-thread performance | Accepted deliberately. Phase 9 mitigations exist. The project's value is multicore, not single-core speed |
| Live class recompilation racing with executing methods | The object table makes this tractable — `become:` is atomic — but it needs explicit design in Phase 7 |
| Scope. This is a multi-year single-developer project | Phase gates are ordered so that Phase 3 alone yields a real, usable Smalltalk-80. Every phase after that is independently valuable |

### Licensing

Your C code is yours. The **Xerox image and 1983 sources carry no license grant
anywhere** — not on Wolczko's site, not on the archive.org mirror, not in dbanay's
repo. It is de-facto abandonware with no enforcement in ~20 years, but there is no
affirmative permission. Hence: `oracle/` stays gitignored and private, and the
shipping image is bootstrapped from `markbush/Smalltalk-80-Sources`, which is MIT.
The Blue Book PDF is author-permitted free distribution (INRIA/Ducasse mirrors),
not public domain.

---

## Conventions

Matching your existing C: snake_case for functions and variables, `PREFIX_name`
for public APIs (`OM_fetch_pointer`, `VM_alloc` style), ALL_CAPS macros, BSD
2-clause header on library files, include guards, platform detection via feature
macros defined once at the top rather than `#ifdef` scattered through logic.

**Never a statement on the same line as `if`/`else`/`for`/`while`** — body always
on the next line, indented.

One convention to settle at kickoff: your core code (Dynace, VMEM) uses 8-wide
hard tabs; your newer code (ViewFS, `xcd-core.c`) uses 4 spaces. This plan assumes
**4 spaces** for a new project — say the word if you want tabs.

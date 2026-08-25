# Smalltalk-2026

A Smalltalk-80 written in C that runs Smalltalk bytecodes on **every core of the
machine at once**, and is checked byte-for-byte against Xerox's own 1983
execution traces.

**<https://github.com/blakemcbride/Smalltalk-2026>**

![The System Browser](doc/desktop.png)

Every production Smalltalk — Squeak, Pharo, Cuis, VisualWorks, GNU Smalltalk —
schedules its processes green, on one OS thread. `Process`,
`ProcessorScheduler` and `Semaphore` are real, and they buy concurrency but
never parallelism. This one replaces the object memory and the scheduler with
ones that use the whole machine, and keeps the language, the class library and
the interface.

| | |
|---|---|
| Language | C11 · no dependencies but SDL3 for the window, and ODBC — optional — for the database |
| Graphics | SDL3 |
| Platforms | Linux (developed on), Windows and macOS — each builds, bootstraps an image and runs its desktop. See [`Windows.md`](Windows.md) and [`macOS.md`](macOS.md) |
| Licence | BSD 2-Clause. See [Provenance](#provenance) — parts of the tree are other people's |

## It runs on every core

Measured on 8 physical cores, one worker per core, total work fixed and divided
between them. `make bench`:

| kernel | 1 worker | 8 workers | speedup | what it measures |
|---|---|---|---|---|
| arithmetic | 155.1 ms | 19.9 ms | **7.79×** | the interpreter's loop, nothing allocated |
| mandelbrot | 380.4 ms | 48.1 ms | **7.91×** | heavy arithmetic, almost no allocation — the ceiling |
| intervals | 124.9 ms | 27.3 ms | **4.58×** | pure interpretation: sends, blocks, a context per activation |
| collections | 191.7 ms | 29.7 ms | **6.45×** | heavy allocation and collection — the case a shared heap makes hardest |

Every worker computes something only it can check, so a fault arrives as a
wrong answer rather than as a hope that a sanitizer noticed. The whole suite is
clean under ThreadSanitizer at 31 threads, and that is a gate, not a report.

For why the numbers are what they are — including where the object table helps
and where it costs — see [`doc/SCALING.md`](doc/SCALING.md).

## It is checked against 1983

The Xerox tape carried reference dumps and execution traces beside the image.
Checking against those is far sharper than any test we could write, because
they were produced by the machine this is pretending to be:

| Oracle | Result |
|---|---|
| **`trace2`** | **611 / 611 lines, byte for byte** — every bytecode, send, return and primitive of the image's startup |
| `trace3` | 482-line prefix exact; the one documented divergence is in `tests/unit/test_trace.c` |
| `class.oops` | 446 / 446 classes — pointer, octal, hex and name |
| `method.oops` | 4,494 / 4,494 methods — pointer, class and selector |
| `ref-count-distribution` | 18,391 objects, all 124 histogram buckets |

These run against the Xerox tape, which carries no licence grant from anyone
and so is not distributed with this repository; the suite skips without it and
everything else still passes. See [`doc/LICENSING.md`](doc/LICENSING.md).

**One interpreter, two object memories.** `src/om/om.h` defines the
object-memory operations as macros and exactly one implementation is compiled
in, so the hot path inlines either way:

- `om_bb.c` — Blue Book Chapter 30 verbatim: 16-bit object pointers, object
  table, reference counting. Never shipped. It exists so the *same interpreter*
  can load the real 1983 image and reproduce Xerox's traces.
- `om_mt.c` — 64-bit object table, tagged SmallIntegers, per-worker allocation,
  a marking collector at a safepoint, weak references and ephemerons.

Passing `trace2` therefore validates the same code the parallel system runs.
That is the central trick of the project.

## Quick start

A C11 compiler, GNU make, and SDL3 with its headers. Everything else is in
the tree.

```sh
sudo dnf install gcc make SDL3-devel        # Fedora, RHEL
sudo apt install build-essential libsdl3-dev pkg-config   # Debian, Ubuntu
brew install sdl3 pkg-config                # macOS
```

```sh
make                    # build ./st80
make test               # unit suites, then every profile's own SUnit suites
make deps               # what this machine has, and what it is missing
make help               # targets and variables
```

`make` stops before it compiles anything if the compiler, the C library or
SDL3 is not there, and the message carries the command that installs it —
`make deps` reports on all of them at once, and runs on a machine too bare to
build. If a machine is *meant* to have no display, say so and the graphics
layer becomes a stub: `-bootstrap`, `-eval`, `-doctests` and `make test` all
still work, and `./st80 -run` refuses to open a window and says why.

```sh
make HEADLESS=1
```

A database is optional in the same way, and absent is a first-class outcome: a
build without ODBC has every method of `lib/Database` present, `Odbc
isAvailable` answering false, and any attempt to connect raising with a
sentence saying so. `make deps` says whether a driver manager was found and
names the package that installs one — `unixODBC-devel` on Fedora,
`unixodbc-dev` on Debian; macOS and Windows already have one.

```sh
make NODB=1
```

Bootstrap an image from source and run its desktop:

```sh
./st80 -bootstrap -profile profiles/st2026.profile -o st80.image
./st80 -run st80.image
```

Or evaluate something without a window:

```sh
$ ./st80 -bootstrap -profile profiles/st2026.profile -eval '(1 to: 10) inject: 0 into: [:a :b | a + b]'
55
```

**Use a profile, not `-manifest sources/MANIFEST`.** The manifest is the 226
classes of `sources/` and nothing else, so an image built from it has no
closures, no exceptions, no `Mutex` — and none of the corrections in `lib/`,
including the one that puts the text caret where the next character will
appear. That build is the museum piece, and it is what `profiles/bluebook.profile`
is for:

```sh
./st80 -bootstrap -profile profiles/bluebook.profile -o bluebook.image   # 1983, exactly
```

`profiles/` says what each one composes, and `#requires` chains them:
`st2026` builds on `bluebook`, and the `pharo-*` profiles build on `st2026`.

| Variable | Meaning |
|---|---|
| `OM=mt` | 64-bit threaded object memory — the real system, and the default |
| `OM=bb` | 16-bit Blue Book memory — the validation harness. It loads the 1983 Xerox image and reproduces its traces; it cannot bootstrap an image, and says so if asked to |
| `HEADLESS=1` | Build with no display: the graphics layer becomes a stub, and `make` stops asking for SDL3. Its own build directory, so it can never be half-linked against a graphical one |
| `TSAN=1` | ThreadSanitizer build |
| `ASAN=1` | Address + UB sanitizer build |
| `FONT=`, `SIZE=`, `LEAD=` | inputs to `make font` |

Sanitizer builds go to their own directory, so an instrumented binary can never
be linked against stale uninstrumented objects.

The interface has two habits worth knowing before you decide it is broken —
menus are press-and-hold, and a new window is placed by dragging out its
rectangle. [`doc/Display.md`](doc/Display.md) covers those and the display.

## What runs today

**The 1983 class library, entire.** All 4,500-odd methods of the 226 MIT
classes in `sources/` compile and bootstrap into a live image, and the Browser,
Compiler, Inspector and Debugger in it are the library's own, not ours.

**A language past the Blue Book.** Full closures with non-local return,
`ensure:`/`ifCurtailed:`, an exception system (`signal`, `on:do:`, `retry`,
`resume:`, `pass`), general pragmas, dynamic arrays `{ }`, byte arrays `#[ ]`,
block-local temporaries, and traits by load-time flattening. Bytecodes are
Squeak's V3PlusClosures, because we compile the source ourselves and so choose
the encoding. See [`doc/LanguageExtensions.md`](doc/LanguageExtensions.md).

**Pharo's own code, on Pharo's own tests.** Packages are imported in Tonel
format and held to their upstream suites, ratcheted in both directions —
a score may not fall, and may not rise without being recorded:

| Profile | Tests |
|---|---|
| `st2026` | 205 / 205 |
| `pharo-announcements` | 236 / 236 |
| `pharo-time` | 826 / 826 |
| `pharo-weak` | 225 / 225 |
| `pharo-collections` | 662 / 662 |

**1,177 of those are Pharo's own tests**, run unmodified against this system.
The rest are ours: twelve for the exceptions and concurrency classes 1983 has
no equivalent of, a suite for the 1983 library itself — the numeric tower, the
collections, strings and streams — 43 for the database and 101 for JSON. Every
profile requiring `st2026` inherits all of them, which is why the same 193
appear in every row. The composed image is 295 classes and 5,881 methods.
Where this is going is [`doc/PLAN-TO-PHARO.md`](doc/PLAN-TO-PHARO.md).

**SQL, on every core at once.** `lib/Database` reaches PostgreSQL, MySQL,
SQLite, Oracle and SQL Server through ODBC, with a query builder that finds its
own joins — name a column from a fourth table and the join appears, because a
graph of the schema's foreign keys knows how the tables connect. Every other
Smalltalk schedules its processes green, so N processes share one connection
and take turns at one socket; here N workers hold N connections on N cores and
the database sees N clients. That needs the blocking calls to park the worker
rather than stall it, which is the whole design and is in
[`doc/DATABASE.md`](doc/DATABASE.md) — and is gated by a test that fails if it
regresses: a 0.24s query is interrupted by a safepoint granted in 0.0000s, where
without the parking it takes the whole 0.2396s. `DECIMAL` columns answer exact
`Fraction`s, because a money column read through a float is wrong quietly.

**JSON, and exactly.** `lib/Json` is `JSONObject`, `JSONArray`, a parser and a
writer, with the API breadth of `org.kissweb.json` and two divergences from it
that matter. Numbers stay exact — `1.5` reads as the `Fraction` 3/2, so a tenth
times ten is 1 and a price read out of a document and written back is the price
that was sent, which is the same decision `DECIMAL` columns get and for the same
reason. And the grammar is RFC 8259's rather than org.json's, which forgives an
unquoted name, a trailing comma and any bare word: over a third of the suite is
documents this parser must *refuse*, because a reader that accepts too much
passes every test of a valid document. Names keep the order they were put in, so
a document read and written twice is the same file both times. And a document is
safe to use from more than one process at once — 31 threads share one in
`tests/unit/test_parallel_json.c`, which is a gate rather than a hope.
[`doc/JSON.md`](doc/JSON.md) has the rest, including why not one line could be
copied.

**A screen that is not from 1983.** The display Form is grown to fill the
window rather than letterboxed into it, and text is Inter, proportional and
antialiased — on a system whose BitBlt is still one bit per pixel, because
BitBlt has to stay the Blue Book's. How that is possible is the interesting
part, and it is in [`doc/Display.md`](doc/Display.md).

## Design

**Why keep an object table**, when Spur and every modern VM dropped it? Under
threading the trade inverts: `become:` is one atomic swap instead of a
stop-the-world heap scan, pinning for SDL and FFI is free, and compaction
touches one entry per object rather than every reference to it. We pay one
indirection to buy atomic identity mutation.

**The 1983 library, audited on real workers.** Every class variable in the
image was scanned and the image itself was run on thirty-one workers, and what
leaned on the green scheduler was found and fixed: the Symbol table, `Smalltalk`,
the dependents table, the compiler's `#DoIt`, `Delay`'s timing process and
`Processor yield` are each serialized or reorganized in `lib/`, and six faults
in the scheduler underneath came out with them. `tests/unit/test_parallel_shared.c`
is the gate; the findings are in `doc/CONCURRENCY.md`.

**The semantic break.** Smalltalk-80 guarantees a process is never preempted by
another of the same priority, and the 1983 library uses that as an implicit
lock. We break it deliberately, and supply `Mutex`, `Monitor`, `SharedQueue`
and `Promise` in its place. This is normative:
[`doc/CONCURRENCY.md`](doc/CONCURRENCY.md) is required reading before writing
Smalltalk for this system.

**Thread map.** Thread 0 is a dedicated SDL pump and never executes Smalltalk;
threads 1..N are Smalltalk workers and never call SDL video. SDL3's
main-thread-only rules and macOS's Cocoa run loop force this shape.

**`sources/` is never edited.** Every divergence from 1983 is a new file in
`lib/` or `pharo/`, so "how far have we drifted" has a mechanical answer and
the Blue Book path stays untouched code rather than carefully-preserved code.

## Layout

```
src/port/       threads, mutexes, condition variables, TLS, time, atomics
src/om/         object memory (bb and mt), image readers, collector
src/interp/     bytecode interpreter, contexts, method lookup, primitives
src/gfx/        BitBlt, display, SDL3 pump, the rasterised face
src/sched/      Process, Semaphore, the scheduler
src/compiler/   Smalltalk compiler in C, chunk and Tonel readers
src/db/         ODBC, and nothing else that knows what a database is
src/boot/       image bootstrap
sources/        the 1983 class library (MIT), vendored, frozen — 226 classes
lib/            ours: exceptions, concurrency, SUnit, protocol shims, SQL, JSON
pharo/          imported Pharo packages, each with a PROVENANCE.md
profiles/       which packages compose an image
tools/          make_font.py — rasterises an outline face into the strike
```

## Documentation

| | |
|---|---|
| [`doc/CONCURRENCY.md`](doc/CONCURRENCY.md) | the semantic break, the primitive table, the locking rules |
| [`doc/SCALING.md`](doc/SCALING.md) | the benchmark, and what each kernel is actually measuring |
| [`doc/Display.md`](doc/Display.md) | the window, the face, antialiasing, and what the interface expects of you |
| [`doc/LanguageExtensions.md`](doc/LanguageExtensions.md) | every post-1983 syntax, and where each stands |
| [`doc/DATABASE.md`](doc/DATABASE.md) | SQL through ODBC, the join graph, and why a query does not stop the world |
| [`doc/JSON.md`](doc/JSON.md) | RFC 8259, why the numbers stay exact, and why not one line could be ported |
| [`doc/PLAN-TO-PHARO.md`](doc/PLAN-TO-PHARO.md) | where this is going, sized honestly |
| [`manual/`](manual/) | **a book-length manual** on the system, the language, the database and JSON — `cd manual && make` |
| [`Windows.md`](Windows.md) | building with MSVC, and what a real one found |
| [`macOS.md`](macOS.md) | building with the same makefile Linux uses, and the four places Apple differs |
| [`doc/LICENSING.md`](doc/LICENSING.md) | what may be redistributed, and what may not |

## Provenance

The tree is not all one licence, and the distinction matters:

- **Ours** — `src/`, `lib/`, `tests/`, `tools/`, `doc/`: BSD 2-Clause.
- **`lib/Database`** — ours and BSD 2-Clause too, and a port of
  `org.kissweb.database` from [Kiss](https://github.com/blakemcbride/Kiss), by
  the same author under the same licence. No Java was copied: the query
  builder, the join search and the type mapping are the same designs
  re-expressed, and JDBC was replaced with ODBC.
  [`lib/Database/PROVENANCE.md`](lib/Database/PROVENANCE.md) records every
  place the two now differ, and why.
- **`lib/Json`** — ours and BSD 2-Clause, and *not* a port: the obvious source,
  `org.kissweb.json`, is a fork of JSON-java and carries JSON.org's licence,
  which adds "The Software shall be used for Good, not Evil" to MIT and is for
  that reason not free software. Nothing could be taken, so nothing was; what
  crossed is the shape of the API, and every line is written against RFC 8259.
  [`lib/Json/PROVENANCE.md`](lib/Json/PROVENANCE.md) records it.
- **`sources/`** — the 1983 class library from
  [`markbush/Smalltalk-80-Sources`](https://github.com/markbush/Smalltalk-80-Sources),
  MIT. Vendored and never edited.
- **`pharo/`** — imported from [Pharo](https://github.com/pharo-project/pharo),
  MIT with parts under Apache-2.0. Every file keeps its notice and every
  package carries a `PROVENANCE.md` recording the upstream commit and any
  local edit.
- **`src/gfx/font_face.c`** — a rasterisation of Inter (SIL Open Font License
  1.1). The font itself is not vendored: `tools/make_font.py` runs against one
  installed on the machine and the *output* is what is checked in.
  `src/gfx/LICENSE.font` is the licence it derives under.

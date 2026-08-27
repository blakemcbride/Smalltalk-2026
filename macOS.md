# Building on macOS

`make` is the build. There is no separate makefile and no Xcode project:
macOS is a POSIX target and it uses the same GNU makefile Linux does, with
three small branches for the places Apple differs. What you need is the
Xcode Command Line Tools and SDL3.

This is a much shorter story than [`Windows.md`](Windows.md), and the reason
is worth knowing before you start. The Windows build is a second makefile
maintained by hand against a second set of platform code. The macOS build is
the Linux build with `-pthread` taken off the command line. Almost everything
you compile here is the code that is tested every day.

**Read this first.** No macOS machine has compiled it — see the last section,
which says exactly what was checked and how. The gap is far narrower than on
Windows, but it is a gap.

## The short version

```sh
xcode-select --install
brew install sdl3 pkg-config

cd /path/to/Smalltalk-2026
make
./st80 -bootstrap -profile profiles/st2026.profile -o st80.image
./st80 -run st80.image
```

If `make` stops, it stops before compiling anything and tells you the command
that installs what is missing. `make deps` reports on all of it at once and
runs on a machine too bare to build.

## 1. The toolchain

```sh
xcode-select --install
```

That is the whole requirement: the Command Line Tools carry `clang`, `make`,
`ar` and the SDK. Full Xcode works and is not needed.

**The build uses `cc`, which is Apple clang.** The makefile says `CC ?= gcc`,
and that line does nothing — GNU make predefines `CC = cc`, so `?=` finds it
already set and leaves it alone. On Linux `cc` is gcc and on macOS it is
Apple clang, which is the right answer on both without anyone configuring
anything. `make deps` probes the same `cc`, so the two cannot disagree.

If you would rather use a Homebrew gcc, name it: `make CC=gcc-14`.

**Apple's `make` is GNU make 3.81**, frozen at the last GPLv2 release and
nineteen years old. It is enough. The makefile uses `else ifeq`, `$(or)` and
`$(and)`, all of which arrived in 3.81, and nothing from 4.x — no `$(file)`,
no `.ONESHELL`, no `!=`, no `$(intcmp)`. You do not need Homebrew's `gmake`,
though it will work if you have it.

**C11 atomics** are `<stdatomic.h>` via `src/port/st_atomic.h`, supported by
Apple clang since 3.6 and needing no flag — unlike MSVC, which still gates
them. Two self-imposed limits keep every operation lock-free: only
naturally-aligned int-, bool- and pointer-sized types, and never `_Atomic` on
a struct.

## 2. SDL3

```sh
brew install sdl3 pkg-config
```

st80 draws its display through SDL3 and finds it with `pkg-config`.
Homebrew's `sdl3` installs `sdl3.pc` beside the library and Homebrew's
`pkg-config` knows where to look, so the two together are all it takes.

**On Apple Silicon, pkg-config is not optional.** Homebrew installs under
`/opt/homebrew`, and Apple clang does not search that prefix — it searches
`/usr/local`, which is where Homebrew put things on Intel Macs and does not
on this one. The makefile has a fallback for a machine that carries SDL3 but
not pkg-config: it asks the compiler to link a bare `-lSDL3`. That fallback
finds an Intel Homebrew's SDL3 and will not find an Apple Silicon one. Make
sure `/opt/homebrew/bin` is on `PATH` ahead of anything else that provides a
`pkg-config`, or the same problem arrives wearing a different hat: MacPorts'
pkg-config knows nothing about Homebrew's `.pc` files.

OpenSSL, which the https client needs, is the same story one step further:
Homebrew's `openssl` is *keg-only* and puts its `.pc` files where nothing
looks by default. `brew install openssl pkg-config` and then
`PKG_CONFIG_PATH=$(brew --prefix openssl)/lib/pkgconfig make`; without it
the build is a valid one with no TLS, and `make deps` says so.

Anything not installed by Homebrew — MacPorts, a `cmake` build of SDL3 you
installed yourself — works the same way as long as pkg-config can see it:

```sh
PKG_CONFIG_PATH=/opt/local/lib/pkgconfig make
```

The official SDL3 release DMG ships an `SDL3.xcframework`, which is meant for
Xcode projects and carries no `.pc` file. It is not the path to take here.

`make deps` answers the question directly, and answers it for pkg-config,
libm, pthreads and the compiler at the same time. On a Mac it should
come out in this shape — the layout and the row names are the script's,
the values are whatever your machine says:

```
$ make deps
st80 external requirements, as this machine answers for them:

  C compiler   cc                         Apple clang version ...
  pkg-config   pkg-config                 2.3.0
  pthreads     libSystem                  links
  libm         -lm                        links
  SDL3         -L/opt/homebrew/lib -lSDL3 links -- pkg-config, 3.2.20
```

Note `libSystem` where Linux says `-pthread`. That is the Darwin branch
reporting itself: Apple folds pthreads into libSystem and wants neither
`-pthread` nor `-lpthread`, and both the makefile and the deps script carry
the same rule so they cannot drift apart.

### No display at all

If the machine is meant to have no display — a build server, a CI runner —
say so and the graphics layer compiles to a stub. `-bootstrap`, `-eval` and
`-doctests` all still work, the whole test suite runs, and `./st80 -run`
refuses to open a window and says why.

```sh
make HEADLESS=1
```

`HEADLESS=1` means it on a machine with SDL3 as much as on one without: a
flag that asks for the stub and silently builds the window when the library
happens to be present is a flag that cannot be used to reproduce anything.
It gets its own build directory for the same reason.

## 3. Build

```sh
make                    # build ./st80
make test               # unit suites, then every profile's own SUnit suites
make bench              # the parallel scaling benchmark
make deps               # what this machine has, and what it is missing
make help               # targets and variables
make clean              # remove build artifacts
```

| Variable | Meaning |
|---|---|
| `OM=mt` | 64-bit threaded object memory — the real system, and the default |
| `OM=bb` | 16-bit Blue Book memory — the validation harness. It loads the 1983 Xerox image and reproduces its traces; it cannot bootstrap an image, and says so if asked to |
| `HEADLESS=1` | no display; the graphics layer becomes a stub and `make` stops asking for SDL3 |
| `TSAN=1` | ThreadSanitizer build |
| `ASAN=1` | Address + UB sanitizer build |
| `OPT=-O0` | override the optimisation flags |
| `FONT=`, `SIZE=`, `LEAD=` | inputs to `make font` |

Each variant gets its own build directory, so an instrumented binary can
never be linked against stale uninstrumented objects, and `make HEADLESS=1`
followed by `make` cannot relink against whichever objects survived.

`make font` is the one target that wants more than the two requirements
above — Pillow and an installed outline face — and it is not part of any
build. `src/gfx/font_face.c` is checked in, so building needs no font and no
Python.

## 4. Make an image and run it

The binary is a virtual machine with a compiler in it. It has no image until
you build one:

```sh
./st80 -bootstrap -profile profiles/st2026.profile -o st80.image
./st80 -run st80.image
```

That is 264 classes and 5,123 methods — the 1983 class library plus closures,
exceptions, concurrency and SUnit. Or evaluate something without a window:

```sh
$ ./st80 -bootstrap -profile profiles/st2026.profile -eval '(1 to: 10) inject: 0 into: [:a :b | a + b]'
55
```

`./st80 -version` will say `platform : macOS`, which is the one place the
binary tells you which branch it took.

**Use a profile, not `-manifest sources/MANIFEST`.** The manifest is the 226
classes of `sources/` and nothing else, so an image built from it has no
closures, no exceptions and no `Mutex`. `profiles/bluebook.profile` is what
the museum piece is for. `OM=bb` cannot bootstrap at all — it is the harness
that loads the real Xerox image, and asked to build one it refuses and says
why.

### The window

An unbundled binary, which is what this is, still opens a window and takes
keyboard focus on any current macOS. It has no `.app`, so it appears in the
Dock under the executable's name and carries the generic icon. Nothing here
needs an entitlement, a signature or a Privacy & Security exception: SDL uses
ordinary window-server APIs, and a binary you compiled yourself is not
quarantined.

**On a Retina display the screen is scaled by the window server, not by us.**
`GFX_open` creates its window with `SDL_WINDOW_RESIZABLE` and nothing else —
no `SDL_WINDOW_HIGH_PIXEL_DENSITY` — so SDL renders at point resolution and
macOS scales the result up to the physical pixels. On a 2× panel every
display pixel becomes a 2×2 block of physical ones, filtered rather than
sharp. `choose_scale` sizes the window from `SDL_GetDisplayUsableBounds`,
which on macOS is measured in points too, so the automatic scale is chosen in
the same units and comes out sensible; it is the last hop to the panel that
is not ours. If it reads soft, an explicit integer scale is the lever:

```sh
ST_DISPLAY_SCALE=2 ./st80 -run st80.image
```

The rest of the display settings are environment variables — `ST_DISPLAY_THEME`
(`paper`, `classic`, `dark`), `ST_DISPLAY_WINDOW=WxH`, `ST_DISPLAY_PRESENTATION`
(`integer`, `letterbox`, `stretch`). `./st80 -help` lists them all.

### Two habits of the interface

Menus are press-and-hold, and a new window is placed by dragging out its
rectangle. Neither is a bug and both will read as one.
[`doc/Display.md`](doc/Display.md) covers them, and the display generally.

## 5. Tests

```sh
make test
```

Unlike Windows, all of it runs. `make test` is `unit-test` and `suite-test`:
sixteen C suites in `tests/unit/`, then `tests/run_profiles.sh`, which
bootstraps each profile and runs Pharo's own tests inside it, holding the
score to `tests/profiles.expected` in both directions. That script is POSIX
`sh` with `grep -E`, `sed` and `printf` and no GNU-isms, so macOS's own tools
run it.

One thing skips: the Xerox oracle. `test_trace` and the reference-dump checks
run against the 1983 tape, which carries no licence grant from anyone and is
not distributed with this repository. They say so and pass.
See [`doc/LICENSING.md`](doc/LICENSING.md).

Sanitizers are Apple clang's:

```sh
make TSAN=1 test
make ASAN=1 test
```

Both are supported on Intel and Apple Silicon. Neither has been run on
either.

## Apple Silicon, and why `make bench` will read low

This is the one number on a Mac that will not match the README, and it is the
machine rather than the interpreter.

`ST_cpu_count()` on macOS is `sysconf(_SC_NPROCESSORS_ONLN)`, which on an
M-series chip counts performance and efficiency cores together — an M4 Pro
reports 14, of which 10 are P and 4 are E. A speedup is a ratio against one
worker, and one worker runs on a performance core. Fourteen workers spread
across a mixture therefore measure the chip's asymmetry as much as the
interpreter's scaling.

`tests/bench/bench_parallel.c` knows about this and steps around it — it pins
to one thread per physical core of the fastest kind before measuring, and
says so when it does:

```
pinned to 8 core(s) of 32 logical CPUs: one thread per physical core, fastest kind only
```

That code is inside `#if defined(__linux__)`, and on every other platform
`pin_to_physical_cores` is an empty function. It reads core topology out of
`/sys/devices/system/cpu/`, which macOS does not have, and pins with
`sched_setaffinity`, which macOS does not have either — Darwin's nearest
equivalent, `thread_policy_set` with `THREAD_AFFINITY_POLICY`, is a hint that
Apple Silicon ignores outright. So the benchmark runs unpinned on a Mac and
the speedups come out below the README's.

The correctness results are unaffected. Every worker computes something only
it can check, so a fault arrives as a wrong answer rather than as a slow one.
`make test` on a Mac means what it means on Linux; `make bench` on a Mac
means something narrower, and the honest reading is "at least this fast".

## What is macOS-specific in the code

The whole list. Three source branches and one makefile branch.

| Where | What |
|---|---|
| `Makefile`, `tools/check-deps.sh` | `uname -s` = `Darwin` drops `-pthread` and `-lpthread`. Apple folds pthreads into libSystem and wants neither. Both files carry the rule so a change to one cannot leave the other wrong. |
| `src/port/st_port.c` `ST_thread_set_name` | Apple's `pthread_setname_np` names the calling thread and takes one argument, where Linux's takes a thread and caps the name at 16 bytes. |
| `src/port/st_port.c` `ST_cpu_count` | The `sched_getaffinity` path — what the process is *allowed* to run on, which is what a `taskset` or a container cpuset makes different from what the machine has — is `__linux__` only. macOS falls through to `sysconf(_SC_NPROCESSORS_ONLN)`. |
| `src/main.c` `print_version` | `platform : macOS`. |
| `tests/bench/bench_parallel.c` | Core pinning is `__linux__` only; see above. |

And two things that are *not* branched, and matter more than the ones that
are:

**No C11 `<threads.h>`, because of macOS.** Apple ships no such header in any
SDK, and macOS is a required target — so the portability layer is a shim over
pthreads and the Win32 API rather than over C11 threads. Windows inherits
that decision; macOS caused it. [`doc/PORTABILITY.md`](doc/PORTABILITY.md).

**Thread 0 is the SDL pump, because of macOS.** `SDL_PumpEvents`,
`SDL_CreateRenderer` and `SDL_LockTexture` are documented main-thread-only,
and on macOS "main thread" means the thread that entered `main()` — Cocoa's
run loop is bound to it and cannot be moved. So thread 0 never executes
Smalltalk and workers never call SDL video. `src/main.c` includes
`<SDL3/SDL_main.h>` so SDL can stand the entry point up.

In practice today this costs nothing, because `-run` is single-threaded:
`GFX_pump` is called from the interpreter loop in `do_run`, on the thread
that entered `main()`, between bytecode slices. `WORKER_start` is called only
from the tests and the benchmark. The window and the worker pool do not yet
meet, and when they do, this is the shape they have to meet in.
[`doc/CONCURRENCY.md`](doc/CONCURRENCY.md) is normative on the rest.

## Known rough edges

1. **No Mac has compiled this.** See below.
2. **`make bench` reads low on Apple Silicon.** Not a defect, but it will
   look like one. The section above says why. Making it right means a Darwin
   `pin_to_physical_cores` — `sysctl hw.perflevel0.logicalcpu` names the
   P-cores — and Apple Silicon offers no way to pin a thread to them, so the
   honest version of that fix may be to report the mixture rather than step
   around it.
3. **`-D_GNU_SOURCE` is passed on every platform**, including this one. It is
   a glibc feature macro and Darwin's headers do not read it, so it is inert
   rather than wrong: `strdup`, `S_IFDIR` and the rest are visible on macOS
   at `__DARWIN_C_FULL`, which is the default unless `_POSIX_C_SOURCE` or
   `_ANSI_SOURCE` is set, and `-std=c11` sets neither.
4. **No Retina backing store.** `SDL_WINDOW_HIGH_PIXEL_DENSITY` is not passed
   and the renderer works in points. The fix is that flag plus honouring
   `SDL_GetWindowPixelDensity` in the presentation, and it wants a Mac in
   front of it to judge.
5. **`sched_yield` had no header.** `src/port/st_port.c` calls it and reached
   the declaration through `<pthread.h>`, which is a fact about glibc and
   Darwin rather than a promise POSIX makes — and with
   `-Werror=implicit-function-declaration` in the warning set, the first libc
   to stop doing it is a build that stops rather than a warning. Now included
   explicitly. This is the only code change this document required.

## What has been checked, and how

**Checked, on Linux:**

- **The Darwin branch of the makefile produces the right command line.**
  `make UNAME_S=Darwin -Bn` emits zero occurrences of `pthread` across the
  whole build; the ordinary Linux run emits 23. The Darwin path is reachable
  and does what it says.
- **The non-Linux POSIX branches compile.** Everything built with
  `-U__linux__`, which takes `ST_cpu_count` off the `sched_getaffinity`
  path, takes `ST_thread_set_name` off the Linux `pthread_setname_np`,
  and reduces `pin_to_physical_cores` to its empty stub — the same three
  branches macOS takes. Both object memories build and link, and all
  sixteen unit suites and the benchmark compile, under the production
  warning set: `-std=c11 -Wall -Wextra -Wpedantic
  -Werror=implicit-function-declaration -O2`. It is the same compiler
  taking the same branches, which is most of what a Mac would be doing
  and not the part that is a different compiler.
- **The `<sched.h>` change.** Full build and all sixteen unit suites after
  it: 0 failures, including the 31-thread parallel suites.
- **Apple's make is new enough.** The makefile was searched for every GNU
  make 4.x construct — `$(file)`, `.ONESHELL`, `!=`, `::=`, `$(intcmp)`,
  `$(let)`, `.RECIPEPREFIX`, `$(guile)` — and uses none. What it does use
  (`else ifeq`, `$(or)`, `$(and)`) arrived in 3.81, which is what Apple
  ships.
- **`tests/run_profiles.sh` is portable.** POSIX `sh`, `grep -E`, `sed`,
  `printf`, `tail`. No `sed -i`, no `grep -P`, no `readlink -f`.
- The platform claims in the tables above were read out of the source, not
  recalled.

**Not checked:**

- **No macOS machine has compiled any of this**, and no Apple clang has seen
  it — the closest available was gcc with `__linux__` undefined, which
  exercises the same branches but not the same compiler.
  `doc/PORTABILITY.md` has said so since the portability work landed and it
  is still true.
- Whether Darwin's `<pthread.h>` chain declares `sched_yield` — which is why
  `<sched.h>` is now included rather than assumed.
- Homebrew formula names and prefixes, the deps report's exact wording on a
  Mac, and the Retina behaviour, all of which are reasoned from documented
  behaviour rather than observed.
- Both sanitizers.
- The socket layer and `st80 -serve`. `src/net/st_socket.c`'s POSIX half is
  what a Mac would run — `poll`, `pipe` where `pipe2` is missing,
  `getentropy` — and those are the branches Linux with `__linux__` undefined
  compiled, not a Mac; the server has never listened on one.

The gap here is real but narrow: the shared makefile and the shared source
are exercised continuously, and the macOS-specific surface is the five rows
of the table above. The first build will probably turn up something anyway.
If it does, please report it at
<https://github.com/blakemcbride/Smalltalk-2026>.

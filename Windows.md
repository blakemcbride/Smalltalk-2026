# Building on Windows

`nmake /f Makefile.msvc` is the native build. It wants Visual Studio 2022
version 17.8 or later, an SDL3 development package, and nothing else — no
Python, no pkg-config, no vendored dependency to fetch. The font is
rasterised into `src/gfx/font_face.c` and checked in; the class library is
plain text in `sources/`, `lib/` and `pharo/`.

**Read this first.** This build is partly proven. A real MSVC has compiled
the portability layer and the interpreter; the files after those have not
been through a Windows compiler yet, and the program has never linked or
run there. The last section says exactly where that line falls. Everything
below is the build as designed, and where a step is a known rough edge
rather than a confident instruction, it says so.

## The short version

In an **x64 Native Tools Command Prompt for VS 2022**:

```bat
cd \path\to\Smalltalk-2026
nmake /f Makefile.msvc SDL3=C:\SDL3-3.2.20
copy C:\SDL3-3.2.20\lib\x64\SDL3.dll .
st80.exe -bootstrap -profile profiles/st2026.profile -o st80.image
st80.exe -run st80.image
```

Note the forward slashes in `profiles/st2026.profile`. That is not a
typo — see [Forward slashes](#forward-slashes-in-paths-given-to--profile).

## 1. The compiler

Install **Visual Studio 2022 17.8 or later** with the *Desktop development
with C++* workload, or the smaller *Build Tools for Visual Studio 2022* if
you want no IDE. Both give you `cl`, `nmake` and the Developer Command
Prompts.

17.8 is a floor, not a preference. C11 atomics arrived in 17.5 and the rest
of the C11 support this code leans on settled in 17.8. `src/port/st_atomic.h`
includes `<stdatomic.h>` directly and `_Atomic int64_t` appears in it, so
there is no path through this build that avoids the question.

Newer is fine and does not change the flag below: this has been run
against MSVC 14.50 under Visual Studio 18, which still keeps C11 atomics
behind `/experimental:c11atomics`.

**Use the x64 prompt.** The Start menu offers several; the one you want is
*x64 Native Tools Command Prompt for VS 2022*. The plain *Developer Command
Prompt* targets x86, and since the build links `lib\x64\SDL3.lib` the link
ends in

```
LNK1112: module machine type 'x64' conflicts with target machine type 'x86'
```

which names the symptom and not the wrong shortcut that caused it.

**`/experimental:c11atomics`.** `cl` still keeps C11 atomics behind that
flag, which is not what "MSVC has supported C11 atomics since 17.5" leads
anyone to expect. Without it the build stops on the first file that
includes `st_atomic.h`:

```
vcruntime_c11_stdatomic.h(12): fatal error C1189: #error:
    "C atomic support is not enabled"
```

`Makefile.msvc` sets it for `cl` and not for anything else. If `cl` ever
answers `D9002: ignoring unknown option` for it and then fails the same
way, the flag has been renamed and this is the line to change.

**clang-cl** works and is what most portable C projects reach for on Windows,
since it brings the full GCC and Clang atomics with it. It ships inside the
same Visual Studio installer under *C++ Clang tools for Windows*:

```bat
nmake /f Makefile.msvc CC=clang-cl SDL3=C:\SDL3-3.2.20
```

Any `CC` other than `cl` drops `/experimental:c11atomics`, which clang-cl
rejects.

## 2. SDL3

st80 draws its display through SDL3 and needs the development package —
headers and an import library, not just the runtime DLL.

Take **`SDL3-devel-<version>-VC.zip`** from
<https://github.com/libsdl-org/SDL/releases> and unzip it anywhere. Point
`SDL3=` at the directory holding `include\` and `lib\`:

```
C:\SDL3-3.2.20\
    include\SDL3\SDL.h ...
    lib\x64\SDL3.lib          <- linked against
    lib\x64\SDL3.dll          <- needed at run time
```

Other sources put it elsewhere. vcpkg and an SDL3 you built and installed
yourself both use a flat `lib\`, so name the import library directly:

```bat
nmake /f Makefile.msvc SDL3=C:\vcpkg\installed\x64-windows SDL3LIB=C:\vcpkg\installed\x64-windows\lib\SDL3.lib
```

**`SDL3.dll` must be findable at run time.** Copy it next to `st80.exe`, or
put its directory on `PATH`. This is the one that bites: the link succeeds,
and then the program refuses to start with *"The code execution cannot
proceed because SDL3.dll was not found"* — or, if you copied the 32-bit one,
with `0xc000007b`.

You do not need SDL's own `SDL3.lib` for `main`. SDL3's `SDL_main.h` is
header-only and `src/main.c` includes it, so SDL stands up the entry point
itself and there is no `SDL3main.lib` to hunt for.

### No display at all

If the machine is meant to have no display, say so and the graphics layer
compiles to a stub. `-bootstrap`, `-eval` and `-doctests` all still work,
the test suites still run, and `st80.exe -run` refuses to open a window and
says why.

```bat
nmake /f Makefile.msvc HEADLESS=1
```

`HEADLESS=1` wins over `SDL3=` if both are given. A flag that asks for the
stub and quietly builds the window instead cannot be used to reproduce
anything.

Without `SDL3=` and without `HEADLESS=1` the build stops before it compiles
anything and tells you both. It used to build the stub silently, so a
mistyped path and a missing library produced the same result: an `st80.exe`
that opened no window, and only said why a day later.

## 3. Build

```bat
nmake /f Makefile.msvc SDL3=C:\SDL3-3.2.20
```

Objects go to `build\<om>-msvc\`; the binary is `st80.exe` in the top of the
tree.

| Variable | Meaning |
|---|---|
| `OM=mt` | 64-bit threaded object memory — the real system, and the default |
| `OM=bb` | 16-bit Blue Book memory — the validation harness. It loads the 1983 Xerox image and reproduces its traces; it cannot bootstrap an image, and says so if asked to |
| `SDL3=` | root of an SDL3 development package: the directory holding `include\` and `lib\` |
| `SDL3LIB=` | the import library itself, when it is not at `lib\x64\SDL3.lib` |
| `HEADLESS=1` | no display; the graphics layer becomes a stub and the build stops asking for SDL3 |
| `CC=clang-cl` | build with clang-cl instead of cl |

Each `OM` gets its own build directory, so an `mt` object can never be
linked against a `bb` one.

`nmake /f Makefile.msvc clean` removes `build\`, `st80.exe`, and the `.pdb`
and `.ilk` beside it.

There is no `deps` target on this side. The GNU makefile's is
`tools/check-deps.sh`, which is a shell script and knows package managers
this platform does not have; on Windows the two external requirements are the
two named above and the build's own error messages carry them.

## 4. Make an image and run it

The binary is a virtual machine with a compiler in it. It has no image until
you build one:

```bat
st80.exe -bootstrap -profile profiles/st2026.profile -o st80.image
st80.exe -run st80.image
```

That is 264 classes and 5,123 methods — the 1983 class library plus
closures, exceptions, concurrency and SUnit. Or evaluate something without a
window:

```bat
st80.exe -bootstrap -profile profiles/st2026.profile -eval "(1 to: 10) inject: 0 into: [:a :b | a + b]"
```

Double quotes, not single: `cmd` does not treat `'` as a quote, so a
single-quoted expression arrives split on its spaces. Inside the expression,
Smalltalk's own string literals are single-quoted and stay that way.

`profiles/` says what each profile composes, and `#requires` chains them:
`st2026` builds on `bluebook`, and the `pharo-*` profiles build on `st2026`.
`profiles/bluebook.profile` is 1983 exactly, and is the only one that gives
you the museum piece.

`OM=bb` cannot bootstrap. It is the harness that loads the real Xerox image
and reproduces its traces; asked to build an image it refuses and says why.
Build the default `OM=mt` unless you are working on the oracle.

### Forward slashes in paths given to `-profile`

`src/boot/profile.c` splits and joins paths on `/` and only `/`. It has a
Win32 directory walk — `FindFirstFileA` where POSIX gets `opendir` — but
`directory_of` is `strrchr(path, '/')` on every platform.

So `-profile profiles\st2026.profile` finds no separator, decides the
profile lives in `.`, and then cannot resolve the `#requires : [ 'bluebook' ]`
inside it, because it looks for `.\bluebook.profile` rather than
`profiles\bluebook.profile`. `-profile profiles/st2026.profile` works,
because every Win32 file API accepts forward slashes.

The same holds for source paths handed to `-bootstrap`, `-syntax` and
`-primitives`. **Use forward slashes.** Tab completion in `cmd` will give
you backslashes, so this is worth remembering rather than deriving.

This is a rough edge in the code, not a property of Windows, and it is
listed as such below.

### Environment

The display settings are environment variables; `set` them before running.

```bat
set ST_DISPLAY_THEME=dark
set ST_DISPLAY_SCALE=2
set ST_DISPLAY_WINDOW=1600x1200
st80.exe -run st80.image
```

`st80.exe -help` lists the rest.

### Two habits of the interface

Menus are press-and-hold, and a new window is placed by dragging out its
rectangle. Neither is a bug and both will read as one.
[`doc/Display.md`](doc/Display.md) covers them, and the display generally.

## 5. Tests

```bat
nmake /f Makefile.msvc test
```

That builds and runs all sixteen unit suites in `tests/unit/`. Each is
wrapped in `#ifdef ST_OM_MT` or `#ifdef ST_OM_BB` and reports itself skipped
under the other memory, so the same list is right for both builds. nmake
stops at the first suite that fails, at the suite that failed.

Two things do not run here:

**The Xerox oracle.** `test_trace` and the reference-dump checks run against
the 1983 tape, which carries no licence grant from anyone and is not
distributed with this repository. They skip when it is absent, and say so.
See [`doc/LICENSING.md`](doc/LICENSING.md).

**The imported packages' own suites.** The GNU makefile's `suite-test`
bootstraps each profile and runs Pharo's tests inside it, holding the score
to `tests/profiles.expected`. It is `tests/run_profiles.sh`, a POSIX shell
script, and it has no nmake equivalent. Run it from Git Bash, MSYS2 or WSL
against the `st80.exe` you just built:

```sh
sh tests/run_profiles.sh ./st80.exe tests/profiles.expected
```

Or run one profile by hand, which is all the script does with a comparison
around it:

```bat
st80.exe -bootstrap -profile profiles/pharo-collections.profile -tests
```

## Two other ways to build on Windows

The MSVC build above is the one `Makefile.msvc` exists for. Neither of these
needs it.

### WSL2 — the build that is actually verified

Everything in this project is developed and tested on Linux. Under WSL2 you
get that build, unmodified, including `make test`, `make deps` and
`make bench`:

```sh
sudo apt install build-essential libsdl3-dev pkg-config
make
make test
./st80 -bootstrap -profile profiles/st2026.profile -o st80.image
./st80 -run st80.image
```

The window comes up through WSLg, which is present by default on Windows 11
and on Windows 10 builds from 19044 with an updated WSL. If `libsdl3-dev` is
not in your distribution's archive yet — it is new enough that some releases
predate it — either build SDL3 from source or use `make HEADLESS=1`, which
gives up the window and nothing else.

This is the path to take if you want to *run* the system on Windows rather
than *port* it. It is the only one whose test results mean anything today.

### MSYS2 — the GNU makefile, natively

MSYS2's UCRT64 environment has gcc, GNU make, pkg-config and
`mingw-w64-ucrt-x86_64-sdl3`, and MinGW defines `_WIN32`, so
`src/port/st_port.h` takes the same Win32 branch MSVC does — SRWLOCK,
CONDITION_VARIABLE, fibre-local storage. In principle `make` then works as
it does on Linux and you get the targets `Makefile.msvc` has no answer for.

**Untried, with one thing to watch.** The GNU makefile has no notion of an
`.exe` suffix anywhere in it: it links to `build/mt/st80` and copies that to
`./st80`. MinGW's gcc appends `.exe` to an output name that has none, so the
file make asked for is not the file that appears. MSYS2's runtime makes
`stat("st80")` find `st80.exe`, which is very likely enough to paper over
it — but "very likely" is not a build instruction, and nobody has run it.
If it relinks on every invocation, or the final copy fails, that is what you
are looking at.

## What is Windows-specific in the code

Worth knowing before the first build error, because most of what could go
wrong is in this list.

| Where | What |
|---|---|
| `src/port/st_port.h`, `.c` | The whole threading layer forks here on `_WIN32`. Threads, mutexes and condition variables become `CreateThread`, `SRWLOCK` and `CONDITION_VARIABLE`. Thread-local storage is **fibre**-local: `FlsAlloc` is the only Win32 flavour that takes a destructor callback, which is what `pthread_key_create` provides on the other side. |
| `src/boot/profile.c` | Directory walking forks on `_WIN32`: `FindFirstFileA`/`FindNextFileA` where POSIX gets `opendir`/`readdir`. Path *splitting* does not fork — see above. |
| `src/interp/prim.c` | The 1983 File and FilePage primitives — open, close, size, read a page, write a page, truncate, list a directory. POSIX answers all seven directly; Windows answers none of them under those names, so they go through a shim at the top of the file's primitive section. `pread`/`pwrite` become `ReadFile`/`WriteFile` with the offset in an `OVERLAPPED`, which is positional in the same way and is not the same thing as seeking first. Every `_open` carries `_O_BINARY`: MSVC's defaults to text mode, and these primitives carry image pages. |
| `src/port/st_atomic.h` | `<stdatomic.h>` directly. Two self-imposed limits keep every operation lock-free everywhere: only naturally-aligned int-, bool- and pointer-sized types, and never `_Atomic` on a struct. |
| `src/main.c` | Includes `<SDL3/SDL_main.h>` so SDL can stand up the process entry point. Thread 0 is a dedicated SDL pump that never executes Smalltalk; threads 1..N are Smalltalk workers and never call SDL video. SDL3's main-thread rules force this shape. See [`doc/CONCURRENCY.md`](doc/CONCURRENCY.md). |
| everywhere | Every file that holds binary — images, `.st` sources, chunk files, Tonel files, screenshots — is opened `"rb"` or `"wb"`. The two text-mode opens are the manifest readers in `main.c` and `profile.c`, one path per line, and both strip CR themselves anyway. |

**No `<threads.h>`.** Apple ships no such header in any macOS SDK and macOS
is a required target, so the portability layer is a shim over pthreads and
Win32 rather than over C11 threads. Windows is not the reason it exists, but
Windows is why it has two halves. [`doc/PORTABILITY.md`](doc/PORTABILITY.md)
has the rest.

### Line endings

There is no `.gitattributes`, so a checkout with `core.autocrlf=true` gives
you CRLF in the `.st` sources. The readers handle it — `chunk.c`, `lexer.c`
and `tonel.c` all treat CR, LF and CRLF alike, and Tonel normalises to the
CR that Smalltalk strings use internally. Setting `git config core.autocrlf
input` is still the tidier choice, since it keeps the working tree matching
what every other platform compiles.

## Known rough edges

Listed rather than smoothed over, because a first Windows build should know
which surprises are already accounted for.

1. **The build has been run on Windows, and has not finished there.** It
   compiles `src/port` and `src/interp` under MSVC 14.50 and stops
   somewhere after; every file past that point is still unproven. See
   below for exactly where the line is.
2. **`-profile` needs forward slashes.** `src/boot/profile.c`'s
   `directory_of` and `resolve` are `/`-only, so a backslash path silently
   resolves `#requires` against the wrong directory. Fixing it properly means
   splitting on either separator under `_WIN32`, and treating `C:\` as
   absolute in `resolve`, which today tests only `path[0] == '/'`.
3. **The object list in `Makefile.msvc` is maintained by hand.** nmake has no
   wildcard, so the GNU makefile picks up a new `.c` under `src/` and this one
   does not. It had already drifted once: `src/boot/` was written after
   `Makefile.msvc` and nothing in it linked `bootstrap.c` or `profile.c`, so
   the build ended in unresolved externals from `main.obj` for as long as the
   file existed. Add to both.
4. **`strdup`.** [`doc/PORTABILITY.md`](doc/PORTABILITY.md) says it appears
   nowhere under `src/`; three files call it. MSVC declares it and links it,
   deprecated in favour of `_strdup`, so the build sets
   `_CRT_NONSTDC_NO_WARNINGS` rather than leaving a run of C4996 under `/W4`.
5. **The CRT's POSIX names.** `strdup`, `struct stat` and `S_IFDIR` are
   used by `src/main.c` and `src/boot/profile.c` under those spellings,
   and the CRT hides them when `__STDC__` is 1 — which `/std:c11` is one
   way of causing. The build asks for them with
   `/D_CRT_DECLARE_NONSTDC_NAMES=1`, which is free where they were
   already declared. If a future CRT drops that switch, the answer is
   the underscore spellings, which is what the `prim.c` shim already
   uses for exactly this reason.
6. **`shell32.lib`.** If the link ever fails on `__imp_CommandLineToArgvW`,
   that is SDL3's `SDL_main.h` wanting it; add `shell32.lib` to the link
   line. It should not, on a console-subsystem build where `SDL_RunApp` lives
   in the DLL, but it is the one plausible missing default library.
7. **No sanitizer builds.** The GNU makefile's `TSAN=1` and `ASAN=1` have no
   nmake equivalent. MSVC has `/fsanitize=address`; it has no thread
   sanitizer, and the thread sanitizer is the one that matters here — the
   suite is a gate at 31 threads. Use Linux or WSL2 for that.

## What has been checked, and how

Honest accounting, because the alternative is a document that reads as
though someone ran it.

**Confirmed on Windows, by MSVC 14.50 under Visual Studio 18:**

- `nmake /f Makefile.msvc` parses and runs. The inference rules fire, the
  batch mode compiles, and the variables reach `cl` — the whole file was
  written without an nmake to try it on, so this was the open question.
- `/experimental:c11atomics` is required, and is enough. Without it the build
  stops at `vcruntime_c11_stdatomic.h(12)` with C1189; with it, the atomics
  compile.
- `src/port/st_port.c` compiles clean. That is the Win32 half of the
  portability layer — `SRWLOCK`, `CONDITION_VARIABLE`, `FlsAlloc` — written
  from the documentation and never before compiled.
- `src/interp/interp.c` and `trace.c` compile, with two shadowed-declaration
  warnings (C4456, C4457) and nothing worse.
- C4333 on `ST_header_frame_size` was real and is fixed. `cl` was right that
  a 16-bit `st_oop` shifted right by 16 keeps nothing; it is wrong that the
  loss is a mistake, because zero is what "frame size not stated" means under
  `OM=bb`. Widened before the shift rather than silenced.
- `src/interp/prim.c` did not compile: `<dirent.h>` does not exist on
  Windows. It now goes through a shim, along with `<unistd.h>`, `pread`,
  `pwrite`, `ftruncate`, `fstat`, `ssize_t` and `off_t`.

**Checked, on Linux:**

- The object list in `Makefile.msvc` is complete. Exactly the set it names,
  plus `src/main.c`, compiles and links clean with gcc under both `OM=mt` and
  `OM=bb`. The list it named before this document was written produced 40
  undefined references.
- The `prim.c` shim is behaviour-identical to what it replaced. Both memories
  build warning-clean, all sixteen unit suites and all five imported-package
  suites pass at their recorded scores, and the file primitives were driven
  end to end from inside the image: a 2000-byte file written across four
  pages through `pwrite` is byte-for-byte what Smalltalk was asked to write
  when read back from outside; the directory listing finds it; the size
  primitive answers 2000. Where behaviour looked odd — `newFileNamed:` not
  truncating, `doCommand: 2` not shortening a file — the committed code
  before the shim does exactly the same thing, so it is 1983's business and
  not the shim's.
- The platform-specific claims above were read out of the source, not
  recalled: the `_WIN32` branches in `st_port.h`/`.c` and `profile.c`, the
  `/`-only path splitting, the `fopen` modes, the `#ifdef ST_OM_*` guard in
  every unit suite.

**Still not checked:**

- **Everything after `src/interp`.** `src/sched`, `src/gfx`, `src/compiler`,
  `src/boot`, `src/om` and `src/main.c` have not been through a Windows
  compiler. `src/boot/profile.c`'s `FindFirstFileA` walk is in that set.
- **The link, and the program.** No `st80.exe` has been produced, so
  `SDL3.lib`, `SDL_main.h`'s entry point, the `shell32.lib` question and
  every runtime claim in this document remain untested.
- The `prim.c` shim's Windows half specifically: `_open` with `_O_BINARY`,
  `_chsize_s`, `_filelengthi64`, and `ReadFile`/`WriteFile` through an
  `OVERLAPPED`. The POSIX half is exercised by the whole test suite; the
  Win32 half is exercised by nothing yet.
- The remaining MSVC-specific flags — `/MD`, `/Fd`,
  `/D_CRT_DECLARE_NONSTDC_NAMES=1` — are from the documented behaviour of
  `cl` rather than from a build log.

The next build will probably turn up something. If it does, that is the
document doing its job; please report it at
<https://github.com/blakemcbride/Smalltalk-2026>.

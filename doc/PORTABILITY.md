# Portability

Targets: Linux, macOS, Windows. C11 throughout.

## What the code does about it

**No C11 `<threads.h>`.** Apple ships no such header in any macOS SDK, and
macOS is a required target. `src/port/st_port.h` is a shim over pthreads on
POSIX and the Win32 API on Windows. On Windows it uses `SRWLOCK` and
`CONDITION_VARIABLE`, and **fiber**-local storage rather than thread-local:
`FlsAlloc` is the only Win32 flavour that takes a destructor callback, which
is what `pthread_key_create` provides on the other side.

**C11 `<stdatomic.h>` is used**, because it is in good shape everywhere
— gcc since 4.9, clang since 3.6 including Apple's, MSVC since Visual Studio
2022 17.5. It is still wrapped in `st_atomic.h` so that a future MSVC edge
can be handled by swapping in `_Interlocked*` without touching a call site.
Two self-imposed limits keep every operation lock-free on every platform:
only naturally-aligned int-, bool- and pointer-sized types, and never
`_Atomic` on a struct.

**Few POSIX-only calls in shipping code, and they are named.**
`open_memstream`, `getline`, `usleep` and `gettimeofday` appear nowhere under
`src/`. The tests needed two of them and use portable equivalents:
`st_test_strdup` in `tests/st_test.h`, and a `tmpfile()` capture in place of
`open_memstream`.

`strdup` is the exception, in `src/main.c`, `src/compiler/source.c` and
`src/boot/bootstrap.c`. This file claimed it appeared nowhere and had stopped
being true; the first MSVC build is what said so. MSVC declares and links it
under the POSIX spelling, so the Windows build asks for that with
`/D_CRT_DECLARE_NONSTDC_NAMES=1` and silences the deprecation with
`/D_CRT_NONSTDC_NO_WARNINGS`.

`src/net/st_socket.c` is POSIX on one side of `ST_WINDOWS` and Winsock on the
other, by design rather than exception: `poll` and `WSAPoll`, `pipe2` (or
`pipe` where `pipe2` is missing) and a loopback UDP socket for the wake
channel, `getaddrinfo` on both, `getrandom`/`getentropy`/`/dev/urandom` and
`BCryptGenRandom`, `SO_REUSEADDR` and `SO_EXCLUSIVEADDRUSE`. The Windows half
is compiled for and not yet run.

`<dirent.h>` and `<unistd.h>` were the other exception, unguarded in
`src/interp/prim.c`, where the 1983 File and FilePage primitives were written
straight onto POSIX — `open`, `close`, `fstat`, `pread`, `pwrite`,
`ftruncate`, `ssize_t`, `off_t`. They now go through a shim in that file with
a Win32 half: `_open` with `_O_BINARY` because MSVC's defaults to text mode
and these primitives carry image pages, `_filelengthi64`, `_chsize_s`, and
`ReadFile`/`WriteFile` through an `OVERLAPPED`, which is positional the way
`pread` is and the way seek-then-read is not.

**SDL3's main-thread rule is honoured by construction.** `SDL_PumpEvents`,
`SDL_WaitEvent`, `SDL_CreateRenderer` and `SDL_LockTexture` are all
documented main-thread-only, and on macOS "main thread" means the one that
entered `main()` — Cocoa's run loop is bound to it and cannot be moved.
`src/main.c` includes `<SDL3/SDL_main.h>` so SDL can stand that up, and
thread 0 is a dedicated pump that never executes Smalltalk. See
`doc/CONCURRENCY.md`.

**SDL3 is optional.** Without it `src/gfx/display.c` compiles to a headless
stub, so the suite runs anywhere.

## Build

| Platform | Command | |
|---|---|---|
| Linux, macOS | `make` (the 64-bit memory; `make OM=bb` for the trace harness) | [`macOS.md`](../macOS.md) |
| Windows | `nmake /f Makefile.msvc SDL3=<path>` | [`Windows.md`](../Windows.md) |

Windows needs Visual Studio 2022 **17.8** or later — C11 atomics arrived in
17.5 and the rest of the C11 support settled in 17.8. `clang-cl` also works
and is what most portable C projects reach for on Windows, since it brings
the full gcc/clang atomics.

## What is verified, and what is not

Verified here, on Fedora 44 with gcc 16.1.1:

- Both object memories build clean under `-Wall -Wextra -Wpedantic -std=c11`.
- All suites pass under ASAN + UBSAN and under TSAN, in both memories.
- No POSIX-only calls remain in shipping code.

**Windows: verified, and it turned up plenty.** `Makefile.msvc` and the Win32
half of the portability shim now compile, link and run under MSVC 14.50 —
`st80.exe` bootstraps an image and opens its desktop. Getting there took an
object list eight files stale, `/experimental:c11atomics`, a `<dirent.h>` the
file primitives assumed, five `_Atomic` qualifiers discarded silently on the
way to `free`, a `uint16_t` that truncated a word count, path splitting that
knew only `/`, and a parse-time `!ERROR` that refused to let a machine clean
its own build tree. [`Windows.md`](../Windows.md) lists what is confirmed and
what is still untested — chiefly `nmake test`.

**macOS: still not verified.** No Mac was available. The Darwin branches are
four: `-pthread` dropped, Apple's one-argument `pthread_setname_np`, no
`sched_getaffinity`, and the benchmark's core pinning compiled out. All four
were exercised on Linux with `__linux__` undefined, which is the same
compiler taking the same branches and not the same compiler.
[`macOS.md`](../macOS.md) says exactly what that does and does not prove.

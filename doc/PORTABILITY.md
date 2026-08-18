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

**No POSIX-only calls in shipping code.** `strdup`, `open_memstream`,
`getline`, `usleep` and `gettimeofday` appear nowhere under `src/`. The
tests needed two of them and use portable equivalents: `st_test_strdup` in
`tests/st_test.h`, and a `tmpfile()` capture in place of `open_memstream`.

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

| Platform | Command |
|---|---|
| Linux, macOS | `make` (the 64-bit memory; `make OM=bb` for the trace harness) |
| Windows | `nmake /f Makefile.msvc` |

Windows needs Visual Studio 2022 **17.8** or later — C11 atomics arrived in
17.5 and the rest of the C11 support settled in 17.8. `clang-cl` also works
and is what most portable C projects reach for on Windows, since it brings
the full gcc/clang atomics.

## What is verified, and what is not

Verified here, on Fedora 44 with gcc 16.1.1:

- Both object memories build clean under `-Wall -Wextra -Wpedantic -std=c11`.
- All suites pass under ASAN + UBSAN and under TSAN, in both memories.
- No POSIX-only calls remain in shipping code.

**Not yet verified: an actual macOS or Windows build.** Neither machine was
available. `Makefile.msvc` and the Win32 half of the portability shim are
written from the documented APIs and have not been compiled. Treat Phase 6
as complete in intent and unconfirmed in fact until someone runs it on those
platforms; the first build on each will almost certainly turn up something.

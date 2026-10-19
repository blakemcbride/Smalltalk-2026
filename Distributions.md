# Binary distributions

A binary distribution is a directory someone can unpack and run without
building anything: the binary, a ready image, the libraries their machine is
unlikely to have, the manual, the licence, and a `README.md` that says what
must be installed and how. Each platform builds its own, on a machine of that
platform.

| Platform | Command | Result |
|---|---|---|
| Linux | `make dist-container` (recommended) or `make dist` | `distributions/linux-<arch>/` and `.tar.gz` |
| macOS | `make dist` | `distributions/macos-<arch>/` and `.tar.gz` |
| Windows | `nmake /f Makefile.msvc dist SDL3=C:\SDL3-x.y.z` | `distributions\windows-x86_64\` and `.zip` |

`<arch>` is `uname -m` on the building machine: `x86_64`, or `arm64` on Apple
Silicon. Each run removes the previous directory and archive first.
`distributions/` is in `.gitignore`.

Only the default system is distributed — `OM=mt` with a display. `make dist`
with `OM=bb`, `HEADLESS=1`, `TSAN=1` or `ASAN=1`, and `nmake ... dist` without
`SDL3=`, refuse and say so.

## What a distribution holds

| File | |
|---|---|
| `st2026` (`st2026.exe`) | the binary, linked for the distribution — see below |
| `lib/` (Windows: `SDL3.dll` beside the binary) | the bundled libraries |
| `st2026.image`, `st2026.image.changes` | bootstrapped from `profiles/st2026.profile` by the binary being shipped |
| `README.md` | from `tools/dist/README-<platform>.md` |
| `smalltalk-2026-manual.pdf` | copied from `manual/` when it has been built |
| `LICENSE`, `LICENSING.md` | from the top of the tree and `doc/` |

The image carries every method's source in itself, so a distribution needs
nothing else from the tree: not `sources/`, `lib/`, `pharo/` nor the profiles.

## Where it is made

| | |
|---|---|
| `Makefile`, target `dist` | links the binary again into the distribution directory, then runs `tools/dist.sh` |
| `tools/dist.sh` | bundles the libraries, bootstraps the image, fills in the README, copies the rest, packs the `.tar.gz` |
| `tools/dist/README-linux.md`, `README-macos.md`, `README-windows.md` | the READMEs, as templates |
| `Makefile.msvc`, target `dist` | all of the above for Windows, in nmake |

The Linux and macOS templates take fields that `tools/dist.sh` fills in from
the build: `@VERSION@`, `@ARCH@`, `@DIR@`, `@GLIBC@` (Linux), `@MACOS@`
(macOS), and a line `@CONFIG@` that becomes the binary's `-version` report,
without its CPU count. The Windows README has no fields and is copied as it
is.

## Linux

The binary is linked again with a run path of `$ORIGIN/lib`, as `DT_RPATH`
(`-Wl,--disable-new-dtags`) and not `RUNPATH`. Only `DT_RPATH` is searched
for a library's own dependencies, and libodbc's libltdl is one. The run path
is named before `$(LIBS)`, and `--disable-new-dtags` again after it,
because an SDL3 in `/usr/local` brings a run path and `--enable-new-dtags`
of its own through pkg-config: the first run path named is searched first,
and the last dtags flag wins.

Bundled: `libSDL3.so.0`, `libodbc.so.2`, `libltdl.so.7`, copied from where
`ldd` resolves them. SDL3, because Ubuntu 24.04 and Debian 12 have no
package for it. unixODBC, because the binary links libodbc whether or not
anyone uses a database, and a machine without it would refuse to start the
program. After copying, `tools/dist.sh` checks with `ldd` that all three
resolve inside `lib/`, and stops if one does not.

Left to the system, and named in the README with the command that installs
them on each distribution: glibc, OpenSSL 3 and zlib. Every desktop
distribution has them, and a private OpenSSL would not find the system's
certificates. ODBC drivers are the user's own; the bundled unixODBC reads
`/etc/odbcinst.ini`, where the distributions' driver packages register
themselves.

**The glibc floor is the building machine's.** `tools/dist.sh` takes the
newest `GLIBC_` symbol version that the binary and the bundled libraries
ask for, and the README states it as the requirement. Built on Fedora 44,
that floor is 2.43 — set by Fedora's SDL3, not by `st2026`, which asks for
2.38 — and so the distribution does not run on Ubuntu 24.04 (2.39) or
Debian 13 (2.41).

### Built on Ubuntu 24.04: `make dist-container`

`make dist-container` makes the same distribution in a Docker container of
Ubuntu 24.04, so its floor is what that system's toolchain asks for —
glibc 2.38 — and it runs on Ubuntu 24.04 and later, Debian 13, and every
current Fedora. Unpacked into bare `ubuntu:24.04` and `debian:13`
containers, with nothing installed, it starts, loads its image, and draws
the same desktop it draws on Fedora 44. It needs only Docker on the host — not
SDL3 or anything else the build wants — and `DOCKER=podman` uses podman
instead.

| | |
|---|---|
| `tools/dist/linux/Dockerfile` | Ubuntu 24.04 with the build tools, OpenSSL and unixODBC headers, and SDL3 built from its release tag (`SDL3_VERSION`, default 3.4.18) |
| `tools/dist-container.sh` | builds that image, copies the tree into a container without `build/`, `distributions/`, `oracle/`, `.git` or anything built, runs `make dist` there, and copies the result back |

The result replaces `distributions/linux-<arch>/` and its `.tar.gz`, owned
by the caller. Docker keeps the image, so only the first run spends its
minutes building SDL3; a new `SDL3_VERSION` in the Dockerfile builds it
again. The SDL3 made there links only libc and libm and loads X11, Wayland
and the sound systems with `dlopen`, as Ubuntu's own packages of it would.

Releases should use `make dist-container`. `make dist` on Linux is for a
distribution meant only for machines as new as the one that built it.

## macOS

The binary is linked again with an `LC_RPATH` of `@executable_path/lib`.
Every library it loads from outside `/usr/lib` and `/System` — Homebrew's
SDL3, and its OpenSSL when the build found one — is copied into `lib/`,
following each library's own dependencies too. Each copy is renamed to
`@rpath/<name>`, every reference to it in the binary and in the other
copies is rewritten to that name, and then all of them are signed again
ad hoc (`codesign --force --sign -`), since `install_name_tool` breaks the
signature and Apple Silicon will not run an unsigned binary. iODBC is part
of macOS and is not bundled.

The README's macOS floor is the building machine's `sw_vers
-productVersion`, and its architecture is the building machine's. An
`arm64` distribution does not run on an Intel Mac; an `x86_64` one runs on
Apple Silicon under Rosetta 2.

The binary is not notarised, so a downloaded copy is quarantined and
refused. The README gives the `xattr -dr com.apple.quarantine` that clears
it.

## Windows

`nmake /f Makefile.msvc dist` builds `st2026.exe` if it needs to, then
copies it and `SDL3.dll` into the distribution, bootstraps the image with
the copy, copies the README and the rest, and packs the folder with
PowerShell's `Compress-Archive`.

`SDL3.dll` is taken from `$(SDL3)\lib\x64\SDL3.dll`, the layout of SDL's
`-VC.zip` development package. For another layout, name it:

```bat
nmake /f Makefile.msvc dist SDL3=C:\vcpkg\installed\x64-windows SDL3LIB=C:\vcpkg\installed\x64-windows\lib\SDL3.lib SDL3DLL=C:\vcpkg\installed\x64-windows\bin\SDL3.dll
```

The binary is built `/MD` and needs Microsoft's C runtime,
`VCRUNTIME140.dll`. That is not bundled; the README sends the user to the
Visual C++ Redistributable, with the download link and the `winget`
command. The binary is not signed, so the README also covers SmartScreen
and unblocking a downloaded zip.

## Checking a distribution

Unpack the archive somewhere outside the tree and run, from inside it:

```sh
./st2026 -version
./st2026 -census st2026.image
./st2026 -run st2026.image
```

On Linux, `ldd ./st2026` should show `libSDL3`, `libodbc` and `libltdl`
inside `lib/`; on macOS, `otool -L ./st2026` should show `@rpath/` for
everything outside `/usr/lib` and `/System`.

Best of all is a machine that has never built the tree: one with no SDL3,
no unixODBC and no Homebrew, at the oldest system version the README claims.

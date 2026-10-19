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
| Windows | `nmake /f Makefile.msvc dist SDL3=%USERPROFILE%\SDL3-x.y.z` | `distributions\windows-x86_64\` and `.zip` |

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
SDL3, and its OpenSSL and unixODBC when the build found them — is copied
into `lib/`,
following each library's own dependencies too. Each copy is renamed to
`@rpath/<name>`, every reference to it in the binary and in the other
copies is rewritten to that name, and then all of them are signed again
ad hoc (`codesign --force --sign -`), since `install_name_tool` breaks the
signature and Apple Silicon will not run an unsigned binary. Every
run path but `@executable_path/lib` is deleted from the binary, so the
`-rpath /opt/homebrew/lib` that pkg-config adds cannot send it to the
user's own Homebrew.

Homebrew's OpenSSL is keg-only, so a plain `make dist` finds no OpenSSL
and builds without `https`; and without unixODBC it builds without the
database. The release recipe below says how to have both.

The README's macOS floor is the building machine's `sw_vers
-productVersion`, and its architecture is the building machine's. An
`arm64` distribution does not run on an Intel Mac; an `x86_64` one runs on
Apple Silicon under Rosetta 2. The floor is also the highest `minos` of anything bundled:
Homebrew builds its bottles for the macOS they are installed on, so a
Homebrew OpenSSL on macOS 27 makes the distribution macOS 27 or later, and
a lower `MACOSX_DEPLOYMENT_TARGET` for `st2026` cannot lower it.

The binary is not notarised, so a downloaded copy is quarantined and
refused. The README gives the `xattr -dr com.apple.quarantine` that clears
it.

## Windows

`nmake /f Makefile.msvc dist` builds `st2026.exe` if it needs to, then
copies it, `SDL3.dll` and OpenSSL's `libssl-3-x64.dll` and
`libcrypto-3-x64.dll` into the distribution, bootstraps the image with
the copy, copies the README and the rest, and packs the folder with the
`tar.exe` that Windows 10 and 11 ship (`tar -a`, which writes a zip for a
`.zip` name). Not Windows PowerShell's `Compress-Archive`: it stores the
entries as `windows-x86_64\LICENSE`, which unzips anywhere but Windows as
flat files with backslashes in their names.

The files in the zip have CRLF line endings when git checked the tree out
with `core.autocrlf=true`, and so does the source in the image. The readers
treat CR, LF and CRLF alike (`Windows.md`, *Line endings*).

**ODBC** is Windows' own: `sql.h` is in the SDK and `odbc32.dll` in every
Windows, so the build defines `ST_HAVE_ODBC` and links `odbc32.lib` unless
`NODB=1`, and nothing is bundled for it.

**TLS** wants an OpenSSL 3.2 or later development package, which Windows
does not have. `winget install ShiningLight.OpenSSL.Dev` installs one in
`C:\Program Files\OpenSSL-Win64`, where `Makefile.msvc` finds it without
being told; `OPENSSL=` names another, and `OPENSSLLIB=` its import
libraries' directory when that is not `lib\VC\x64\MD`. 3.2 because on
Windows `src/net/st_socket.c` verifies servers against the Windows
certificate store, through OpenSSL's `org.openssl.winstore:` URI; OpenSSL's
default paths there name a directory nobody fills. `dist` refuses a build
with no OpenSSL unless `NOTLS=1` says to ship without `https`. A build
without ODBC or TLS has its own build directory (`build\mt-nodb-msvc`,
`build\mt-notls-msvc`), as a headless one does.

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

## Making a release

A release is a tag on GitHub with the three archives attached, each built
on its own platform from the same commit. The manual's PDF is not in git
(`*.pdf` is ignored) and is built only where LaTeX is; it reaches the other
two archives by the last step below.

**1. Commit and push**, and pick the version. The binary reports
`ST_VERSION` from `src/main.c`; the tag is `v` and the version, as in
`v0.1.0`.

**2. The manual, on Linux:**

```sh
make -C manual
```

**3. Linux, on Linux:**

```sh
make dist-container
```

`distributions/linux-x86_64.tar.gz`, built on Ubuntu 24.04, with the
manual. `strings distributions/linux-x86_64/st2026 | grep 'GCC: ('` should
name Ubuntu.

**4. macOS, on the Mac:**

```sh
git pull
brew install sdl3 unixodbc openssl pkg-config
PKG_CONFIG_PATH=$(brew --prefix openssl)/lib/pkgconfig make dist
distributions/macos-arm64/st2026 -version
```

`-version` must say `database : ODBC` and `TLS : OpenSSL`; `none` means
the build did not find one, and the distribution goes out without it. Copy
`distributions/macos-arm64.tar.gz` to the Linux machine's `distributions/`.

**5. Windows, on the Windows machine**, in an *x64 Native Tools Command
Prompt*:

```bat
git pull
curl.exe -LO https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip
tar -xf SDL3-devel-3.4.18-VC.zip -C %USERPROFILE%
winget install ShiningLight.OpenSSL.Dev
nmake /f Makefile.msvc dist SDL3=%USERPROFILE%\SDL3-3.4.18
distributions\windows-x86_64\st2026.exe -version
```

`-version` must say `database : ODBC` and `TLS : OpenSSL`. The SDL3
download and the OpenSSL install are once per machine. SDL3 goes into the
user folder because an ordinary prompt cannot create one at the top of
`C:\`; keep its version the same as the Dockerfile's `SDL3_VERSION`.
`winget` asks for elevation itself.
Copy `distributions\windows-x86_64.zip` to the
Linux machine's `distributions/`.

**6. The manual into the macOS and Windows archives**, on Linux:

```sh
cd distributions
tar -xzf macos-arm64.tar.gz
cp ../manual/smalltalk-2026-manual.pdf macos-arm64/
tar --owner=0 --group=0 --numeric-owner -czf macos-arm64.tar.gz macos-arm64
unzip -q windows-x86_64.zip
cp ../manual/smalltalk-2026-manual.pdf windows-x86_64/
rm windows-x86_64.zip && zip -qr windows-x86_64.zip windows-x86_64
cd ..
```

**7. Check each archive** as the section above says, on a machine that has
never built the tree when there is one.

**8. Publish:**

```sh
gh release create v0.1.0 --target main --title "Smalltalk-2026 0.1.0" \
    --notes-file notes.md \
    distributions/linux-x86_64.tar.gz \
    distributions/macos-arm64.tar.gz \
    distributions/windows-x86_64.zip
```

`--target` takes a branch or a full commit hash, not a short one. To
replace an archive on a release that exists, `gh release upload v0.1.0
<file> --clobber`; to take one down, `gh release delete-asset v0.1.0
<name>`; to change the notes, `gh release edit v0.1.0 --notes-file
notes.md`. The notes should carry, per platform, the archive's name and
what it runs on: the glibc floor from the Linux README, the macOS floor and
processor from the macOS one, and the Visual C++ Redistributable for
Windows.

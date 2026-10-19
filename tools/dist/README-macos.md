# Smalltalk-2026 @VERSION@ for macOS (@ARCH@)

A Smalltalk-80 that runs on every core of the machine at once.
Source, documentation and issues: <https://github.com/blakemcbride/Smalltalk-2026>

## What is in this directory

| File | What it is |
|---|---|
| `st2026` | the virtual machine |
| `st2026.image`, `st2026.image.changes` | a ready-to-run image: the class library, its source, and the desktop |
| `lib/` | SDL3 (and OpenSSL, when this build has it), bundled so you need not install them |
| `smalltalk-2026-manual.pdf` | the manual |
| `LICENSE`, `LICENSING.md` | the licence, and the provenance of the parts that are other people's |

This build reports:

```
@CONFIG@
```

## Requirements

### 1. macOS @MACOS@ or later, on the right processor

This was built on macOS @MACOS@ for **@ARCH@** — `arm64` is Apple Silicon
(M1 and later), `x86_64` is Intel. Check yours with:

```sh
sw_vers -productVersion     # must be @MACOS@ or later
uname -m                    # must print @ARCH@
```

An `x86_64` build also runs on Apple Silicon under Rosetta 2; macOS offers
to install Rosetta the first time, or `softwareupdate --install-rosetta`.
An `arm64` build does not run on an Intel Mac.

### 2. Nothing else to install

Everything the program needs that macOS lacks is in `lib/`. You do **not**
need Homebrew, Xcode or SDL3. ODBC (iODBC) is part of macOS.

### 3. Optional: an ODBC driver, for the Database package

To reach a particular database, install that database's ODBC driver for
macOS (PostgreSQL's psqlODBC, SQLite ODBC, and so on) and register it in
the driver manager's `odbcinst.ini`, as the driver's own instructions say.
`./st2026 -version` shows whether this build has database support at all.

## Install

Put the directory somewhere **you can write to** — the image records your
work beside itself — such as your home directory:

```sh
tar -xzf @DIR@.tar.gz -C ~
mv ~/@DIR@ ~/st2026
```

**Clear the download quarantine.** A file that came from a browser, mail or
AirDrop is quarantined, and macOS then refuses to run the unsigned program
("cannot be opened because the developer cannot be verified" or "is
damaged"). This removes the mark from the whole directory:

```sh
xattr -dr com.apple.quarantine ~/st2026
```

Then, to type `st2026` from anywhere, add it to your `PATH` and open a new
terminal:

```sh
echo 'export PATH="$HOME/st2026:$PATH"' >> ~/.zshrc
```

Keep `lib/` next to `st2026`; the program looks for its libraries there.

## Run

From Terminal:

```sh
cd ~/st2026
./st2026 -run st2026.image
```

To check the installation without opening a window:

```sh
./st2026 -version
```

There is no `.app`: the window appears in the Dock under the name `st2026`
with the generic icon. Menus are press-and-hold, and a new window is placed
by dragging out its rectangle. Saving from the desktop writes a new image in
the current directory. On a Retina display an explicit scale is often
sharper:

```sh
ST_DISPLAY_SCALE=2 ./st2026 -run st2026.image
```

`./st2026 -help` lists every option and variable. The manual is
`smalltalk-2026-manual.pdf`.

## If it does not start

| Message | Cause and fix |
|---|---|
| "cannot be opened because the developer cannot be verified", or "is damaged" | run the `xattr` line above |
| `Bad CPU type in executable` | an `arm64` build on an Intel Mac (requirement 1) |
| `Library not loaded: @rpath/...` | `lib/` is not beside `st2026` — keep the directory whole |
| `built for macOS ... which is newer than running OS` | macOS is older than requirement 1 |

## Uninstall

Delete the directory, and the `PATH` line if you added one.

# Smalltalk-2026 @VERSION@ for macOS (@ARCH@)

A Smalltalk-80 that runs on every core of the machine at once.
Source, documentation and issues: <https://github.com/blakemcbride/Smalltalk-2026>

## What is in this directory

| File | What it is |
|---|---|
| `st2026` | the virtual machine |
| `st2026.image`, `st2026.image.changes` | a ready-to-run image: the class library, its source, and the desktop |
| `lib/` | the libraries macOS lacks — SDL3, and OpenSSL and unixODBC when this build has them — bundled so you need not install them |
| `smalltalk-2026-manual.pdf` | the manual |
| `LICENSE`, `LICENSING.md`, `licenses/` | the licence; what in this directory is other people's and under what terms; and their licence texts, with `licenses/BUNDLED.md` listing the bundled libraries |

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
need Homebrew, Xcode or SDL3.

### 3. Optional: an ODBC driver, for the Database package

@ODBC unixodbc@
The ODBC driver manager is **unixODBC**, bundled in `lib/` — not the iODBC
that macOS driver installers register with, so a driver has to be shown to
it. Install the database's ODBC driver for macOS (PostgreSQL's psqlODBC,
SQLite ODBC, and so on), and then either name the driver's library in the
connection string, which needs no registration at all:

```
Driver=/path/to/the/driver.dylib;Database=...
```

or tell unixODBC where the driver is registered. An installer for macOS
writes `/Library/ODBC/odbcinst.ini`, and its data sources to
`/Library/ODBC/odbc.ini`, in the format unixODBC reads, so setting this
before running `st2026` makes them visible:

```sh
export ODBCSYSINI=/Library/ODBC
```

Without `ODBCSYSINI`, unixODBC looks for drivers in `@ODBCINI@`, where the
machine that built this kept them — on most others, a file that does not
exist.
@ODBC iodbc@
The ODBC driver manager is macOS's own iODBC. Install the database's ODBC
driver for macOS (PostgreSQL's psqlODBC, SQLite ODBC, and so on) and
register it in `/Library/ODBC/odbcinst.ini`, as the driver's own
instructions say.
@ODBC none@
This build has no database support: `./st2026 -version` says
`database : none (NODB)`.
@ODBC end@

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

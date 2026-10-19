# Smalltalk-2026 @VERSION@ for Linux (@ARCH@)

A Smalltalk-80 that runs on every core of the machine at once.
Source, documentation and issues: <https://github.com/blakemcbride/Smalltalk-2026>

## What is in this directory

| File | What it is |
|---|---|
| `st2026` | the virtual machine |
| `st2026.image`, `st2026.image.changes` | a ready-to-run image: the class library, its source, and the desktop |
| `lib/` | SDL3 and unixODBC, bundled so you need not install them |
| `smalltalk-2026-manual.pdf` | the manual |
| `LICENSE`, `LICENSING.md` | the licence, and the provenance of the parts that are other people's |

This build reports:

```
@CONFIG@
```

## Requirements

You need all of the following. Most desktop Linux systems already have them.

### 1. A 64-bit Intel/AMD (@ARCH@) Linux with glibc @GLIBC@ or later

Check with:

```sh
uname -m          # must print @ARCH@
ldd --version     # the first line ends in the glibc version
```

For reference: RHEL/Rocky/Alma 9 have 2.34, Debian 12 has 2.36, Ubuntu
24.04 has 2.39 and Debian 13 has 2.41. If yours is older than @GLIBC@,
this build will not run there; build from source instead (see the project
page).

### 2. OpenSSL 3 and zlib

`https` needs OpenSSL, and the program will not start without it. Install
them with the line for your distribution (it does nothing if they are
already there):

```sh
sudo dnf install openssl-libs zlib                  # Fedora, RHEL, Rocky, Alma
sudo apt install libssl3t64 zlib1g                  # Ubuntu 24.04 and later
sudo apt install libssl3 zlib1g                     # Debian 13
sudo pacman -S openssl zlib                         # Arch, Manjaro
sudo zypper install libopenssl3 libz1               # openSUSE
```

### 3. A graphical desktop (X11 or Wayland)

`st2026 -run` opens a window, so it must be started from a desktop session.
Without one, `-serve` (a server, no window) still works.

### 4. Optional: an ODBC driver, for the Database package

unixODBC itself is bundled. To reach a particular database, install that
database's ODBC driver; it registers itself in `/etc/odbcinst.ini`, which
the bundled unixODBC reads. For example:

```sh
sudo dnf install postgresql-odbc sqliteodbc          # Fedora
sudo apt install odbc-postgresql libsqliteodbc       # Debian, Ubuntu
```

Nothing else is needed. SDL3 does not have to be installed.

## Install

Put the directory somewhere **you can write to** — the image records your
work beside itself — such as your home directory:

```sh
tar -xzf @DIR@.tar.gz -C ~
mv ~/@DIR@ ~/st2026
```

Then, to type `st2026` from anywhere, add it to your `PATH` (in `~/.bashrc`
or your shell's equivalent) and open a new terminal:

```sh
echo 'export PATH="$HOME/st2026:$PATH"' >> ~/.bashrc
```

Keep `lib/` next to `st2026`; the program looks for its libraries there.

## Run

```sh
cd ~/st2026
./st2026 -run st2026.image
```

To check the installation without opening a window:

```sh
./st2026 -version
```

Menus are press-and-hold, and a new window is placed by dragging out its
rectangle. Saving from the desktop writes a new image in the current
directory. The display is set by environment variables:

```sh
ST_DISPLAY_THEME=dark ST_DISPLAY_SCALE=2 ./st2026 -run st2026.image
```

`./st2026 -help` lists every option and variable. The manual is
`smalltalk-2026-manual.pdf`.

## If it does not start

| Message | Cause and fix |
|---|---|
| `version 'GLIBC_2.xx' not found` | the system is older than requirement 1; build from source |
| `libssl.so.3` or `libz.so.1: cannot open shared object file` | install requirement 2 |
| `libSDL3.so.0: cannot open shared object file` | `lib/` is not beside `st2026` — keep the directory whole |
| no window, or `could not open display` | not running in a desktop session (requirement 3) |

## Uninstall

Delete the directory, and the `PATH` line if you added one.

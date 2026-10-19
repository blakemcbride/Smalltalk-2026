# Smalltalk-2026 for Windows (x64)

A Smalltalk-80 that runs on every core of the machine at once.
Source, documentation and issues: <https://github.com/blakemcbride/Smalltalk-2026>

## What is in this folder

| File | What it is |
|---|---|
| `st2026.exe` | the virtual machine |
| `SDL3.dll` | the graphics library, bundled so you need not install it |
| `st2026.image`, `st2026.image.changes` | a ready-to-run image: the class library, its source, and the desktop |
| `smalltalk-2026-manual.pdf` | the manual |
| `LICENSE`, `LICENSING.md` | the licence, and the provenance of the parts that are other people's |

## Requirements

### 1. 64-bit Windows 10 or Windows 11

Check in **Settings → System → About**: *System type* must say
"64-bit operating system, x64-based processor". (Windows 11 on ARM runs it
too, through its built-in x64 emulation.)

### 2. The Microsoft Visual C++ Redistributable (x64)

`st2026.exe` needs Microsoft's C runtime, `VCRUNTIME140.dll`. Many machines
already have it, because other programs install it. If yours does not,
`st2026.exe` stops with *"The code execution cannot proceed because
VCRUNTIME140.dll was not found"*. Install it either way:

- download and run <https://aka.ms/vs/17/release/vc_redist.x64.exe>, or
- in a Command Prompt: `winget install Microsoft.VCRedist.2015+.x64`

Running the installer when it is already present does no harm.

### 3. Optional: an ODBC driver, for the Database package

Windows has ODBC built in. To reach a particular database, install that
database's 64-bit ODBC driver (PostgreSQL's psqlODBC, SQLite ODBC, and so
on) and, if you use a DSN, define it in **ODBC Data Sources (64-bit)**.

Nothing else is needed. SDL3 does not have to be installed separately.

## Install

1. Unzip `windows-x86_64.zip` to a folder **you can write to** — the image
   records your work beside itself — such as `C:\Users\<you>\st2026`.
   Not `C:\Program Files`, which ordinary programs cannot write to.
2. If Windows marked the zip as downloaded, unblock it *before* unzipping:
   right-click the zip → **Properties** → tick **Unblock** → **OK**.
3. Keep `SDL3.dll` in the same folder as `st2026.exe`.

## Run

Open a Command Prompt in the folder and run:

```bat
cd %USERPROFILE%\st2026
st2026.exe -run st2026.image
```

To check the installation without opening a window:

```bat
st2026.exe -version
```

If SmartScreen says *"Windows protected your PC"*, choose **More info →
Run anyway**; the program is not signed.

Menus are press-and-hold, and a new window is placed by dragging out its
rectangle. Saving from the desktop writes a new image in the current
folder. The display is set by environment variables:

```bat
set ST_DISPLAY_THEME=dark
set ST_DISPLAY_SCALE=2
st2026.exe -run st2026.image
```

`st2026.exe -help` lists every option and variable. The manual is
`smalltalk-2026-manual.pdf`.

## If it does not start

| Message | Cause and fix |
|---|---|
| `VCRUNTIME140.dll was not found` | install requirement 2 |
| `SDL3.dll was not found` | `SDL3.dll` is not beside `st2026.exe` |
| `0xc000007b` | a 32-bit `SDL3.dll` replaced the bundled one — unzip again |

## Uninstall

Delete the folder.

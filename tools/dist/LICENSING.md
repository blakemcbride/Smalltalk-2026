# Licensing of this distribution

Most of what is in this directory is Smalltalk-2026's own, under the
BSD-2-Clause licence in `LICENSE`. Some of it is other people's, under their
own terms, and their licences are in `licenses/`.

| What | Whose | Licence | Its text |
|---|---|---|---|
| `st2026` (`st2026.exe`), `smalltalk-2026-manual.pdf` | Smalltalk-2026 | BSD-2-Clause | `LICENSE` |
| the text face `st2026` draws with — a rasterisation of Inter at 18 pixels, compiled into the binary | The Inter Project Authors | SIL Open Font License 1.1 | `licenses/LICENSE.font` |
| the class library in `st2026.image`, and the source it carries, where they were written for Smalltalk-2026 | Smalltalk-2026 | BSD-2-Clause | `LICENSE` |
| the Smalltalk-80 version 2 class library in the same image, and its source, from Mark Bush's transcription of the 1983 sources | Mark Bush | MIT | `licenses/LICENSE.smalltalk-80` |
| the libraries bundled beside the binary: `lib/` on Linux and macOS, the DLLs beside `st2026.exe` on Windows | their authors | each its own | `licenses/BUNDLED.md` |

## The bundled libraries

`licenses/BUNDLED.md` lists every library this distribution carries: the file,
the package it was taken from and its version, its licence, the directory
under `licenses/` that holds its licence text, and where its source is.

They are the libraries as their packagers built them, unmodified, and the
binary loads them at run time. Any of them can be replaced by another build
of the same library — put it in place of the copy here — and the GNU Lesser
General Public License, under which unixODBC and its libltdl are
distributed, asks exactly that of a program that uses them. The source of
each, at the version shipped, is where `licenses/BUNDLED.md` says.

## The source of Smalltalk-2026

<https://github.com/blakemcbride/Smalltalk-2026>. Its `doc/LICENSING.md`
says which parts of the source tree are other people's and under what
terms, including some that a distribution does not carry.

# Licensing

Three bodies of source live in this repository and they are not under the same
terms. Which directory a file is in tells you which apply.

| directory | origin | licence |
|---|---|---|
| `src/`, `tests/`, `lib/`, `bench/`, `doc/` | written for this project | BSD-2-Clause, © 2026 Blake McBride |
| `sources/` | Smalltalk-80 v2 sources, Mark Bush's transcription | MIT |
| `pharo/` | imported from the Pharo project | **MIT, with parts under Apache-2.0** |

## `sources/` is frozen

Nothing in it is ever edited. Every divergence from 1983 is a new file in `lib/`.
That is a licensing convenience — the transcription stays verbatim and attributable —
and, more importantly, it is what makes "how far have we drifted" a question with a
mechanical answer.

## `pharo/` — imported, attributed, and recorded

Pharo is distributed under the MIT License **with parts under the Apache License**;
its own `LICENSE` says so in its first line. Every imported file keeps whatever notice
it arrived with, and every package directory carries a `PROVENANCE.md` recording:

- the upstream repository and the exact commit,
- the licence as the upstream states it,
- the files taken, and any file deliberately **not** taken,
- **every local edit**, line by line.

The last of those is the one that matters. A package that has been quietly patched is
a package nobody can update, and the whole point of importing rather than rewriting is
that upstream keeps improving. If a change is needed, it goes in the provenance file
before it goes in the source.

## Copyright notices in imported files

Pharo's Tonel files do not carry per-file copyright headers; the notice lives in the
repository's `LICENSE`. Copying that convention is what "keeps its notice" means here,
so `pharo/LICENSE.pharo` holds the upstream text verbatim and each `PROVENANCE.md`
points at it. Adding a per-file header would be *changing* the notice, not preserving
it.

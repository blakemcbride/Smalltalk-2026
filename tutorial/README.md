# Smalltalk-2026: A Tutorial

A book-length introduction to the system and the language — 26 chapters and
5 appendices, in LaTeX, building to a PDF with `pdflatex` and nothing else.

```sh
make            # build smalltalk-2026-tutorial.pdf
make verify     # run every example in the book against a live image
make clean      # remove LaTeX intermediates
```

## What is in it

| Part | Chapters |
|---|---|
| I — Getting Started | what this is, building and running, ten minutes in the image |
| II — The Language | syntax, objects and messages, blocks, control flow, classes, the class side, exceptions, what is past the Blue Book |
| III — The Library | numbers, collections, streams, text, graphics |
| IV — Living in the Image | the desktop, the tools, keeping your work |
| V — Concurrency | the contract, processes, writing parallel Smalltalk |
| VI — Working on Disk | packages and profiles, tests and doctests, the command-line toolbox |
| VII — Under the Hood | how the virtual machine works |
| Reference | command line, selectors, differences from Pharo and from 1983, troubleshooting, further reading |

## The examples are checked

Every expression in the book whose answer is printed is also a doctest in
`examples/TutorialExamples.class.st`, and `make verify` runs all of them
against an image built from this tree:

```
st80: 253 doctests in 24 methods of 1 files: 251 passed, 0 wrong, ...
```

A manual whose examples were typed rather than run is wrong somewhere and
cannot tell you where. If the book and the system ever disagree, `make verify`
is the referee — and it has already been: five answers in the first draft were
wrong and the run found all five.

`-closures` is passed deliberately. A profile's `#dialect` governs the
*packages* it loads; the doit each doctest is compiled into is a separate
compilation that defaults to the Blue Book dialect. Without the flag
`[:x | x] class` answers `BlockContext` and the book looks wrong when it is
not.

## Editing it

`chapters/` holds one file per chapter, `preamble.tex` the shared setup.

Two helper scripts exist because the same two mistakes kept recurring:

- **`check.py`** — the gate. `\ct` is `\lstinline` and therefore verbatim, so
  a LaTeX escape inside it prints the backslash and a newline inside it is a
  hard error; neither is visible in the source and one of them does not fail
  the build. It also catches `\ctp|...|` (that macro takes braces) and `\ct`
  inside an `\item[...]` label (lstinline cannot live there).
- **`fix.py`** — the repair. Rewrites `\ct{...}` to `\ct|...|` and drops the
  escapes. Writing the escapes is the habit of anyone who has written LaTeX
  before, so this fixes them rather than asking the author not to have the
  habit.

Run `python3 check.py` before `make`; it fails loudly and names the line.

## Licence

BSD 2-Clause, as the rest of `doc/`. The figure is `doc/desktop.png` from this
repository.

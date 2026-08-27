# Smalltalk-2026: A Manual

A book-length introduction to the system and the language — 30 chapters and
5 appendices, in LaTeX, building to a PDF with `pdflatex` and nothing else.

```sh
make            # build smalltalk-2026-manual.pdf
make verify     # run every example in the book against a live image
make clean      # remove LaTeX intermediates
```

## What is in it

| Part | Chapters |
|---|---|
| I — Getting Started | what this is, building and running, ten minutes in the image |
| II — The Language | syntax, objects and messages, blocks, control flow, classes, the class side, exceptions, what is past the Blue Book |
| III — The Library | numbers, collections, streams, text, graphics, the database, JSON, serving HTTP, talking to language models |
| IV — Living in the Image | the desktop, the System Browser, the other tools, keeping your work |
| V — Concurrency | the contract, processes, writing parallel Smalltalk |
| VI — Working on Disk | packages and profiles, tests and doctests, the command-line toolbox |
| VII — Under the Hood | how the virtual machine works |
| Reference | command line, selectors, differences from Pharo and from 1983, troubleshooting, further reading |

## The examples are checked

Every expression in the book whose answer is printed is also a doctest in
`examples/ManualExamples.class.st`, and `make verify` runs all of them
against an image built from this tree:

```
st80: 389 doctests in 30 methods of 1 files: 389 passed, 0 wrong, 0 need something not here
```

A manual whose examples were typed rather than run is wrong somewhere and
cannot tell you where. If the book and the system ever disagree, `make verify`
is the referee — and it has already been: five answers in the first draft were
wrong and the run found all five.

The dialect comes from the profile. `#dialect` governs the *packages* a
profile loads and the doit each doctest is compiled into alike, so the book
is checked in the language it documents: `[:x | x] class` answers
`BlockClosure` under `profiles/st2026.profile`. Set `PROFILE` to
`profiles/bluebook.profile` and the 1983 answers are what the run demands.

## Editing it

`chapters/` holds one file per chapter, `preamble.tex` the shared setup.

Two helper scripts exist because the same mistakes kept recurring:

- **`check.py`** — the gate. `\ct` is `\lstinline` and therefore verbatim, so
  a LaTeX escape inside it prints the backslash and a newline inside it is a
  hard error; neither is visible in the source and one of them does not fail
  the build. It also catches `\ctp|...|` (that macro takes braces) and `\ct`
  inside an `\item[...]` label (lstinline cannot live there).

  `\ctp` has the opposite failure, and it is worse because it is silent: the
  argument is ordinary text, so a bare `~` sets as a non-breaking space and
  the tilde *disappears*, `\~{}` and `\^{}` set as raised accents instead of
  the ASCII characters, and `'` sets as a curly quote no Smalltalk string
  literal uses. Those are checked too. The `>>` and `--` ligatures are the
  same class of bug and are turned off for the typewriter family in
  `preamble.tex`, which is why the linter does not look for them.

  It also holds every line inside a listing to 92 columns. `breaklines`
  folds a longer one in the PDF, and the fold copies out of the PDF as a line
  break: one statement becomes two, and the first of them is a syntax error.
  The number is measured, not derived -- 92 sets, 93 folds.

  The rule behind all of it: **every character the book prints as code must
  be one the reader can type and the compiler accepts.** The Blue Book set
  `^` as an up-arrow because the Xerox keyboard had that glyph. This one does
  not, so this book sets a caret.
- **`fix.py`** — the repair. Rewrites `\ct{...}` to `\ct|...|` and drops the
  escapes. Writing the escapes is the habit of anyone who has written LaTeX
  before, so this fixes them rather than asking the author not to have the
  habit.

Run `python3 check.py` before `make`; it fails loudly and names the line.

## Licence

BSD 2-Clause, as the rest of `doc/`. The figure is `doc/desktop.png` from this
repository.

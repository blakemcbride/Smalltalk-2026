# Reading Pharo, measured

What happens when this system's compiler is pointed at Pharo's own source.
Nothing is vendored yet; this is measurement, taken in place.

- **Source**: `/home/blake/Backup/Pharo/src/pharo`, at commit `490f37c591`.
- **Licence**: MIT with parts under Apache-2.0. Copyright the Pharo Project and
  Inria (2008–2019), Viewpoints Research Institute (1996–2008), Apple Inc. (1996).
- **Nothing has been copied into this repository.** Vendoring is a separate decision
  and needs the per-package `PROVENANCE.md` the plan describes.

## The numbers

Taken with `st80 -syntax`, which compiles every method and throws the result away.

| | files | methods | compiled | failed |
|---|---|---|---|---|
| **All of Pharo** | 9,443 | 91,210 | **91,057** | 153 (0.17%) |
| **Kernel** | 94 | 1,670 | **1,670** | **0** |
| Collections-Abstract | 6 | 132 | 132 | 0 |
| Collections-Sequenceable | 20 | 641 | 641 | 0 |
| Collections-Unordered | 21 | 304 | 302 | 2 |
| Collections-Strings | 13 | 430 | 427 | 3 |
| AST-Core | 92 | 1,507 | 1,506 | 1 |

And `st80 -primitives` on Kernel: **93 distinct primitives, 48 implemented here,
45 to implement.** That is the finite checklist Phase F5 existed to produce.

Kernel went from 1,639 to **all of it** by fixing what the report named.

## What the 153 are

Ranked, because the order is the work order:

| count | what | note |
|---|---|---|
| 79 | `#( double sx; double shx; )` | a semicolon inside a literal array. Pharo takes it as a symbol; we stop |
| 21 | `whileTrue:` / `ifTrue:ifFalse:` given a non-literal block | real Smalltalk falls back to a message send; we insist on a literal |
| 14 | non-ASCII characters in source (`$¶`) | the lexer is byte-oriented |
| 10 | `[ :index || segment | … ]` | `||` — the argument bar and the temporaries bar with no space between |
| 29 | assorted | not yet grouped |

**The closure-analysis failures are gone** — 50 methods wearing three different
messages, all one cause. See below.

## What was fixed to get here

Three of these are dialect differences — places where post-1983 Smalltalk spent a
character 1983 had already used — and the compiler now switches on the dialect it
was already being told:

- **The underscore.** In 1983 it *is* the assignment arrow; `a _ b` is what every
  line of `sources/` says. Pharo spells assignment `:=` exclusively and spends the
  underscore on names. Getting this wrong read `simulate_vmMilliseconds:` as an
  assignment.
- **Binary selector length.** Two characters in Smalltalk-80, unbounded after.
  `Boolean>>==>` is in Pharo's Kernel. The rule that keeps `-2@-2` from reading as
  `-2 @- 2` is what the two-character limit was really protecting, and it is
  separate and still in force.
- **`<primitive: N error: ec>`.** The second argument is not a value: it names a
  *temporary* the VM fills in with why the primitive failed. This VM sets no error
  codes, so it stays nil — which is right, because every one of those bodies tests
  the code against a specific symbol and takes the general path otherwise.
- **Primitive numbers above 255.** Eight bits in the Blue Book header extension, so
  255 is the ceiling of the *format*, not of this implementation; Spur uses a
  different header and `SmallFloat64` declares 541–559. The number is recorded and
  not written, and the Smalltalk body is kept — the same thing as a primitive that
  always fails, which is what an unimplemented primitive already does.

### The closure analysis: decided by looking, not by trying

Whether `[cond]` is a real block or the inlined receiver of `whileTrue:` cannot be
known until the selector *after* it has been read, and the block comes first. That
used to be settled by compiling it as a **real block**, looking at what came next,
and rewinding if the answer was `whileTrue:`.

That is sound for tokens and bytecodes, which `rewind_to` gives back. It is unsound
for the closure analysis, which it cannot: the speculative reading marks every
enclosing name the block touches as *captured* and records that its scope *needs*
them, and the inlined reading needs neither. Worse, the two passes disagree about
which reading happened — pass zero's conclusions describe the **final** one — so when
pass one re-ran the same speculative parse it could not resolve the names it was
about to throw away, and failed a method it would otherwise have compiled.

Three fixes were tried against the symptoms and all three were wrong, each in an
instructive way: giving back the needs alone leaves a variable marked shared with
nothing to share it through; giving back the capture flags as well fixes the first
case and breaks a second; giving back the *error* as well lets the parse continue
and fail later. They were symptoms of one thing — **the block was being parsed
twice, under two different readings.**

So it is not any more. The lexer scans to the matching bracket — it already knows
what a comment, a string and a `$]` are — and answers the selector after it. Nothing
in the compiler is touched, so there is nothing to give back. The speculative
machinery added while chasing the symptoms was removed once the cause was fixed.

`sources/` compiles 4,521 of 4,521 and trace2 stayed byte-identical, which is the
only evidence worth anything about a change to how every loop in the system is
recognised.

And one reporting defect, which cost an hour: the survey folded a failure message
from its **first apostrophe** to the end, so *"a shared name's vector is not in
scope"* displayed as `a shared name'...'`, and two unrelated errors both displayed
as `'...'`. It folds a matched pair now and keeps the tail. A report that hides the
message it is reporting is not a small problem.

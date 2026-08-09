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
| **All of Pharo** | 9,443 | 91,210 | **91,009** | 201 (0.22%) |
| **Kernel** | 94 | 1,670 | **1,669** | 1 (0.06%) |
| Collections-Abstract | 6 | 132 | 132 | 0 |
| Collections-Sequenceable | 20 | 641 | 641 | 0 |
| Collections-Unordered | 21 | 304 | 302 | 2 |
| Collections-Strings | 13 | 430 | 427 | 3 |
| AST-Core | 92 | 1,507 | 1,506 | 1 |

And `st80 -primitives` on Kernel: **93 distinct primitives, 48 implemented here,
45 to implement.** That is the finite checklist Phase F5 existed to produce.

Kernel went from 1,639 to 1,669 in one sitting, by fixing what the report named.

## What the 201 are

Ranked, because the order is the work order:

| count | what | note |
|---|---|---|
| 79 | `#( double sx; double shx; )` | a semicolon inside a literal array. Pharo takes it as a symbol; we stop |
| 50 | closure analysis: *"used in a block that did not capture it"*, *"a captured name is not in scope"*, *"a shared name's vector is not in scope"* | **one bug, three faces** — see below |
| 21 | `whileTrue:` / `ifTrue:ifFalse:` given a non-literal block | real Smalltalk falls back to a message send; we insist on a literal |
| 14 | non-ASCII characters in source (`$¶`) | the lexer is byte-oriented |
| 10 | `[ :index || segment | … ]` | `||` — the argument bar and the temporaries bar with no space between |
| 27 | assorted | not yet grouped |

### The closure-analysis bug, diagnosed but not fixed

Fifty methods, and one cause. Reproduction, which fails today:

```smalltalk
F >> minimal [
	[ [go] whileTrue:
		[ | t |
		  t := 1.
		  [ a isNil and: [t >= 2] ] whileTrue: [ a := nil ] ]
	] ensure: [ nil ]
]
```

A temporary declared in an **inlined** block, inside a **real** block, used from a
nested inlined block. `compile_inline_while` parses the `whileTrue:` receiver
speculatively as a real block, which takes a scope number and records that the
scope *needs* the variable — then rewinds. `mark`/`rewind_to` give back the scope
number but **not the need**, so the entry goes on naming a number that the next
real block to be parsed is given, and the emitting pass tries to copy a vector into
a block that never mentioned the variable.

Two fixes were tried and neither is right: restoring `need_count` wholesale loses
needs that legitimately survive a rewind (34 Kernel methods break), and filtering
by scope number against `block_seen` does the same. The needs recorded by an
abandoned parse are not simply the ones added after the mark, and working out what
they *are* wants a clear head rather than the end of a long session.

`sources/` compiles 4,521 of 4,521 and trace2 is byte-identical throughout, so the
bug is reachable only from the closure dialect.

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

And one reporting defect, which cost an hour: the survey folded a failure message
from its **first apostrophe** to the end, so *"a shared name's vector is not in
scope"* displayed as `a shared name'...'`, and two unrelated errors both displayed
as `'...'`. It folds a matched pair now and keeps the tail. A report that hides the
message it is reporting is not a small problem.

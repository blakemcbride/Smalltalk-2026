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
| **All of Pharo** | 9,443 | 91,210 | **91,199** | 11 (0.012%) |
| **Kernel** | 94 | 1,670 | **1,670** | **0** |
| Collections-Abstract | 6 | 132 | 132 | 0 |
| Collections-Sequenceable | 20 | 641 | 641 | 0 |
| Collections-Unordered | 21 | 304 | 302 | 2 |
| Collections-Strings | 13 | 430 | 427 | 3 |
| AST-Core | 92 | 1,507 | 1,506 | 1 |

And `st80 -doctests` runs Pharo's **own examples** against this image:

```
1,426 doctests in 4,790 methods of 106 files:
   418 passed, 35 wrong, 973 need something not here
```

Pharo documents a method by putting examples in its comment, in a form meant to be
read by machine — `"(#(10 20 30) indexOf: 20) >>> 2"`. There are about fifteen
hundred, they were written by the people who wrote the methods, and they say what a
method is *for* rather than what it happens to do. That makes them the cheapest
oracle this port will ever get: nobody has to write them, they cannot drift from the
code they sit in, and they answer the question that matters — not "does Pharo's
source parse here" but "does it **mean** here what it means there".

The three outcomes are told apart on purpose, because they are three different pieces
of news. *Passed* is 418 of Pharo's own examples working against an image with none
of Pharo's code in it. *Needs something not here* is a class or a selector still to
port — the work, and the number that should fall. ***Wrong* is the only one that is a
bug**: the method exists here and disagrees. Each doctest runs under a handler, so a
missing selector is never miscounted as a wrong answer.

Two of the 35 were found and fixed within the hour, and both were silent:

- **`#(1 5 10 -4)` was five elements** — 1, 5, 10, the symbol `#-`, and 4. A minus
  written against a number is ambiguous in code (`3-4` is a send) and *not* ambiguous
  inside `#( )`, where there are no sends. Nothing failed; the array was simply the
  wrong array, and `#(1 5 10 -4) min` answered 1.
- **`'hello' sorted` answered an Array of five Characters.** Our `Collection>>sorted`
  answers an Array by design — a sorted Set is not a Set — but a *sequenceable*
  collection has an obvious species, because order is what it is for. Pharo's doctest
  is what pointed out that the general rule had been applied one level too high.

And two it measured rather than fixed: `Float` prints six significant digits where
Pharo prints shortest-round-trip, and `2 raisedTo: 1/12` is off by 2·10⁻⁷ because
`ln` and `exp` fall back to the 1983 image's Taylor series — primitives 58 and 59 are
on the list below.

And `st80 -primitives` on Kernel: **115 distinct primitives, 66 implemented here,
49 to implement** — the finite checklist Phase F5 existed to produce, worked down
from 48/45. (The total grew because more of Pharo's Kernel now compiles, so more of
what it asks for is visible.)

### What was implemented, and what was refused

Eighteen went in: **58 `ln`** and **59 `exp`**, **132 `instVarsInclude:`**,
**135** the millisecond clock and **240** the UTC microsecond clock, **148 `clone`**,
**159 `hashMultiply`**, **168 `copyFrom:`**, **169 `~~`**, **170/171** Character to
and from its code point, **173/174** (`instVarAt:` renumbered), **230**
`relinquishProcessorForMicroseconds:`, and **163/164/183/184** — read-only and
pinned objects, which Spur keeps in the object header and this memory does not have.
Those four answer the truth here rather than what the caller hopes: nothing is
read-only and nothing is pinned, so asking answers false and *setting* either to
false succeeds. Setting one to **true fails**, which runs the image's own fallback —
where a decision about what to do instead actually belongs.

A primitive only helps once a method **declares** it, so `lib/` now declares the ones
worth reaching from a Blue Book image: `Float>>ln`, `Float>>exp`, `Object>>~~`,
`Object>>shallowCopy`, `Integer>>hashMultiply`. `ln` and `exp` are not cosmetic — the
1983 Taylor series stops at `MathApproximationEpsilon` and was wrong in float32's last
digit, and `(2 raisedTo: 1/12) = 1.0594630943592953` went from false to **true**.

**A collision worth naming, and since resolved by implementing it.** Pharo uses
primitive **249** for `Array>>elementsForwardIdentityTo:copyHash:`, a bulk `become:`.
This system had taken 249 for `ContextPart>>restartAndJump`. Loading that one Array
method would have called ours — a context restart where a `become:` was meant. Ours
moved to 251, and 249 now *is* one-way become here: `elementsForwardIdentityTo:` with
`Object>>becomeForward:` on top of it. Taking Pharo's number for Pharo's operation is
what lets ported source say it and mean it. Pharo also
uses 240, 242 and 254 in the range `doc/PLAN-PHARO.md` reserved for parallel
primitives, so that reservation needs revisiting before Phase H spends it.

**Refused, and why:**

| | |
|---|---|
| **20–33, and the twelve `LargeIntegers` plugin primitives** | **optimisations of code that already works.** `100 factorial`, `2 raisedTo: 100`, and 20-digit multiplication all answer correctly today through the 1983 Smalltalk implementations of `digitAdd:` and friends. A C bignum library would make them faster and would not make them more correct — worth doing, and worth doing as its own piece of work rather than smuggled in under a bug fix |
| **541–559 (`SmallFloat64`)** | Spur's immediate floats. Every Float here is boxed, so the class has no instances and the primitives have no receiver |
| **38/39 (`Float basicAt:`)** | Squeak means the two 32-bit halves of a **64-bit** float. This system's Float is the Blue Book's — **two words, single precision** — so the index means something else, and answering anything would be answering the wrong question |
| **100 (`perform:withArguments:inSuperclass:`)** | needs a class-directed send this VM has no entry point for. It is also a **number the two dialects disagree about**: 1983 uses 100 for `signal:atMilliseconds:` |
| **118, 188** (`tryPrimitive:`, `withArgs:executeMethod:`) | both build an activation from C for a method chosen at run time |
| **136, 242** (signal a semaphore at a time) | need the VM timer thread that `doc/PLAN-PHARO.md`'s Phase L already calls for under `Delay` |
| **167 (`yield`)** | belongs with Phase H, which rewrites the scheduler it would have to reach into |

**`Float` is single precision here**, and that is worth stating plainly because it is
not a bug: the Blue Book's Float is two 16-bit words, and `make_float` is faithful to
it. Pharo's is `BoxedFloat64`. Every float answer in this system is therefore correct
to about seven digits, not sixteen, and the printString shows six. Making Float
double-precision is a real and separable piece of work with consequences for the trace
oracle.

Kernel went from 1,639 to **all of it** by fixing what the report named.

## What is left: eleven methods, and three of them are correct refusals

| count | what | verdict |
|---|---|---|
| 5 | `{ 1 . ^2 }` — a return *statement* inside a dynamic array | not supported. It appears only in tests that inspect the AST, and the semantics are odd enough (build the array, then return out of the middle of it) that guessing at them is worse than saying no |
| 2 | `##smallUpdate` — Pharo's compile-time-value literal | not supported. `##x` means *the value bound to x when this was compiled*, and a bootstrap compiles before the bindings have values |
| 1 | `#x::` — a symbol ending in two colons | not supported; it appears in one test that expects it to raise |
| 2 | `$€`, `$→` | **correct refusal.** This memory's Character is the Blue Book's — a unique entry in a 256-entry `CharacterTable`, which is what makes `$a == $a` true. U+20AC has nowhere to go, and the message says so by code point |
| 1 | a method with more than 63 literals | **correct refusal.** Six bits in the Blue Book method header. Spur uses a different header; this is a ceiling of the format, like primitives above 255 |

Three of the eleven are this system correctly declining to pretend, and they are
reported by name and number rather than as a parse error.

## What it took to get from 201 to 11

Beyond the closure fix below, all of it found by reading the ranked list rather
than the grammar:

- **Everything inside `#( )` that is not a literal is a Symbol** — including the
  punctuation the grammar uses elsewhere. Pharo's graphics code writes
  `#( double sx; double shx; )` as a field descriptor and means six symbols, two
  of which are `#;`. Seventy-nine methods did that; brackets get in the same way,
  from source that meant to close the array earlier.
- **A radix number may have a fraction and an exponent.** `2r1.1` is 1.5 and
  `2r1.0e-10` is two to the minus tenth — the digits of both parts are in the
  radix, the exponent is decimal, and the power is of the radix. The Blue Book
  grammar always said so. Reading only the integer part left `2r1.1` as `2r1`
  followed by a **statement separator**, which is a wrong answer rather than an
  error anywhere a period could legally follow.
- **A character literal is one UTF-8 sequence, not one byte.** Reading the lead
  byte alone leaves the continuation bytes in the stream, where they are neither a
  token nor a legal anything. A byte above ASCII is also a *letter*, so `#яблоко`
  is a Symbol.
- **`[ :index || segment | … ]`** — the bar closing the arguments written hard
  against the bar opening the temporaries. The lexer, which cannot see the grammar,
  hands the pair over as one binary selector; only the parser knows the arguments
  have just ended, so that is where the two are split.
- **A conditional or a loop is inlined only when every arm is a literal block.**
  `ifTrue: [a] ifFalse: aBlock` is an ordinary message send. Refusing it was this
  compiler mistaking its own optimisation for a rule of the language — and the
  decision has to be made *before* anything is emitted, or finding out halfway
  leaves a jump and an arm with nowhere to put them.
- **A block argument shadowing a temporary hoisted out of an inlined block.** Pass
  zero jumps its visibility cursor to an absolute declaration index; pass one was
  advancing by one. After an inlined block put its temporaries back out of scope
  the two passes disagreed, and `[:dict | …]` resolved to a hoisted `dict` instead
  of its own argument. Silent, and a wrong variable rather than an error.
- **The byte-array buffer moved to the heap.** Pharo embeds whole fonts as
  byte-array literals — one method, one literal, over a quarter of a megabyte. The
  limit had to rise, and half a megabyte of automatic storage inside a
  recursive-descent parser is a stack overflow waiting for a deeply nested method.

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

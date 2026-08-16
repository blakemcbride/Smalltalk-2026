# From here to a 64-bit, multi-threaded Pharo

This continues `doc/PLAN-PHARO.md` from Phase J onward and **revises one row of its
decisions table**. That plan chose "keep MVC, add modern tools on top over time", which was
right for getting the object memory and the language done and is not sufficient for the
stated end goal. The revision is in *The decision that has to change*, below.

Everything numeric here was measured on 16 August 2026, not estimated.

---

## What is already true

| | |
|---|---|
| Object memory | 64-bit, threaded, reference-counted with a marking collector at a safepoint. TSAN-clean, ASAN-clean |
| Parallel bytecodes | On 8 physical cores: `mandelbrot` **7.75×**, `intervals` **4.19×**, `collections` **6.86×**, control kernel **7.68×** |
| Language | Closures, non-local return, `ensure:`/`ifCurtailed:`, exceptions, pragmas, traits (flattened), `{ }`, `#[ ]` |
| Object model | Weak references, ephemerons, `become:` and `becomeForward:`, immediates where they exist, class-format bits |
| Pharo source running | Announcements 43/43, Chronology 633/633, Collections 469/469, Collections-Weak 32/32 — **1177 of Pharo's own tests** |
| Oracle | Xerox `trace2` 611/0 and `trace3` 482/0, byte-exact, on every commit |
| The desktop | 1983 MVC, **one thread**. `do_run` never starts a worker pool |

Phases A–H and K of `PLAN-PHARO.md` are done. I is written and its gate is not formally met.
J, L and M are open.

## The size of the gap

At the commit we import from, Pharo's `src/` holds **7,256 classes, 128 traits, 591
packages**. This system holds 264 classes, of which **60 are Pharo's**. That is **0.8%**.

The number is less bad than it looks, because a large part of Pharo is not wanted:

- **Morphic, Bloc, Spec, Athens** — the UI. Single-threaded by construction. See below.
- **Opal, RB-AST, IRBuilder** — assume SistaV1 bytecodes and Spur literals. This system has a
  C compiler and the 1983 in-image compiler.
- **Iceberg, Metacello, Monticello** — replaced by the Tonel loader, which is done.
- **UFFI and plugin-heavy packages** — a separate problem, named below.
- **Fuel** — serialises Spur object formats.

What remains and is wanted is on the order of **1,500–2,500 classes**: the kernel, the
collections, streams, files, text, numbers, chronology, announcements, SUnit, STON, the
reflection layer, and the model half of the tools.

## What "a Pharo" can mean here

Pharo is six things, and they are not equally reachable.

| Layer | Reachable? | State |
|---|---|---|
| The **language** | Yes | Done |
| The **kernel objects** — `Object`, `Behavior`, `Class`, `CompiledMethod`, `Context`, `Slot`, `Pragma` | Yes, and it is the hardest part | Tier 3 of the ratchet, not started |
| The **libraries** | Yes, mechanically | 4 packages of ~100 wanted |
| The **tool models** — what a browser *knows*, what an inspector *computes* | Yes, once the kernel is Pharo's | Not started |
| The **UI framework** | **No, not by porting** | Must be written here |
| The **dev workflow** — packages, versioning | Already replaced | Tonel loader, profiles, the ratchet |

So the end state, stated so it can be checked:

> **A Smalltalk-80 that runs Pharo's kernel and libraries, is source-compatible with Pharo
> for everything that is not UI, executes bytecodes on every core of the machine, and
> carries its own tools — written here, multi-threaded by construction.**

That is "a multi-threaded Pharo" in every sense that can exist. It is worth being blunt
about why: **Morphic cannot be both ported and multi-threaded.** It is the largest single
piece of Pharo, it assumes one process owns the screen and the world, and making it
otherwise is the rewrite that the upstream project has publicly declined. Porting it would
also be the one part of this work that could not be justified by the thesis, because the
result would be a system that scales everywhere except where the user is looking.

## The decision that has to change

`PLAN-PHARO.md` says *"UI — keep MVC, confine it to one worker, add modern tools on top over
time."* The first half stays and is Phase J. The second half has to stop being "over time"
and become scheduled work, because it is the difference between this system and the stated
goal.

Two shapes for it. They are not exclusive and the order matters.

**(A) An in-image UI on the existing SDL3 + BitBlt layer.** 1,243 lines of graphics code
exist and Phase J's confinement rules apply unchanged. Keeps the thing that makes Smalltalk
worth using — the live image, direct manipulation, the debugger *in* the running system.
Cost: it is a UI toolkit written from scratch, and a year before it is pleasant.

**(B) A headless image with an external front-end over a socket.** The VM already runs
headless well; every test does. Weeks to something usable, inherently non-blocking because
the front-end is a separate process, and it brings editor and LSP integration with it.

**Recommendation: B first, then A** — and B is not throwaway. The protocol B needs
(evaluate, browse, inspect, step, list senders) is exactly the API an in-image UI needs, so
building it first **forces the tool models to be separated from the tool views**. That
separation is precisely what Pharo does not have, and is why Morphic cannot be threaded. Do
it in the order that makes the mistake impossible.

---

## The plan

### P1 — The desktop uses the cores *(Phase J, unchanged)*

The smallest change that makes the parallelism visible in the thing a person looks at, and
the design questions it forces are the same ones every later UI will ask.

- Thread 0 pumps SDL and nothing else; workers run Smalltalk.
- All `Display`-destined drawing confined to one designated UI worker, never a steal target.
- `Sensor`/`Controller>>controlLoop` pinned to that worker (primitive 244).
- A global `st_shutdown_requested` polled beside `WORKER_poll()` — today primitive 113 stops
  exactly one worker, which is a bug waiting for its first user.

**Gate:** the desktop runs on N workers; a Browser is usable; a `forkParallel:`-ed
computation visibly runs while the UI stays responsive; 30 minutes of scripted input under
TSAN, clean.

**Size:** 5 weeks. **Banks:** the first demonstration anyone can see in thirty seconds.

### P2 — The audit becomes a tool *(Phase L, brought forward)*

From P1 onward every imported package is shared mutable state, and the audit is what keeps
P1 true. It has to be mechanical or it will not be done.

- `st80 -audit <Class|Package>` — walk method dictionaries, decode bytecodes, report every
  literal-variable store with its class and selector. The compiler already knows these
  exactly: a store to a global or class variable is bytecode 129/130 with a
  `storeLiteralVariable` descriptor.
- Record each decision as a pragma (`<shared: #serialize>`) so it lives beside the code.
- The named hazards in `PLAN-PHARO.md`'s Phase L table get their rows in
  `doc/ParallelAudit.md`.

**Gate:** the audit runs over every loaded package and its output is empty or explained.
Any new package fails the build until its rows exist.

**Size:** 3 weeks. **Banks:** every later import is safe by construction rather than by hope.

### P3 — The kernel becomes Pharo's *(Phase M, Tier 2 then Tier 3)*

This is the phase that changes what the system *is*. Until `Object`, `Behavior`, `Class`,
`CompiledMethod` and `Context` are Pharo's, every package imported needs a shim in `lib/`,
and the shims are the tax.

Order by VM entanglement, exactly as the ratchet does now:

1. **Tier 2** — `Object`, `Boolean`, `Character`, `SmallInteger`, `Float`, `Fraction`,
   `Array`, `String`, `Symbol`, `Association`, `Point`, `Semaphore`, `Process`,
   `ProcessorScheduler`. Each requires the VM's field layout and primitive numbers to match.
2. **Collections-Sequenceable** — `OrderedCollection`, `SortedCollection`, `Interval`. Hit
   twice already as a blocker; `WeakOrderedCollection` needed it, and so does anything that
   assumes `arrayType`.
3. **Tier 3** — `Behavior`, `ClassDescription`, `Class`, `Metaclass`, `CompiledMethod`,
   `Context`, `BlockClosure`, `Slot`, `Pragma`, `Trait`. These *define* the contract the VM
   must satisfy, so they come last and each one is a VM change as much as a load.

**Gate per turn:** the class loads, its own SUnit suite passes, its audit rows exist, its
`PROVENANCE.md` records every local edit, and no profile's score falls.

**Size:** 12–18 months. This is the bulk of the work and it does not compress.

**Banks:** at the end of Tier 2, Pharo library code loads without shims. That is the point
at which importing a package becomes routine rather than a project.

### P4 — Tools, model first *(new)*

- **P4a — The protocol.** Evaluate, browse, senders/implementors, inspect, step, and the
  package/class/method model, over a socket. Written against the kernel, never against a
  view. **Gate:** a front-end can drive the image with no drawing code in the image at all.
- **P4b — A front-end.** Editor integration first, because it is the cheapest thing that
  makes the system usable daily.
- **P4c — The in-image UI**, on SDL3 + BitBlt, against the same protocol objects, drawing
  confined per P1. **Gate:** browse, edit, accept, debug, inspect, and a `forkParallel:`-ed
  computation running visibly while all of that stays responsive.

**Size:** P4a 6 weeks · P4b 6 weeks · P4c 9–12 months.

**Banks:** after P4b this is a system somebody can work in all day. That is the milestone
that decides whether the rest gets done.

### P5 — The libraries, continuously *(Phase M continued)*

Streams, Files, Text, Numbers, Zinc-Character-Encoding, STON, and the rest of the
1,500–2,500. Ratcheted exactly as now: import, run the package's own tests, record the
score in `tests/profiles.expected`, fail the build in both directions.

**Progress metric:** classes imported and passing, and the count of `lib/` shims, which
should *fall* once Tier 2 lands.

---

## Sizing, honestly

| | |
|---|---|
| P1 desktop on N workers | 5 weeks |
| P2 the audit tool | 3 weeks |
| P3 the kernel ratchet | 12–18 months |
| P4a+b protocol and front-end | 3 months |
| P4c in-image UI | 9–12 months |
| P5 libraries | continuous, years |

Bottom-up that is **roughly 2.5–3 years** to the stated end state for one developer, and
`PLAN-PHARO.md`'s own warning applies unchanged: projects of this shape run 1.7–2.5× their
bottom-up estimate, and this one contains no time for the thing that turns out to be wrong.
The first plan estimated A–K at 2–3 years and that has proven about right.

**Say 4–6 years part-time to the full end state, with something usable daily after P4b —
about 8 months in.**

## What will make this fail, named in advance

- **P3 Tier 3.** `CompiledMethod` and `Context` are the VM's contract. Changing them is not
  a load, it is an interpreter change with `trace2` on the other side of it. This is the
  phase most likely to sit ninety per cent done.
- **The UI is a toolkit.** P4c is a year of work that produces nothing measurable until it
  produces everything. Guard it by keeping P4b usable throughout, so there is always a way
  to work.
- **FFI.** Not on the critical path and not avoidable for ever — files, sockets and time
  already reach outside. Today those are primitives; at some point something wants a C
  library and there is no answer. Decide it deliberately rather than under pressure.
- **The audit is unbounded in principle.** Bound it in practice: audit `lib/` and `pharo/`
  completely, audit `sources/` only for what a loaded profile actually executes.
- **Measurement.** Four attempts at one cache line this month were reverted because the
  first three were measured wrongly or not at all. On a machine with other tenants, count
  cycles and alternate the binaries; a single wall-clock reading is worth what it cost.

## What this plan does not change

The constraints that have held all along, and hold here:

- `sources/` is never edited. Every divergence is a new file in `lib/` or `pharo/`.
- `oracle/` is never committed or redistributed.
- `bluebook.profile` boots and `trace2` stays byte-exact, through every phase above.
- Pharo is MIT with parts under Apache-2.0; every imported file keeps its notice and every
  package carries a `PROVENANCE.md`.
- Every score is a ratchet and fails in both directions.

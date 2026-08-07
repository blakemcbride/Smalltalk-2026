# Smalltalk-2026 → a multi-threaded Pharo

## Context

Smalltalk-2026 is a complete Smalltalk-80 in C: two object memories, one interpreter,
byte-exact against Xerox's own `trace2`, a C compiler and bootstrap, BitBlt and SDL3,
the 1983 MVC interface, and — uniquely — real parallel bytecode execution over a shared
mutable heap, TSAN-clean at 31 threads. All nine phases of `doc/PLAN.md` are complete.

Pharo does everything Blake wants except use more than one core. Making Pharo itself
multi-threaded means rewriting Spur, Cog and Morphic and auditing 10,000 classes inside
a fast-moving upstream that has publicly declined the design (Miranda et al., PX/24).
The alternative — the one this plan takes — is to move the *other* way: grow this VM
until Pharo's source runs on it, then adapt that source for genuine parallelism. The two
systems fork at that point, deliberately.

**Decisions taken (Blake, this session):**

| | |
|---|---|
| Compatibility scope | **Load Pharo's kernel**, not merely Pharo-flavoured application code |
| Closures | **Commit to full closures now** — Squeak V3PlusClosures |
| UI | **Keep MVC**, confine it to one worker, add modern tools on top over time |
| Sequencing | **Library before the parallel runtime**, except where forced otherwise |

**Standing constraints:** never create a git branch without explicit permission — commit on
`master`. `oracle/` is never committed or redistributed. `sources/` (markbush, MIT) is
frozen and never edited. Pharo is MIT *with parts under Apache-2.0*; every imported file
retains its notice.

**The load-bearing constraint:** the `OM=bb` build loads the real 1983 Xerox image and
reproduces `trace2` byte-for-byte. That is the project's only external correctness oracle
and it must stay green through every phase below. Everything here is designed so the
Blue Book path is untouched code, not carefully-preserved code.

---

## Two findings that shape everything

**1. Squeak's closure bytecodes fit exactly.** The dispatch switch leaves 126, 127,
138–143 unassigned (`src/interp/interp.c:1120-1131`). Squeak's V3PlusClosures set occupies
138 `pushNewArray`, 140 `pushRemoteTemp`, 141 `storeRemoteTemp`, 142 `popIntoRemoteTemp`,
143 `pushClosureCopy` — leaving 126, 127, 139 spare in Squeak too. Verified against
`opensmalltalk-vm/src/v3.stack/interp.c`.

Better: Squeak's unified `MethodContext` layout `sender pc stackp method closureOrNil
receiver` maps onto fields 0–5 of the existing layout with **one repurposed slot**.
`MethodContext.stClass:2` declares field 4 as `receiverMap`, and its own class comment says
*"unused (we expect to use it later for multiple inheritance)"*. It is never assigned
anywhere in `sources/`. `ST_CTX_TEMP_FRAME_START = 6` is preserved, and
`fetch_context_registers` (`interp.c:218-235`) already sets `home_context = ctx` for
anything that is not a `BlockContext` — so closure activations get correct temp addressing
with **zero interpreter changes**.

**2. A live parallelism bug exists today.** `ST_activate_block` (`interp.c:528-548`) mutates
the very `BlockContext` it activates — storing IP, SP, caller and arguments into it. So
recursive blocks corrupt their own frame (a faithful Blue Book limitation), and **two
workers cannot evaluate the same block object concurrently**. `forkParallel:` would hit
this immediately. A ~30-line copy-on-activate fix closes it before closures land.

---

## The three gaps

| Gap | Size | Nature |
|---|---|---|
| **Language** — closures, `{ }`, `#[ ]`, block temps, general pragmas, named primitives | ~1,200 lines of C | Bounded. Four of six are clean syntax errors today, so adding them can break nothing |
| **Object model** — weak refs/ephemerons, Slots, `Pragma`/`AdditionalMethodState`, immediates, traits, Pharo's primitive numbers | ~3,000 lines of C | The real cost of "load Pharo's kernel". Each item is individually tractable; together they are the multi-year part |
| **Library** — exceptions, `BlockClosure` protocol, modern Collection/Stream/String protocol, `new`→`initialize` | ~2,000 methods | Mostly Smalltalk. The exception system is the one with an uncertain estimate |

---

## Architecture

### The tree

```
sources/          1983 Smalltalk-80, markbush, MIT.  FROZEN.  Never edited.
lib/              OURS.  BSD-2.  Tonel.  Written by hand; the fork lives here.
pharo/            Imported Pharo packages.  Tonel.  MIT, attributed per package.
  <Package>/PROVENANCE.md    upstream repo, commit SHA, license, every local edit
profiles/         which packages compose an image
bench/parallel/   the scaling benchmark
doc/ParallelAudit.md         the audit ledger
```

**`sources/` is never edited. Every divergence is a new file in `lib/` or `pharo/`.**
This buys four things: `bluebook.profile` boots forever so the trace oracle and the three
existing parallel tests keep working; "how far have we drifted" has a mechanical answer;
class extensions are the only tool needed; and the eventual fork is a directory boundary
rather than an archaeology problem.

### Profiles replace MANIFEST

The Tonel reader brings a STON subset anyway, so reuse it:

```
Profile { #name : 'st2026-mvc', #requires : [ 'st2026' ],
          #packages : [ 'lib/Kernel-Exceptions', 'pharo/Collections-Abstract' ],
          #files : [ 'sources/Kernel-Objects/Object/Object.stClass' ],
          #exclude : [ 'FormMenuView' ] }
```

`#files` keeps `sources/MANIFEST` expressible without converting it.

### The substitution ratchet — how "load Pharo's kernel" is made tractable

Pharo's `Kernel` package cannot be loaded as an atomic unit: it fails on the first
unimplemented dependency and yields no signal. Instead every class has exactly one
provider, declared in the profile, and the port is a **ratchet** that moves classes from
ours to Pharo's one at a time. Progress is a number: *N of M Pharo kernel classes load and
pass their tests.*

Order the ratchet by VM entanglement, not by package:

| Tier | Classes | When |
|---|---|---|
| **1 — free** | Exceptions, Chronology, Announcements, most Collections and Streams, SUnit, STON | As soon as the protocol they need exists. No VM contract |
| **2 — contract** | `Object`, `Boolean`, `Character`, `SmallInteger`, `Float`, `Fraction`, `Array`, `String`, `Symbol`, `Association`, `Point`, `Semaphore`, `Process`, `ProcessorScheduler` | Requires the VM's field layout and primitive numbers to match Pharo's |
| **3 — metamodel** | `Behavior`, `ClassDescription`, `Class`, `Metaclass`, `CompiledMethod`, `Context`, `BlockClosure`, `Slot`, `Pragma`, `Trait` | Last. These *define* the contract the VM must satisfy |

**Never portable, and the plan does not try:** Morphic / Bloc / Spec / Athens (Cairo vector
graphics, single-threaded by construction); Opal / RB-AST / IRBuilder (assume SistaV1
bytecodes and Spur literals — we have a C compiler and the 1983 in-image compiler);
UFFI and plugin-heavy code; Iceberg / Metacello / Monticello (the C Tonel reader removes
the need); Fuel (serializes Spur object formats).

**Bytecode set:** we compile Pharo's source ourselves, so we choose the encoding —
V3PlusClosures, not SistaV1. Only Pharo code that *inspects* bytecodes would care, and
that code is on the not-portable list.

---

## Phases

Lettered to avoid colliding with `doc/PLAN.md`'s 0–9. Each has a gate that can fail.

### A — Syntax extensions
`{ expr. expr }`, `#[1 2 3]`, block-local temporaries, general pragmas (several per method),
named primitives (`<primitive: 'name' module: 'M'>` → primitive 117, descriptor as literal 0).

Delete the lexer's `<primitive: N>` special case (`lexer.c:462-492`) and move pragma
recognition into the parser, where the grammatical context is known — this removes a
special case rather than adding one, and `a < b` keeps working. Add `ST_TOK_LBRACE`/
`RBRACE`/`BYTE_ARRAY_OPEN` to `lexer.h:37-58`; new cases in `compile_primary`
(`compiler.c:679-840`). Add `make_byte_array` to `st_compile_context` and update its seven
initialisation sites.

*Do not add scaled decimals.* `1.23s2` already parses as `1.23 s2`, a unary send — the only
one of the six with an existing meaning to take away.

**Gate:** `./st80 -syntax sources/...` reports the same failure count as today; `make test`
under both `OM=bb` and `OM=mt` green; trace2 byte-identical; new unit tests for each form.

### B — Block re-entrancy *(done; scope corrected in contact)*
`primitive_value` allocates a fresh `BlockContext` copying `initialIP`, `numArgs` and
`home`, and activates the copy. ~30 lines, no bytecode or compiler change.

**This phase was planned against a wrong diagnosis and the correction is worth keeping.**
The plan assumed copying the context would make blocks re-entrant and named `fib 25` as
the gate. It does not, and could not. A `BlockContext` being both closure and activation
record is only *half* the reason Blue Book blocks are not re-entrant; the other half is
that a block's **arguments and temporaries live in the home method's frame**, not the
block's. Two activations of one block therefore share the variable, and so do two
*different* blocks in the same method, which the compiler may hand the same slot:

```
| b c | c := [:m | m * 10]. b := [:n | (c value: 99). n]. b value: 7   "answers 99"
```

No amount of copying reaches that. It is what closures exist to fix, so **`fib 25` moves
to Phase D**, and Phase D is now load-bearing rather than merely desirable.

What the copy does buy, and what it is worth: each activation gets its own instruction
pointer, stack pointer and caller. That makes **two workers able to evaluate one shared
block object** — the bug `forkParallel:` would have met on its first day — and it fixes
recursion wherever the outer activation's values are already on the stack before the
inner call, which is most single recursions (`factorial` works, `fib` does not).

**Gate, as met:** `test_parallel_mvc` binds one block object into a global and has all 31
workers send `value` to it, 2,480 times per run; reverting the copy makes that fail with
~106 wrong answers, so the test fails for the right reason. `test_image` asserts the
recursions that now work *and* asserts the aliasing case above, so the boundary is
recorded rather than left to be rediscovered — when Phase D lands, that assertion changes
with the behaviour it describes. trace2 and trace3 stay byte-exact.

### C — Tonel loader and the source pipeline *(done)*
Extract the event dispatch currently inline in `read_source` (`bootstrap.c:1380-1438`) into
`src/compiler/source.h` — a sink with `class_def` / `class_side_def` / `comment` / `method` /
`diagnostic` callbacks — and give it two producers: the existing chunk reader and a new
`src/compiler/tonel.c`. `SRC_read` dispatches on suffix. The three-pass structure in
`boot_build_locked` (`bootstrap.c:1654-1740`) is unchanged, and it already solves Tonel's
hardest problem: pass zero reads definitions only, so **load order is already irrelevant**.

Class *extensions* need almost no new machinery — an `Extension { #name : 'Object' }` file
produces zero `class_def` events and N `method` events with `class_name = "Object"`, which
is byte-for-byte what a second chunk file with `!Object methodsFor: 'x'!` already does.
Keep the leading `*` on extension-method categories verbatim.

Raise the capacity limits, which all fail at Pharo scale: `MAX_CLASSES 512`
(`bootstrap.c:23`), `MAX_IVARS 64`, `MAX_SYMBOLS 8192`, `MAX_GLOBALS 1024`,
`method_protocols[6000]`, `USTABLE_BUCKETS 512`. **First** convert `boot_class`'s four
`[64][64]` char arrays to offsets into a string arena and make `classes[]` `realloc`-grown —
at 9 KB per entry, 8192 entries is 72 MB of BSS otherwise.

The 22-bit source pointer (`bootstrap.c:1355`, 4.19 MB, currently *silently* skipped when
exceeded) becomes per-package source files: 22 bits indexes within a file and the 2 high
bits of byte 3 select one of four — which is what the 1983 scheme was for.

Reject loudly, and keep going, on `#type : 'immediate' | 'weak' | 'ephemeron'`, `#slots`,
`#traits` — with a total at the end. A porting effort needs the whole list, not the first item.

**Gate, as met:** `bluebook.profile` produced a **byte-identical image** to
`-manifest sources/MANIFEST` at every step of C1–C5, which is the only way to know a
refactor of this size changed nothing. The same class written both ways produces
identical selectors, identical bytecodes and identical object pointers. `lib/Probe` is
five classes, a chain three deep, class variables, class-side instance variables and an
extension to `Object`, loaded through `profiles/st2026.profile`.

C6 deliberately changes the image, which is the point of it: **the first method compiled
had no source at all**, because `getSource` reads position zero as "none" and that is
where the first method landed. One filler byte fixes it, and the sources file is one byte
longer. The 22-bit ceiling that used to drop a method's source silently now spills into
files 3 and 4 and reports when it runs out — verified by forcing the per-file limit to
600 KB, where `SystemDictionary>>install` lands in file 3 and reads back through the
image's own `RemoteString`.

Two things nothing reset between builds turned up here: the symbol index, which would
have answered a lookup with a confident wrong Symbol, and the source text, which
accumulated.

### D — Full closures
The largest VM change, and after Phase B's correction the only thing that can make blocks
genuinely re-entrant: block arguments and temporaries have to live in the activation's own
frame, which is what `pushClosureCopy` and the remote-temp bytecodes are for.

Before writing any of it, add to `tests/unit/test_trace.c` an
assertion over the loaded Xerox image that **every live `MethodContext` has field 4 nil**
and **no method has primitive index 198 or 199**. The whole design rests on those two facts;
static evidence is strong but the shipped `VirtualImage` was built by Xerox.

- `BlockClosure` as `Object variableSubclass:` with `outerContext startpc numArgs` — matches
  `shape_of_class` (`prim.c:279-296`), so `new:`/`at:`/`size` work unmodified.
- Reach it from C through `st_om_vm_state[]` (`om.h:67-71`), **not** a new guaranteed oop —
  the table ends at 56 and `OM=bb` loads a real image where 58+ are ordinary objects. Add
  `ST_VM_CLASS_BLOCK_CLOSURE` and `ST_VM_SELECTOR_ABOUT_TO_RETURN`; bump `IMAGE_VERSION`
  2→3 (`image_mt.c:32`); visit them in `provide_roots` (`interp.c:288-311`). In an `OM=bb`
  build these are nil, so every closure primitive fails and closures are unreachable —
  **this is the coexistence mechanism.**
- Bytecodes 138/140/141/142/143 with the verified operand encodings; `ST_activate_closure`
  beside `ST_activate_block`; primitives 201–206, 221–222, and 82 (`valueWithArguments:`,
  missing today although `BlockContext.stClass:116` declares it and its Smalltalk fallback
  has an inverted test).
- **The compiler needs two passes.** `numCopied`, which names are remote, and each frame's
  index map are whole-method properties known before the first byte of a block is emitted.
  Run *the same recursive-descent parser twice* — pass 0 records variable uses, pass 1 emits.
  Not an independent AST: the set of blocks that are real blocks (versus inlined
  `ifTrue:`/`and:`/`whileTrue:` bodies) is decided by `at_inlinable_block` (`compiler.c:888-898`)
  plus two rewind-and-retry sites, and any independent analysis must re-derive those
  decisions and stay in sync forever. Running the same parser makes them identical by
  construction. `mark`/`rewind_to` (`compiler.c:859-881`) gain three lines to save and
  restore the use list, exactly as they already do for `literal_count`.
- A `dialect` field in `st_compile_context` defaults to Blue Book; closure opcodes are
  emitted only in `ST_DIALECT_CLOSURES`. The block-argument slot-sharing rule
  (`compiler.c:755-785`) is a Blue Book artifact and must be dead in closure mode.
- Frame ceiling: `numArgs + numCopied + numLocals + stackDepth ≤ 32`. Emit a compiler
  **error**, not the silent heap corruption documented at `compiler.c:1459-1476`.

**Gate:** trace2 and trace3 byte-identical; `test_self_hosting` (`test_image.c:1055-1106`)
still passes in Blue Book dialect — it compares against the image's *own* 1983 compiler, so
any unconditional change to block emission fails there loudly; closure bytecode tests match
the verified tables.

### E — Non-local return, unwinding, exceptions
Rewrite `return_value` (`interp.c:598-606`) so the `ST_CLASS_BLOCK_CONTEXT` arm is *literally
the three lines it is today* and everything new is in the `else`. **Do not touch `do_return`** —
the nil-sender bottom-of-the-world stop is what `-eval` depends on (`main.c:504-530`).

Walk `closureOrNil`→`outerContext` to the home; a dead home sends `cannotReturn:`; an
unwind-marked context between (a method whose primitive index is **198**) sends
`aboutToReturn:through:`. Primitives 198 and 199 need **no code in `prim.c`** — unknown
primitives already fail (`prim.c:1235`), and they are tags, not operations. Reserve 3 stack
slots for the error-path sends. `cannotReturn:` must be looked up before being sent: the
1983 library does not implement it, and a blind send lands in `doesNotUnderstand:` →
`NotifierView` → the reporting recursion at `interp.c:412-421`.

Then the exception system, in Smalltalk: `Exception`, `Error`, `Warning`, `ZeroDivide`,
`MessageNotUnderstood`, with `signal`, `on:do:`, `ensure:`, `ifCurtailed:`, `return:`,
`retry`, `resume:`, `pass`, `outer`. Handler search is `^method primitive = 199` up the
sender chain — pure library. Target ANSI/early-Squeak semantics, not Pharo's modern
`runUntilErrorOrReturnFrom:`, which drags in process machinery.

`Object>>error:` becomes `^Error new signal: aString` and the `NotifierView` moves **down**
into `Error>>defaultAction`; `doesNotUnderstand:` keeps its `tryCopyingCodeFor:` path and
then signals `MessageNotUnderstood`. Every existing caller is unchanged, unhandled errors
still open the same debugger, and `on:do:` now works.

**Gate:** `[1/0] on: ZeroDivide do: [:e | e return: 42]` → 42; `[^1] ensure: [flag := true]`
sets the flag; `nil foo` raises a catchable `MessageNotUnderstood`; an unhandled error still
reaches `NotifierView`; `test_compile_inspect_debug` and `test_browsing` still pass; a
headless bootstrap reports errors as **text**, never by drawing.

### F — The Pharo object model
This is what "load Pharo's kernel" costs, and it is the phase most likely to be revised
in contact.

- **Weak references and ephemerons.** A weak class flag; the marker does not follow weak
  slots; after marking, dead weak slots are nilled and finalization is queued. Bounded, and
  our mark-and-recount collector at a safepoint is a friendly place to add it.
- **Slots.** Support plain `#instVars` (which is what most of Pharo's kernel uses) and
  `#slots : [...]` where every entry is a plain slot. Reject indexed/weak/custom slot types
  with a named report. Full `Slot>>read:`/`write:to:` indirection would mean rewriting
  variable resolution in the compiler — defer until a class you actually want requires it.
- **`Pragma` and `AdditionalMethodState`.** Pharo's convention is a literal-frame one:
  `literal[n-1]` is the class binding, `literal[n-2]` is the selector *or* an
  `AdditionalMethodState`. Implementable in `build_header`/`COMPILE_method`
  (`compiler.c:1670-1785`). This is what turns Phase A's parse-and-discard into real pragmas.
- **Immediates.** Map `#type : 'immediate'` onto our tagging for `SmallInteger` and our
  `CharacterTable` for `Character`. `SmallFloat64` is Spur-only; Pharo's `Float` works boxed.
- **Traits.** Load-time flattening: copy the composed traits' methods into the class,
  recording origin in the protocol string (`*trait:TA`). Implement `+` only; reject `-` and
  `@`. ~200 lines for ~80% of the value. Real trait reflection and update propagation are
  out of scope.
- **The primitive set.** Extract mechanically from Pharo's Tonel sources every
  `<primitive: N>` its kernel invokes; implement them; report the remainder. This converts
  an unbounded question into a finite checklist.

**Gate:** the extraction report exists and shrinks; a Pharo kernel class from Tier 1 that
uses weak references, pragmas and a trait loads and passes its own SUnit tests.

### G — Kernel protocol and the first ratchet turns
`Object` protocol (`displayString`, `printNl`, `ifNil:`, `assert:`), Collection protocol
(`do:separatedBy:`, `sorted:`, `flatCollect:`, `ifEmpty:`, ~80 methods), Dictionary
(`at:ifAbsentPut:`, `keysAndValuesDo:`), Stream (`<<`, `streamContents:`), String
(`format:`, `join:`, `substrings:`, `trimBoth`).

`new` → `initialize`: **the loader synthesizes the override.** When a `lib/`/`pharo/` class
has `initialize` reachable in its loaded superclass chain and defines no class-side `new`,
emit `Foo class>>new ^super new initialize` — the 1983 idiom. A global `Behavior>>new`
change would double-initialize the ~34 1983 classes that call `^super new initialize`
themselves; a per-class flag taxes the hottest allocation path. The loader reports every
class it did this to, so the list is auditable.

Then begin the ratchet: SUnit first — it converts "did the port work" from a judgement call
into a green bar — then STON, Announcements, Zinc-Character-Encoding, and the Tier 1 kernel
packages.

**Gate:** a checked-in corpus of ~500 Pharo one-liners evaluates to the documented answers.
Extract them mechanically from Pharo's own `>>>` doctest comments — `Collection.class.st`
is full of them, they are machine-parseable, and they are a free oracle. SUnit's own suite
passes.

### H — M:N scheduling and atomic semaphores
Per-worker state in `struct st_worker` (`worker.h:57-75`): `active_process`, ready
`LinkedList`s per priority — the **existing Blue Book objects**, so the root walk and
`Processor` reflection work for free and `SCHED_add_last_link`/`remove_first_link`'s
hard-won reference-counting discipline (`st_sched.c:97-135`) is reused rather than rewritten.

The three C statics in `st_sched.c:24-29` split three ways: `new_process`/
`new_process_waiting` **replicate** into `st_worker`; `async_queue` **reorganizes** into a
mutex-guarded MPSC queue so thread 0 never blocks; `input_semaphore` stays global.

`Processor activeProcess` becomes primitive 240 answering the calling worker's process,
with the instance variable kept as the fallback a snapshot carries. Reserve primitives
**240–255** for Smalltalk-2026 parallel operations and document the table in
`CONCURRENCY.md`: 240 `activeProcess`, 241 `activeWorkerIndex`, 242 `workerCount`,
243 `forkParallel:`, 244 `pinToWorker:`, 245 `compareAndSwapSlot:from:to:`.

Work stealing: own queue first, then a random victim's *tail* under its lock, then park on a
pool-wide idle condvar — spinning 31 cores on an idle desktop is unacceptable. Green
processes here are coarse, so a per-worker mutex is not the bottleneck; record Chase-Lev
deques as a later optimization to be justified by measurement, not adopted on reputation.

Atomic `Semaphore`: keep primitives **85/86** so the image source does not change, and guard
them with **stripe locks keyed on `OM_identity_hash`** (64 stripes) — no object gets wider,
no header format changes, and the hash is already stable across collection and snapshot.
The bug being closed: `SCHED_primitive_wait` (`st_sched.c:388-409`) tests `excessSignals`,
*then* adds to the list; a `signal` landing between spends itself on an empty list and the
waiter queues forever.

**Rule, written down once rather than rediscovered per lock: never poll a safepoint holding
a stripe lock.** Same class of hazard `om_mt.c` already solved by dropping `table_lock`
before collecting. Assert it in debug builds.

**Gate:** `test_parallel_processes` — N workers, 10N green processes, every process
completes and every semaphore handshake is accounted for, one hour TSAN-clean;
`Processor activeProcess` differs per worker.

### I — The concurrency classes
`Mutex`, `Monitor`, `SharedQueue`, `Promise`, `Processor>>forkParallel:` in
`lib/Concurrency/`, over primitives 85/86/240/243/245. **Every one needs `ensure:`**, which
is why E precedes this — building them first produces classes that leak locks on any
non-local exit. `Mutex` carries an `owner` slot so re-entry is *detected* rather than
deadlocking. `SharedQueue` is reimplemented over `Monitor`. `Promise` gets `signalError:`
from the start, so a failed producer does not hang every consumer forever.

**Gate:** 8 producers / 8 consumers move 10⁷ items through a `SharedQueue` with none lost or
duplicated; a `Mutex`-guarded counter equals exactly the number of increments.

### J — The multi-threaded desktop
Thread 0 pumps SDL and nothing else; workers run Smalltalk. Three things must be designed,
not discovered:

- **The display.** *Reorganize, don't serialize*: confine all `Display`-destined drawing to
  one designated UI worker (`pinned_only`), never a steal target. This also solves the next one.
- **`Sensor` / `Controller>>controlLoop`.** Pin the UI process to the UI worker (primitive 244).
- **Shutdown is a bug-in-waiting today.** `st_vm.running` is inside `_Thread_local st_interp`,
  so primitive 113 (`prim.c:1213-1215`) stops exactly one worker. Add a global
  `st_atomic_int st_shutdown_requested` polled beside `WORKER_poll()` at `interp.c:895`.

**Gate:** the desktop runs on N workers; a Browser is usable; a `forkParallel:`-ed
computation visibly runs while the UI stays responsive; 30 minutes of scripted input under
TSAN, clean.

### K — The scaling benchmark
`doc/PLAN.md:335`'s Phase 7 exit criterion, never met. `bench/parallel/` driven by
`tests/bench/bench_parallel.c`, inheriting `test_parallel_mvc.c`'s discipline: every worker
computes something only it can check, so a fault arrives as a wrong answer rather than a
hope that a sanitizer noticed. `WORKER_start(n, ...)` already takes an explicit count, so
sweep 1, 2, 4, 8, 16, N.

Three kernels, because they measure different things: **Mandelbrot** (the ceiling — heavy
arithmetic, little allocation; also the direct comparison to RSqueak's STM attempt, which
*lost* to single-threaded on exactly this); **disjoint intervals** (pure interpretation, one
context per activation — measures `table_lock` contention); **collection churn** (heavy
allocation and collection — **this is the one that will not scale at first**, and its number
is the honest headline).

Instrument total safepoint pause in `WORKER_request_safepoint`/`release` — without it a
scaling failure cannot be attributed and you will guess.

**Gate, stated so it can fail:** on ≥ 8 physical cores, `mandelbrot` ≥ **4.0×** at 8 workers
versus 1 with identical answers; `intervals` ≥ **3.0×**; `collections` reported, not gated,
until TLABs land. All three clean under `make OM=mt TSAN=1`. Baseline checked in; a later
run regressing > 15% fails the build.

### L — The parallel-safety audit, as continuous practice
Pallas & Ungar's *serialize / replicate / reorganize*, plus the two buckets that keep the
audit finite: **immutable** (written once at build) and **thread-confined** (only ever
touched by one worker). If everything lands in *serialize*, the audit has failed and the
system will not scale.

Build **`st80 -audit <Class|Package>`**: walk method dictionaries, decode bytecodes, report
every literal-variable store with its class and selector. The compiler already knows these
exactly — a store to a global or class variable is bytecode 129/130 with a
`storeLiteralVariable` descriptor. That converts "grep and hope" into a complete list;
`survey.c` and `-disasm` already establish the pattern. Record each decision as a pragma
(`<shared: #serialize>`) so it lives next to the code, and keep the ledger in
`doc/ParallelAudit.md`. TSAN is necessary and **not sufficient** — it reports only races
that actually executed.

The named hazards and their decisions:

| Structure | Decision |
|---|---|
| **Symbol interning** (`Symbol.stClass:52-63`) | *Serialize* — the `hasInterned:` check and the insert must be **one** critical section; splitting them is the whole bug. One `Mutex` covering both the image's `USTable` and the C-side `symbols[]`. Later *reorganize* into a primitive over a CAS table |
| **`SystemDictionary` globals** | *Serialize writes, free reads.* A compiled method holds the `Association`, not the dictionary, and an Association's identity never changes — so reading an existing global needs no lock at all |
| **`methodDict` during live recompile** | *Reorganize* — **method dictionaries become immutable once published**; every change publishes a new one by a single atomic store. `compile_into` already does this by accident on its overflow path (`bootstrap.c:1330-1343`). Removes the hazard rather than locking it |
| **A future method cache** | Per-worker, with a global epoch bumped on publish. There is none today (primitive 89 is a no-op) — designing it now costs nothing, retrofitting costs a week of confusing bugs |
| **`Transcript`** | *Replicate* a per-worker buffer, *serialize* the flush at `cr`/`endEntry`. Removes contention **and** makes lines whole |
| **`Delay`** | *Reorganize* into a dedicated VM timer thread over `ST_monotonic_ns`. Serializing does not fix the second problem: with every worker parked on the idle condvar, nothing polls for expiry |
| **Lazy class-variable init** | *Forbidden.* `^Default ifNil: [Default := ...]` is check-then-act; two workers each build one. Initialize eagerly at build, which the bootstrap already does. `-audit` flags any class-variable store guarded by an `isNil` test on the same variable |
| **`SourceFiles`** (`bootstrap.c:2537-2580`) | *Reorganize.* One `ReadWriteStream` shared by every `getSource` — two workers browsing get each other's text. **The sharpest one on the list, because it produces wrong source rather than a crash**, in the Browser that Phase 8 declared working |
| **`Object>>dependents`** | *Serialize*, or replace with `Announcer` |
| **`ScheduledControllers`** | *Thread-confine* to the UI worker — one decision covering many structures |

### M — The ratchet continues
Tier 2 then Tier 3 kernel classes. Each turn: load Pharo's version, run its SUnit suite,
add its audit rows, record local edits in `PROVENANCE.md`. This is where the fork becomes
real and visible.

**This phase does not end.** Treat it as ongoing work and do not let the plan pretend
otherwise. Its progress metric is the ratchet count.

---

## Verification

Every phase, before it is called done:

```bash
make clean && make OM=bb && make OM=bb test        # trace2/trace3 oracle
make clean && make OM=mt && make OM=mt test        # the real system
make clean && make OM=mt ASAN=1 test
make clean && make OM=mt TSAN=1 test
./st80 -bootstrap -manifest sources/MANIFEST -o st80.image && ./st80 -run st80.image
```

Specific oracles, in decreasing order of how much they are worth:

1. **`trace2` byte-for-byte** — 611 lines. Xerox's own execution trace. Never negotiable.
2. **`bluebook.profile` byte-identical image** — proves the loader refactor changed nothing.
3. **Tonel vs chunk produce identical method dictionaries** — `census.c` gives this free.
4. **`test_self_hosting`** — compares the C compiler against the image's own 1983 compiler.
   Any unconditional change to block emission fails here loudly.
5. **Pharo's `>>>` doctests**, extracted mechanically — a free oracle for the library port.
6. **Ported SUnit suites** — the ratchet's per-class gate.
7. **TSAN**, on every parallel test, understood as necessary and not sufficient.

New test files: `test_tonel.c`, `test_closures.c`, `test_exceptions.c`,
`test_parallel_processes.c`, `test_parallel_lib.c`, `bench/bench_parallel.c`.

---

## Honest sizing

Bottom-up, for a single developer: A 3wk · B 1wk · C 5wk · D 6wk · E 7wk · F 16wk ·
G 8wk · H 9wk · I 4wk · J 5wk · K 3wk · L continuous · M unbounded.

That is ~67 weeks of *scheduled* work before L and M, and it contains no time for the thing
that turns out to be wrong, no rework of a decision that does not survive contact, and no
reading. Projects of this shape run 1.7–2.5× their bottom-up estimate.

> **Phases A–K: 2–3 years full-time, or 4–6 at a serious part-time pace.
> Phases L and M do not finish; they are how the system is maintained.**

The three items most likely to break that estimate, named: **(E) `ensure:` ordering during
a non-local return**, the classic place a Smalltalk exception implementation sits 90% done
for months; **(F) the Pharo object model**, which is a research-shaped phase wearing an
engineering phase's clothes; and **(L) the audit**, unbounded in principle and bounded in
practice only by choosing to stop — audit `lib/` and `pharo/` completely, audit `sources/`
only for what the loaded profile actually executes under threads.

### Where the work banks value

This matters more than the schedule, because the real risk in a multi-year solo project is
stranding.

- **After C** — the loader exists, the 1983 system is untouched and still works, every
  future library change is cheap. Pure option value.
- **After E** — **the system has exceptions.** The difference between a museum piece and
  something you can write a program in. The strongest early stopping point.
- **After G** — Pharo-flavoured code runs. A small modern Smalltalk with a 1983 UI.
  Publishable as such.
- **After J** — it is a thing you can show someone in thirty seconds.
- **After K** — **the thesis is demonstrated and measured**: a Smalltalk that scales across
  cores, with numbers, against a literature that reports STM *losing*. The strongest
  stopping point for the concurrency half, worth reaching even if M never advances. This is
  also the paper.

---

## Critical files

| File | What changes |
|---|---|
| `src/compiler/lexer.c` | `{`/`}`/`#[` tokens (303-496); delete the `<primitive:` special case (462-492); minus-sign predecessor list (516-528) |
| `src/compiler/compiler.c` | Two-pass analysis; block emission (734-836); `resolve` (469-531); `mark`/`rewind_to` (859-881); `max_stack_depth` (1477-1547); pragma loop (1633-1637); frame sizing (1651-1661) |
| `src/compiler/tonel.c` | **New**, ~800 lines: bracket scanner with Tonel's exact escape set, STON subset, sink producer |
| `src/compiler/source.h` | **New**: the sink interface both readers feed |
| `src/interp/interp.c` | Closure bytecodes in the dispatch switch (905-1132); `ST_activate_closure` beside `ST_activate_block` (528-548); `return_value` (598-606) — **not `do_return`**; `provide_roots` (288-311); shutdown flag at the poll site (895) |
| `src/interp/prim.c` | Primitive 82; closure primitives 201-206, 221-222; parallel primitives 240-245; the per-thread quit bug (1213-1215) |
| `src/om/om.h`, `image_mt.c` | New VM-state slots; `IMAGE_VERSION` 2→3 (`image_mt.c:32`) |
| `src/om/om_mt.c` | Weak references and ephemerons in the collector |
| `src/boot/bootstrap.c` | Sink refactor of `read_source` (1380-1438); string arena and capacity limits (23-25, 90, 93, 489); per-package source files (1355) |
| `src/sched/st_sched.c` | Statics (24-29) become per-worker; stripe-locked semaphores (375-409) |
| `src/sched/worker.c`, `worker.h` | Per-worker ready queues, stealing, idle parking, pinning |
| `src/main.c` | `do_run` (267-350) becomes the thread-0 pump; `-audit` |
| `doc/` | `CONCURRENCY.md` (primitive table, lock rules), `MultiThreading.md` (status), `ParallelAudit.md` (**new**), `LICENSING.md` (Pharo MIT + Apache parts) |

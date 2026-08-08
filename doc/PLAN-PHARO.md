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

### A — Syntax extensions *(done)*
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

### D — Full closures *(done)*
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
**Done.** `fib 25` answers 75025 as a recursive block; each iteration of
`(1 to: 3) do: [:i | a add: [i]]` captures its own `i`; a block outlives the method that
made it; and Phase B's aliasing case is asserted **both ways** in one test — 99 as Blue
Book blocks, 7 as closures. The two assumptions D0 measured against the shipped 1983
image held: 35 contexts, none using field 4; 4505 methods, highest primitive 135.

The dialect is a field in the compile context and defaults to Blue Book, so the 1983
library, `test_self_hosting` and the trace oracle never meet any of it. `trace2` and
`trace3` stayed byte-exact through every step, including the rewrite of `return_value`.

Two things remain open and are Phase F/G work rather than D's: **a package cannot yet
declare its dialect** — `-closures` is a developer switch on `-eval`, and the real answer
is a `#dialect` key in a profile; and **`cannotReturn:` cannot be exercised end to end**
until closures can be compiled into methods rather than only doIts, because in a doIt
every `^` targets the doIt itself, which is always alive.

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
- **The part with the real bookkeeping in it**, written down because it is what makes D5
  a project rather than an afternoon. Each scope's frame is `[args][copied][locals]`, and
  a scope that has any captured-and-assigned variable needs a *vector* — an `Array` in a
  frame slot — so that copying it into a closure shares the variable rather than its
  value. A block nested two deep that reads a grandparent's boxed variable needs the
  grandparent's vector, which means its parent must copy that vector too even though the
  parent never mentions it: the copied sets propagate up the scope tree. Simplifying by
  giving the whole method one vector is wrong, and wrong in a way that hides — an outer
  block's own captured temporaries would then be shared between its own activations.
- Reserve three stack slots in `max_stack_depth` for any method with a `^` inside a
  non-inlined block: `cannotReturn:` and `aboutToReturn:through:` push onto the returning
  frame, which was sized for its own maximum depth. Getting this wrong reproduces the
  silent corruption at `compiler.c:1459-1476`, on the error path only.

**Gate:** trace2 and trace3 byte-identical; `test_self_hosting` (`test_image.c:1055-1106`)
still passes in Blue Book dialect — it compares against the image's *own* 1983 compiler, so
any unconditional change to block emission fails there loudly; closure bytecode tests match
the verified tables.

### E — Non-local return, unwinding, exceptions *(done)*
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

**Gate, as met.** `[1/0] on: ZeroDivide do: [:e | e return: 42]` answers 42. A `^` out
through an `ensure:` runs the unwind block and not the statement after it; **two nested
`ensure:`s both run, innermost first** — the case these implementations usually get wrong.
`ifCurtailed:` runs its block only on the early exit. `nil foo` is a catchable
`MessageNotUnderstood` whose handler can read `e message selector`. `retry`, `pass` and
`resume:` work. Unhandled errors report as text *and* still reach `NotifierView`.
`test_compile_inspect_debug` and `test_browsing` pass; `trace2`/`trace3` byte-exact.

**Two bugs found on the way, both older than this phase.**

`ST_SELECTOR_DOES_NOT_UNDERSTAND` and its three companions were allocated as **empty
objects whose text was never filled in**, and never interned. The bootstrap table has
carried the spelling since the file was written and nothing read it — so the interpreter
looked up a blank symbol, matched nothing, and **`doesNotUnderstand:` had never once been
sent to the image**: every unhandled message went to the VM's own fallback report instead
of the 1983 `NotifierView` the library expects. `mustBeBoolean`, `cannotReturn:` and
`cannotInterpret` were in the same state. Filling them is only half the fix; they must
also be interned, or the next mention of the same characters makes a second Symbol and the
fixed pointer still finds nothing.

And a headless image had **no way to say anything**. `Transcript` is a `TextCollector`: it
draws, and drawing is exactly what is missing when there is no screen — so an unhandled
error could only be reported by the one means that was unavailable. Primitive 248 writes a
String to stderr, and respects the same reporting switch the VM's own diagnostics use, so
the bootstrap's deliberately-silent first initializer pass stays silent.

**Both loose ends closed, and each turned out to hide a real defect.**

Making the search faster meant reading a frame's primitive number in C, and that exposed
a latent crash in D4's `find_unwind_between`: a `BlockContext`'s field 3 is its **argument
count**, not a method — the two layouts share the slot — so walking past a Blue Book block
during a closure's non-local return took the body of a SmallInteger. One image holds both
kinds now, so such a frame is not a hypothesis. The walk is Squeak's primitives 195 and
197, both optimisations with the Smalltalk bodies kept behind them.

Making `resume:` safe meant asking what "still live" means, and the honest answer is
**reachability**, not a nil program counter: `do_return` nils the fields of the frame it
leaves, not of everything that frame called, so a returned context still looks perfectly
well formed. The check is in primitives 246, 247 and 249 rather than in Smalltalk, so
every jump is safe by construction.

The same pass found that **a handler was not disabled while it ran**, so anything it
signalled found the same frame again and the search that was meant to move outwards went
round for ever — an exception a handler could not deal with hung the image instead of
reaching the handler outside it. And `retry` re-sent `on:do:` rather than restarting its
frame, so a retry that never succeeded grew the stack until it dumped core; it restarts
now and loops, which is what a retry that never succeeds should do.

One more, older than any of this and reported by the class it lives in.
**`MethodContext>>restart` counted a frame's arguments twice** — it set the stack pointer
to `numArgs + numTemps`, and `numTemps` already counts the arguments. That is the
Debugger's restart button, and `restartWith:` after a method under debug is recompiled, so
a restarted frame came back believing it had two more values below its stack than it did.
The class contradicts itself about it, which is what made it findable:
`setSender:receiver:method:arguments:` — twenty lines further down, and the method that
*creates* a context — says `stackp := method numTemps`, and that one agrees with the
interpreter. `CompiledMethod>>numStack` had the same double count. Both are corrected in
`lib/Kernel-Methods/`, because `sources/` is frozen.

One compiler change came out of writing the package: **pragmas and temporaries are
accepted in either order and any number of times**. The Blue Book puts temporaries first
and has one pragma; Pharo writes the pragma first at least as often, and a reader that
insists on one order rejects ordinary source for a reason that is about nothing.

### F — The Pharo object model *(F1–F5 done; second gate blocked on a provenance decision)*
This is what "load Pharo's kernel" costs, and it is the phase most likely to be revised
in contact.

- **Weak references — done.** Bit 12 of the class format word, which the Blue Book leaves
  free between the indexable flag and the instance size, so a weak class is an ordinary
  class to everything that does not ask. The marker skips a weak object's *indexed* fields
  and keeps its named ones strong; between the mark and the sweep — the only moment when
  every count is exact and nothing has been freed yet — a slot pointing at a zero-count
  object is nilled. `WeakArray` is in `lib/Kernel/`.

  **Ephemerons are still refused, by name.** An ephemeron is not a weak object with another
  name: its key is weak while its value stays strong *for as long as the key lives*, and
  deciding that needs the marker to run to a fixed point rather than once. That is a
  different collector, not a different flag.

  Two things had to be built before weakness could even be observed, and both were bugs.
  The 1983 library has **no `garbageCollect` anywhere in it** — the image cannot ask for a
  collection, so a weak slot could never be seen to clear. And asking for one found that
  **a collection during a doIt freed the doIt**: `ST_interp_register` is called from
  `ST_interp_init`, which the `-run` path calls and the doIt path does not, so the collector
  walked an empty interpreter table and the interpreter carried on reading bytecodes out of
  freed memory. `provide_roots` now visits the running thread unconditionally, which is the
  half a future caller cannot forget.

  **Known limitation:** the marker walks every slot of a context, not only those below its
  stack pointer, so a stale slot in a *running* frame keeps its object alive and defeats a
  weak reference made in that same frame. Making it precise needs every parked worker to
  write its registers back at the safepoint first — worth doing, and a separate change with
  its own risk.
- **Slots — done.** `#slots : [ 'a', 'b' ]` is Tonel v3's spelling of `#instVars`, and for a
  plain slot the two say exactly the same thing, so a plain-slot class loads and behaves like
  any other. A slot with a *kind* — `#a => WeakSlot` — is refused **by the name of the kind**,
  which meant teaching the STON reader to read `=>` at all rather than letting the header fail
  to parse: a rejection that cannot say *what* was rejected tells a porting effort nothing.
  Full `Slot>>read:`/`write:to:` indirection would mean rewriting variable resolution in the
  compiler, and waits for a class that actually needs it.

- **Immediates — done, and narrower than the word suggests.** An immediate has no object
  header: the value *is* the pointer. There is one tag bit in this memory and `SmallInteger`
  has it, so a **new** immediate class cannot be made here at all. But the two classes Pharo
  declares immediate are already immediate here by other means — `SmallInteger` is the tagged
  one, and every `Character` is a unique entry in `CharacterTable`, which is what makes
  `$a == $a` true. So those two declarations load and any other is refused by name. That is
  the honest reading of the constraint: Pharo's own headers pass through, without pretending
  a third immediate class could work.
- **`Pragma` and `AdditionalMethodState` — done.** Phase A parsed pragmas and threw them
  away; they are objects now, so a framework can act on them — SUnit's `<test>`, and the
  `<shared: #serialize>` that Phase L's audit wants.

  Pharo's convention could not be borrowed as written. It puts the `AdditionalMethodState`
  next-to-last in the literal frame, and **next-to-last here is where the Blue Book header
  extension goes** when a method declares a primitive. So the state is found by its *class*
  instead: a frame holds a few dozen literals, nothing else instantiates that class, and
  the scan cannot be confused by position. A profile that has not loaded the class gets
  nil back from the compiler and its methods are compiled exactly as before, which is the
  Blue Book case.
- **Immediates.** Map `#type : 'immediate'` onto our tagging for `SmallInteger` and our
  `CharacterTable` for `Character`. `SmallFloat64` is Spur-only; Pharo's `Float` works boxed.
- **Traits — done, by flattening.** A trait is a named bag of methods with no instances and
  no place in the superclass chain, and it is applied by compiling its **source** into each
  class that names it — once per class, not once with the method object shared.

  That is not an implementation detail. A Blue Book method names an instance variable by its
  **index in the frame** and carries its own class binding in the literal frame, so one
  CompiledMethod installed in two classes would read whichever field happened to sit at that
  index. Recompiling per class is what makes a trait method mean the same thing everywhere it
  lands, and it is why flattening is the honest way to do traits on this VM rather than a
  shortcut.

  The rules, and each one is a way of refusing to be silently wrong:

  - **A method the class defines itself always wins.** That is why flattening runs after the
    whole compile pass — until then there is no way to know what the class says for itself.
  - **A selector provided by two sibling traits installs neither**, and both traits are named.
    A first-wins would make the answer depend on the order the names were written, which is
    the bug traits exist to avoid.
  - **Within one trait, its own methods override the ones it composes.** That is not a
    conflict; it is what composing means. A trait reached twice by different paths is one
    trait, not a conflict.
  - **`+` only.** `-` (exclusion) and `@` (aliasing) change *which* methods a class gets, so
    honouring the `+` and dropping the rest would load cleanly and put the wrong methods in
    the class. Refused by name, with the expression quoted.
  - **Class-side methods come across from `#traits` alone.** Pharo's mechanical companion
    `#classTraits : 'TFoo classTrait'` therefore says nothing new and is accepted and dropped;
    a `#classTraits` composed differently from `#traits` is refused, because that one *is* a
    statement this system would lose.
  - **A trait with instance variables is refused.** It would have to add a field to every
    class that takes it, changing instance shape from a direction nothing else here does.

  A composition that cannot be honoured is counted **apart from** a class definition that was
  skipped, and reported on its own line — the class was built, its own methods are in it, and
  what is missing is the trait's. Calling that "skipped" would misdescribe what is in the
  image. Each flattened method's protocol records where its source lives (`*trait:TGreeting`),
  which puts them together in the Browser under the convention that already means "defined
  elsewhere".

  Real trait reflection and update propagation remain out of scope: a trait is not an object
  in the image, and changing one does not re-flatten the classes that took it.
- **The primitive set — the tool is done; the Pharo corpus it is aimed at is not here yet.**
  `st80 -primitives <files|-profile p>` compiles every method in a body of source and reports
  every primitive it asks the VM for, against what this VM does with it. It reads both source
  formats, because it goes through the same reader everything else does.

  The report has **four** outcomes rather than two, and the extra two are the point:

  | | |
  |---|---|
  | **implemented** | the VM answers it |
  | **accepted, and does nothing** | succeeds without doing anything, so the method's Smalltalk fallback — usually where the real work is — never runs. Not the same as implemented, and a silent failure rather than a loud one. Five of them: 89, 91, 92, 94, 116 |
  | **deliberately absent** | 198 and 199 are *marks*, read by a walk up the sender chain. They must keep failing; implementing either would break `ensure:` and `on:do:` |
  | **not implemented** | the work |

  Primitive **117 is a doorway, not a primitive**: a named primitive gets one row per
  `(module, function)`, because a report that folded every plugin callout into a single row
  saying "117" would answer nothing at all. That needed the compiler to carry the two strings
  on `st_compiled_code`, since the number alone does not have them.

  Against the 1983 library the answer is 109 distinct primitives, 32 to implement — chiefly
  `LargePositiveInteger` arithmetic (21–37), the stream primitives (65–67), snapshot (97) and
  the Alto/Posix file primitives. Against `st2026.profile` it is 126, of which 87 are
  implemented, and 198/199 appear under *deliberately absent* pointing at `BlockClosure>>ensure:`
  and `on:do:` — which is the report describing the exception system correctly.

  The table it reads lives in `prim.c` immediately after the dispatch switch, because the only
  thing keeping the two in step is that they are impossible to read apart.

**Gate:** *partly met.* The extraction report exists, runs over both formats, and gives a
number that can shrink. The other half of the gate — a Pharo kernel class that uses weak
references, pragmas and a trait, loading and passing its own SUnit tests — **cannot be
claimed, because no Pharo source is vendored under `pharo/` yet.** Every mechanism it needs
is now in place and tested against hand-written equivalents in `lib/Probe/`; what is missing
is a provenance decision, not code. See *Before F can be closed* below.

#### Before F can be closed: a decision that is not a coding decision

Every mechanism F was about now exists and is tested — weak references, pragmas as objects,
slots, immediates, traits, and the primitive report. All of it is exercised against
hand-written Tonel in `lib/`, which proves the mechanisms work but proves nothing about
Pharo, because **no Pharo source is vendored under `pharo/` yet.**

That is deliberate, and it is not something to do unilaterally. Importing Pharo means:

- **Licensing.** Pharo is MIT with parts under Apache-2.0. Every imported file keeps its
  notice, and each package needs a `PROVENANCE.md` recording the upstream repository, the
  commit SHA, the license, and every local edit — otherwise "how far have we drifted"
  stops having a mechanical answer, which is the property the whole `sources/`-is-frozen
  discipline exists to protect.
- **Scope.** Which packages, at which Pharo version. The ratchet's Tier 1 is the natural
  first import (SUnit first, since it turns "did the port work" from a judgement into a
  green bar), but that is a choice about what this project is, not a technical detail.
- **Size.** Pharo's kernel is a large body of source to take into the repository, and it is
  the point at which this stops being a Smalltalk-80 with extensions.

Until that decision is made, F's second gate stays unclaimed and the phase is honestly
described as *mechanisms done, corpus absent*. Running `st80 -primitives -profile <p>` over
a vendored Pharo package is the first thing to do the day it lands: it turns the port into
a number on day one.

### G — Kernel protocol and the first ratchet turns *(done, less the Pharo corpus)*

Four new packages in `lib/`: `Kernel-Protocol`, `Collections-Protocol`, `Streams-Protocol`,
`Strings-Protocol`. Roughly 120 methods, all extensions — `sources/` is untouched, as always.

`new` → `initialize` — **done, and the rule is about the chain, not the class.** When a class
a *package format* defined has `initialize` reachable in its loaded superclass chain and
**nothing between it and `Behavior` defines a class-side `new`**, the loader compiles
`^super new initialize` into it — the same 1983 idiom the ~34 classes that want
initialization already write by hand.

The chain part is load-bearing. Give the method to both a class and its subclass and the
subclass's `initialize` runs **twice**: the subclass's `super new` finds the superclass's
`new`, which sends `initialize`, which dispatches back down. That is precisely the
double-initialization a global `Behavior>>new` change would have caused, arrived at from the
other direction. It runs after the compile pass (until then there is no way to know what a
class says for itself) and after trait flattening (a trait can be where `initialize` comes
from), and every class it touches is named in one line of output.

**Three gaps in the existing system surfaced by writing library code against it**, each of
which would have bitten the first person to port anything:

- **`BlockContext` has no `numArgs`.** The field is there — `nargs`, in the 1983 class
  definition — but Xerox gave it no accessor, because the only thing that ever needed it was
  the interpreter, which reads the field directly. `BlockClosure` answers `numArgs`, so
  every piece of protocol written against "a block" broke the moment it met a Blue Book one.
- **`on:do:`, `ensure:` and `ifCurtailed:` existed only on `BlockClosure`.** So the exception
  system was reachable only from code compiled in the closure dialect: the 4,500 methods of
  the 1983 library could *signal* an `Error` — `Object>>error:` does, since Phase E — and
  could not catch one. The fix is a copy, word for word, because nothing in those three
  methods is about closures: the mechanism is a walk up the sender chain looking for a frame
  whose method declares primitive 199 or 198, and a frame is a frame whichever kind of block
  made it.
- **`BlockClosure` could not answer `fixTemps`,** which `SortedCollection>>sortBlock:` sends —
  so every `asSortedCollection:` in the image failed the moment its argument was a closure.
  For a closure the answer is the receiver: it copied its values when it was made, which is
  the whole difference between the two kinds of block.

**And one real compiler bug**, found the same way — by writing `| ch |` inside a `whileTrue:`:

> **Temporaries declared inside an *inlined* block did not parse at all.** They have nowhere
> of their own to live (the point of inlining is that there is no frame), so they are hoisted
> into the enclosing method's frame, which is what every Smalltalk that inlines these does.
>
> The subtle half is that hoisting must keep the **lexical extent**, and getting that wrong
> is silent. Two sibling inlined blocks each declaring `t` put two declarations named `t` in
> one scope; searching the declaration array backwards, **pass zero finds the first and pass
> one finds the second** — so everything is *computed* about one declaration and *emitted*
> about the other. If a real closure inside the first block captures `t`, pass zero makes the
> first `t` remote and pass one emits a plain frame access to the second: a wrong answer with
> nothing to see. A visibility cursor that advances on every declaration in **both** passes,
> and is saved and restored around an inlined block, makes the two passes agree and gives
> correct shadowing for free — `x ifTrue: [| t | t := 1. y ifTrue: [| t | t := 2]. ^t]`
> answers 1.

trace2 and trace3 stayed byte-identical through the compiler change, which is the only
evidence that would have been worth anything.

**SUnit — done, written here rather than imported**, since Pharo's is not vendored and SUnit
is small enough that writing it is cheaper than the provenance decision. `TestCase`,
`TestSuite`, `TestResult`, `TestFailure` in `lib/SUnit/`, with `lib/SUnit-Tests/` testing
them — including a fixture whose tests **fail and blow up on purpose**, because a runner that
quietly reports every failure as a pass is worse than no runner, and nothing but a deliberate
failure catches that. `st80 -bootstrap … -tests` runs every test in the image and exits
non-zero if any did not pass, which is what makes the ratchet a number a build script can
read.

The distinction SUnit rests on is between a **failure** — the test worked and the answer was
no — and an **error** — something nobody predicted broke. `TestFailure` is a subclass of
`Error`, so telling them apart depends entirely on the order the two handlers are nested in
`TestResult>>runCase:`; get it backwards and every failure is reported as an error, silently.

**Writing it found two more things**, and the first is serious:

- **`ensure:` did not run when an exception unwound past it.** `Exception>>return:` jumped
  straight to the handler's frame and discarded everything in between without looking at it,
  so `[[Error signal] ensure: [flag := true]] on: Error do: […]` answered the handler's value
  and never set the flag. A file left open, a lock left held, and nothing to say so — the
  exact failure mode `ensure:` exists to prevent, arriving through the one path nobody had
  tested. `ensure:` on a *normal* return worked, and so did the non-local-return path through
  `aboutToReturn:through:`, which is why it survived Phase E. The unwind walk now runs before
  the jump in `return:`, `retry` and `retryUsing:`, and `runUnwindBlock` disarms itself so a
  frame that more than one path passes through still runs its block exactly once.
  SUnit found it as *"tearDown did not run after a failing test"*.
- **The bootstrap never filled any class's `subclasses`.** `Behavior` has four instance
  variables — `superclass methodDict format subclasses` — and only three were being written,
  so `Object subclasses` answered an empty `Set` for every class in the image, and with it
  `allSubclasses`, `withAllSubclasses`, and everything that walks *down* the hierarchy rather
  than up. It answered **empty rather than failing**, which is why nothing had noticed: the
  same shape as the method dictionaries that were filled where the image does not look. It is
  wired now by sending `addSubclass:` rather than building `Set`s in C — Behavior's own method
  makes the Set, checks the relationship and hashes the entry the way the image hashes it,
  three things that would each have been a place to get it subtly wrong.

Then the ratchet proper: STON, Announcements, Zinc-Character-Encoding, and the Tier 1 kernel
packages — all of which wait on the same provenance decision Phase F's second gate does.

**Gate:** *half met, and the same half as Phase F.* SUnit's own suite passes — 12 tests, and
the whole image's suite is one command. The corpus of ~500 Pharo one-liners, extracted
mechanically from Pharo's own `>>>` doctest comments, **cannot be built until Pharo source is
vendored**; it remains the right oracle and the extractor is a small job the day it lands.
What stands in for it now is the C suite, which grew from 439 checks to 516 over this phase,
most of them one modern expression each.

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

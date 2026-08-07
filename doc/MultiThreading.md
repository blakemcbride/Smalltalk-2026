# Multi-Threading

How Smalltalk-2026 runs bytecodes on more than one CPU.

This is the description of the mechanism. The *contract* — what the change
does to the meaning of Smalltalk programs — is `doc/CONCURRENCY.md`, and it is
normative. Read that one before writing Smalltalk for this system; read this
one before changing the VM.

---

## The claim

Every production Smalltalk — Squeak, Pharo, Cuis, VisualWorks, GNU Smalltalk —
multiplexes its `Process` objects onto a single OS thread. `Process`,
`ProcessorScheduler` and `Semaphore` are all real, and they buy concurrency,
never parallelism. Here, several native threads each hold their own
interpreter registers and send messages into one shared object memory at the
same time.

That single sentence is the source of every design decision below. Nothing
about it is free: a shared mutable heap under real threads breaks reference
counting, breaks the Blue Book's implicit mutual exclusion, and turns two
previously trivial operations — freeing an object and growing a table — into
protocol problems.

---

## The thread map

```
Thread 0        The SDL pump.  Owns the window, the renderer, the texture and
                the event queue.  NEVER executes a Smalltalk bytecode.

Threads 1..N    Smalltalk workers, N = CPUs - 1.  Run bytecodes, allocate,
                poll safepoints.  NEVER call SDL video.
```

Thread 0 is not a worker, and that is not a stylistic choice:

- A worker parked in a garbage-collection safepoint at the moment the window
  server wants an answer deadlocks the compositor.
- `SDL_PumpEvents`, `SDL_WaitEvent`, `SDL_CreateRenderer` and
  `SDL_LockTexture` are documented main-thread-only.
- On macOS "main thread" means *the thread that entered `main()`*, because the
  Cocoa run loop is bound to it. That is not negotiable and no amount of
  care on our side changes it.

Workers reach thread 0 through `SDL_PushEvent`, which SDL documents as
thread-safe. Nothing goes the other way except through the object memory.

`ST_cpu_count()` reports the machine; `WORKER_start(0, ...)` reserves one core
for the pump and starts a worker on each of the rest.

---

## What is per-thread, and what is shared

| State | Where it lives |
|---|---|
| Interpreter registers — active context, IP, SP, receiver, method | `_Thread_local st_interp st_vm` |
| Which worker am I | `_Thread_local st_worker *current_worker` |
| Object table, object bodies, every Smalltalk object | shared, one copy |
| The safepoint flag | one atomic int, read by every worker |

`st_vm` being thread-local is the whole of what makes an interpreter
re-entrant across threads: `ST_interp_run` compiles to code that touches its
own thread's registers and the shared object memory, and nothing else.

The collector has to find every thread's stack, so a thread announces itself:

```c
void ST_interp_register(void);      /*  publishes &st_vm  */
void ST_interp_unregister(void);
```

These write a fixed table of atomic slots by compare-and-swap. The table is
deliberately **not** mutex-guarded. The collector reads it with every mutator
parked, but a thread on its way *out* of the pool is not parked — it has
stopped polling and is unregistering. A mutex there would let the collector
hold a lock while waiting for a thread that is blocked on it. Atomic slots
avoid the question entirely.

The root walk visits `vm->active_context` for every registered interpreter.
Missing one entry frees another thread's entire stack.

---

## The object memory under threads

### Why there is still an object table

Spur and every other modern VM dropped the object table for direct pointers,
because the indirection costs a load. Under threading the trade inverts:

| Operation | Object table | Direct pointers |
|---|---|---|
| `become:` | one swap of two table entries | stop-the-world heap-wide scan |
| Pinning for SDL / FFI | free — the entry *is* the identity | explicit pin lists |
| Compaction | move the body, update one entry | rewrite every referring slot |
| Per-object metadata | the entry is its natural home | widen every object header |
| Field access | one extra indirection | direct |

We pay one indirection per dereference to make identity mutation atomic. In a
system where a developer recompiles a class while eight threads are executing
its methods, that is architecture, not micro-optimization.

### The table itself

```c
extern st_atomic_ptr   *st_om_table;        /*  4M slots, allocated once  */
extern st_atomic_uint   st_om_table_limit;  /*  first index past the used range  */
```

Two properties matter, and both are threading requirements rather than
simplifications:

**The table is allocated at full size and never moves.** A growable table has
to be reallocated, and reallocation moves it — while other threads are indexing
it without a lock, since taking one on every field access would defeat the
point of having a table. Reserving the whole range costs eight bytes of
*address space* per possible object and nothing resident until a slot is
touched, because the pages arrive on first write. When four million objects is
no longer enough, the growth path is a segmented table: a fixed directory of
fixed-size chunks, so growth adds a chunk and never moves an existing one.

**The slots are atomic**, because they are written under `table_lock` but read
without it on every single dereference. Publishing a *new* entry is ordered by
the release store to `st_om_table_limit`. Slot *reuse* is not: handing a freed
index to the next allocation stores `NULL` and then a fresh header into a slot
another thread may be reading at that instant. A plain pointer there is a data
race, and the thread sanitizer reported it about one run in four.

The publication order is worth stating explicitly, because getting it backwards
is invisible in testing:

```
    claim an index  ->  store the header into the slot  ->  release the limit
```

A reader that sees the new limit is guaranteed by the release/acquire pair to
see the header behind it. `OM_is_object` loads the limit with acquire for
exactly this reason.

### Field access

```c
static inline st_oop ST_oop_load (const st_oop *slot);   /*  relaxed atomic  */
static inline void   ST_oop_store(st_oop *slot, st_oop);  /*  relaxed atomic  */
```

Relaxed is the correct strength, and the reasoning is the whole of what the
object memory promises under concurrency. What must never happen is a **torn**
value — half of one pointer and half of another, which would be neither object
and would send a message to nothing. A relaxed atomic promises exactly that
and nothing more. *Ordering between fields is the program's business*, and
`CONCURRENCY.md` says so in as many words.

So: the object memory never tears. Headers, table entries and an object's
class are always consistent. Two threads racing on the same instance variable
get one of the two values written, never a mixture — and no more than that is
guaranteed.

### `become:`

```c
void OM_swap_identities(st_oop a, st_oop b);
```

The bodies stay where they are; only the two table entries move. No reference
anywhere in the heap is found or rewritten, so no thread can observe a
half-completed `become:`. Reference counts belong to the identity rather than
to the body, so they are put back after the swap.

### The identity hash

`OM_identity_hash(p)` reads a per-object field assigned at allocation from an
atomic counter, and is stable across collection and across a snapshot. It has
to be, because the *image* hashes: `IdentityDictionary>>findKeyOrNil:` probes
from `key asOop \\ length + 1`. A method dictionary filled from slot zero is
perfectly good to the interpreter, which scans, and completely invisible to the
image, which hashes. Primitive 75 answers this value and nothing else may
compute it differently.

---

## Reference counting becomes a hint

This is the most consequential thing threading did to the object memory, and it
is worth stating plainly:

> **With a worker pool running, dropping the last reference does not free the
> object.**

`OM_decrease_ref` still maintains the count — acquire-release on the decrement,
so the thread that reaches zero sees every write another thread made before
releasing its own reference. But it reclaims nothing:

```c
    if (WORKER_count() == 0)
        OM_deallocate(p);
```

Another thread can be holding the same object pointer and be about to
dereference it. It has not *stored* a reference, so no count protects it, and a
thread that reclaimed the body would pull it out from under the reader. This is
the hazard that makes reference counting insufficient for a shared mutable heap
however carefully the counts are maintained — it is not a bug in the counting,
it is a property of counting.

So with threads running, reclamation is deferred to the next collection, which
happens at a safepoint where by construction nobody is reading. Reference
counting becomes *a hint about when collecting is worthwhile*, rather than the
mechanism that reclaims. The single-threaded builds keep the Blue Book's
immediate reclamation, because there the count is sufficient.

The corollary is the rule the bootstrap ignored for months:

> **A reference held only in C protects nothing.** The marking collector zeroes
> every count and recomputes it from the root walk. If your object is not
> visited, `OM_increase_ref` on it is a decoration.

---

## Safepoints

A collection cannot run while another thread is midway through a bytecode, so
every worker must reach a point where its registers are consistent and its
roots are known.

Three mechanisms exist. We use the first:

| Mechanism | Used by | Why not here |
|---|---|---|
| **Polled flag in the dispatch loop** | **this VM** | — |
| POSIX signals | Boehm, MPS | Windows has no signal-based thread suspension at all |
| `mprotect`ed guard page | HotSpot, OCaml | same Windows problem, plus it collides with debuggers and sanitizers |

Polling also wins on the merits. It costs almost nothing against the price of
interpreting a bytecode. And decisively, it yields **precise roots**: at a poll
we know exactly which context slots hold object pointers. Precise roots are
what make a moving, compacting collector possible later; conservative stack
scanning would foreclose that permanently.

The poll site is one line at the top of the dispatch loop, before the bytecode
is fetched, where the registers are known consistent:

```c
    WORKER_poll();
```

and the fast path is a single relaxed load:

```c
static inline void
WORKER_poll(void)
{
    if (ST_load_relaxed(&st_safepoint_requested) != 0)
        WORKER_poll_slow();
}
```

Polling once per bytecode bounds the time-to-safepoint: no bytecode sequence
can run indefinitely without passing one. `worker.h`'s design note describes
polling at sends and backward jumps, which is the cheapest placement that still
bounds it; the loop as written polls every bytecode, which is stricter, and
narrowing it is a Phase 9 measurement rather than a correctness question.

### The protocol

```
    requester                             worker
    ---------                             ------
    lock                                  ...running...
    st_safepoint_requested = 1            poll sees it
    wait until parked == needed           lock; at_safepoint = 1; parked++
                                          broadcast; wait for the flag to clear
    ...collect...                         (parked)
    st_safepoint_requested = 0            wakes; at_safepoint = 0; parked--
    broadcast                             ...running...
```

One mutex and two condition variables coordinate the whole thing, and the fast
path never touches any of them. A condition variable rather than a spin: a
collection can take a while, and burning a core to notice its end sooner is the
wrong trade on a machine whose entire purpose is to spend its cores on
Smalltalk.

`WORKER_at_safepoint(fn, user)` is the wrapper — request, run, release — and
`OM_collect()` is written in terms of it.

Two exclusions from the wait, both required for the protocol to *terminate*:

- **A worker that has finished** will never poll again, so waiting for it hangs
  forever.
- **The requester itself**, which is a worker in the common case — a collection
  is triggered by whoever ran out of room — and which plainly will not park
  while it is the one waiting. Counting yourself is a deadlock that stays
  hidden until a worker, rather than the main thread, asks for a safepoint.

### The bug that taught the rest of it

The exclusion for a finished worker and the requirement to wait for an
unstarted one are *opposite obligations*, and originally one flag carried both.

`WORKER_start` publishes `worker_count` before creating any thread, so the
requester walks every slot. Each worker then announced itself by setting its
own `running` flag as its first instruction. In between, a worker that had been
created and had not yet reached that instruction read `running == 0` — the same
thing a *finished* worker reads. So the requester skipped it, collected, and
that worker's first act was to allocate into the table being swept. The object
it made had no references yet, so the collector reclaimed it and handed its
table slot to the next allocation, which freed the body its creator was still
initialising.

Two lessons generalise, and both are now enforced in code:

1. **Not-yet-started and already-finished are opposite obligations, and one
   flag cannot carry both.** Workers have `running` *and* `exited`, and are
   marked running by `WORKER_start` *before* the thread is created. Counting an
   unstarted worker costs at most a short wait, because it will reach a poll —
   polling is what the interpreter loop does.

2. **Assert the invariant, not its consequences.** As a corrupted-memory
   symptom this reproduced about twice in twenty-five runs under ASAN, in a
   different place each time. `WORKER_unparked_count()` checks the property
   itself from inside the safepoint — *nobody else is running* — and catches
   the same bug on every single run.

`ST_SAFEPOINT_LOG=1` in the environment makes a stalled safepoint print which
workers it is still waiting for, once a second.

---

## Collection

`OM_collect()` runs entirely inside a safepoint. It is a mark-and-recount
collector, not a mark-and-sweep of a separate bit:

1. Zero every reference count in the table.
2. Visit the roots, incrementing as it goes; push newly-reached objects.
3. Drain the mark stack, following class pointers and pointer fields — and the
   header and literal frame of a `CompiledMethod`, which is flagged
   non-pointer because its body is bytecodes. Missing those frees every
   selector and global binding reachable only from a method.
4. Anything still at zero is unreachable: release it directly rather than
   through `OM_deallocate`, because the counts are now exact and decrementing
   its fields again would corrupt them.

The count is therefore *rebuilt*, which is why the root walk is the definition
of liveness and a C-held reference is not.

The roots are:

- the guaranteed object pointers (`nil` through `cannotInterpret`),
- **every registered interpreter's active context** — not just the collecting
  thread's,
- the display `Form` and the input `Semaphore`, which are genuinely held by C,
- the scheduler's pending-process nomination,
- the VM-state slots carried in the snapshot,
- whatever provider the embedder installed, which must chain to the
  bootstrap's.

`ST_GC_LOG=1` prints what each collection reclaimed.

---

## Allocation, and the two deadlocks that aren't

Allocation is where the shared state is hottest, and `table_lock` guards
exactly three things: the free chain, the used limit, and growth. It is
deliberately narrow, and two places deliberately drop it:

**Releasing an object's fields happens outside the lock**, because that recurses
back into `OM_deallocate` and would deadlock on a non-recursive mutex. Only the
bookkeeping is guarded.

**A collection triggered by a full table drops the lock first.** A collection
parks every other worker and then walks the table; holding the table lock across
that deadlocks against any worker whose route to its safepoint runs through an
allocation.

That second one was also a live bug, and its shape is worth keeping. There are
two ways to run out of room and only one of them was handled: a failed `calloc`
collected and retried, but a *full object table* simply gave up. A long run
died with all four million table entries in use and three hundred million words
of heap still free, having collected once in a hundred and seventy-four million
bytecodes. The desktop's own event loop allocates a context per iteration and
asks for nothing else — precisely the shape of program that exhausts the table
first.

---

## The atomics and threading discipline

`src/port/st_atomic.h` wraps C11 `<stdatomic.h>` behind our own names. Two
self-imposed restrictions keep every operation on the lock-free path on every
target, and sidestep MSVC's initially locking atomics:

1. Only naturally-aligned int-, bool- and pointer-sized types.
2. Never `_Atomic` on a struct.

**Memory ordering is always explicit.** There is no default-seq_cst shorthand,
on purpose: in the interpreter's hot path the difference between relaxed and
seq_cst is the difference between a load and a fence, and that belongs at the
call site where a reader can see it.

`src/port/st_port.h` is the thread shim — create, join, mutex, condition
variable, TLS, monotonic time. It is written over pthreads and Win32
**deliberately not** over C11 `<threads.h>`, because Apple ships `<threads.h>`
in no macOS SDK and macOS is a required target.

`_Thread_local` is preferred over TLS keys for hot per-worker state; the
dynamic keys exist only for state that needs a destructor.

---

## Where the green scheduler stands

Smalltalk's own `Process`, `ProcessorScheduler` and `Semaphore` are
implemented per Blue Book Chapter 29 in `src/sched/`, and they remain the green
scheduler: cooperative, one process running at a time, priority-preemptive
between priorities. That layer is what the image expects and what makes the
display interactive — the input process waits on a semaphore the VM signals
when SDL delivers an event.

The two layers meet like this today: **native parallelism is per-thread
interpreters over one object memory, not yet M:N multiplexing of green
processes over workers.** Each worker drives its own interpreter directly.
Every green-scheduler state change already goes through one of the operations
in `st_sched.h`, so there is a single place to make them atomic when the
multiplexing lands — the shape was chosen for that.

Scheduling a green process, meanwhile, taught its own lesson that threading
made lethal: **a process belongs to nobody at three points on its way to
running.** `removeFirstLinkOf:` unlinks it from the list it was waiting on,
the caller resumes it, and resuming stores it somewhere. The list was the only
thing holding it, so the instant it is unlinked its count is zero and the
collector is entitled to it. The rule now is that `SCHED_remove_first_link`
returns a *held* reference and every caller releases it only once something in
the object memory holds the process — and the pending nomination, which lives
in a C variable, is counted and visited by the root walk. `CONCURRENCY.md`
records all three variants of that bug.

---

## What is measured

There is no 1983 oracle for any of this — Xerox never ran Smalltalk on two
CPUs. The thread sanitizer is the judge:

```
$ make OM=mt TSAN=1 test
```

On a 32-CPU machine, today:

| Test | Result |
|---|---|
| `test_parallel` — 31 threads mutating one shared object memory, collections underneath | 398 checks, 0 failures, 0 races |
| `test_parallel_interp` — 31 threads interpreting the 36-class kernel | 11,160 expressions, every answer correct |
| `test_parallel_mvc` — 31 threads running the real 226-class library | 4,960 expressions, 3 collections, every answer correct |

The tests are built so that a fault *has* to show as a wrong answer rather than
as a hope that a sanitizer notices: every worker computes something only it can
check — `inject:into:` over an interval, `Interval>>collect:`, `Fraction`
arithmetic, `Rectangle>>area` — through ordinary library code. A torn field, a
lost reference count or a context freed under another thread arrives as
arithmetic that does not add up.

`test_parallel_mvc` matters most, because the claim the project rests on is not
that a toy image can be interpreted in parallel but that a Smalltalk-80 can:
226 classes, 4,521 methods, a `Display`, a window scheduler and a Browser's
worth of objects in the heap, with 31 threads inside them.

---

## What is not there yet

Stated plainly, because the surrounding documents describe the design and it is
easy to read a design as a report.

| | Status |
|---|---|
| Parallel bytecode execution over a shared heap | **working, measured, TSAN-clean** |
| Safepoints, parallel-safe collection, atomic `become:` | **working** |
| M:N multiplexing of green `Process`es over the worker pool, work stealing | not implemented — workers drive interpreters directly |
| `Mutex`, `Monitor`, `Promise`, `Processor>>#forkParallel:` | specified in `CONCURRENCY.md`, not implemented |
| Genuinely atomic `Semaphore>>#signal` / `#wait` | not yet. `SharedQueue` is present as the 1983 class and guards itself with `Semaphore>>critical:`, which is exactly as atomic as the semaphore underneath it — correct for green processes, not yet across workers |
| Per-thread allocation buffers (TLABs) | not implemented — every allocation takes `table_lock` briefly |
| Generational / parallel scavenge | not implemented — collection is a single-threaded mark-and-recount at a safepoint |
| The interactive desktop (`st80 -run`) | **runs single-threaded**: the main thread interleaves interpreter slices with the SDL pump. The worker pool is exercised by the parallel tests |
| A scaling benchmark showing speedup across cores | not written. Phase 7's exit criterion names one |

The first two rows are the hard part and they are done. The rest is
scheduling work built on top of a foundation that already holds.

---

## Where the code is

| File | What is in it |
|---|---|
| `src/sched/worker.h` / `worker.c` | the worker pool and the entire safepoint protocol |
| `src/port/st_port.h` / `st_port.c` | threads, mutexes, condition variables, TLS, time |
| `src/port/st_atomic.h` | atomics behind our own names, ordering always explicit |
| `src/om/om_mt.h` / `om_mt.c` | the 64-bit object table, atomic slots, counting, the collector |
| `src/interp/interp.c` | `_Thread_local st_vm`, the interpreter registry, the poll site, the root walk |
| `src/sched/st_sched.h` / `st_sched.c` | green `Process`, `Semaphore`, `ProcessorScheduler` |
| `src/gfx/display.c` | thread 0: the SDL pump, the event queue into the VM |
| `tests/unit/test_parallel*.c` | the three parallel tests above |
| `doc/CONCURRENCY.md` | **the contract.** Normative |

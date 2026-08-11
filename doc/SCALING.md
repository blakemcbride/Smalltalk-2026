# Does it scale?

`make bench` runs `tests/bench/bench_parallel.c`. It is not part of `make test`:
a scaling measurement takes minutes and wants a quiet machine, which is the
opposite of what a test suite wants.

It found eight bugs, and all eight are fixed. **Every kernel now scales, and Phase K's
gate is met**: mandelbrot 7.60× against a required 4.0×, intervals 3.18× against 3.0×.
Collection pauses, which limited everything else, are gone from these runs entirely —
not reduced, absent, because a worker no longer needs the world stopped to free
anything.

## What it measures, and why that way

Three kernels, because they measure different things:

| | |
|---|---|
| **arithmetic** | the control — an inlined `whileTrue:` of SmallInteger arithmetic. No block activated, so no context allocated; every value stored is a tagged integer, so no reference count touched. It exists to tell "the interpreter's loop" apart from "the object memory", which cannot be reasoned to from the other three |
| **mandelbrot** | heavy arithmetic, almost no allocation — the ceiling. Fixed point rather than `Float` on purpose: a `Float` here is a boxed object, so a floating-point Mandelbrot would be an allocation benchmark wearing a disguise. It was one anyway, via a single send of `not` — see below |
| **intervals** | pure interpretation — sends, blocks, one context per activation. What the interpreter costs when the arithmetic is trivial |
| **collections** | heavy allocation and collection. The case a shared heap makes hardest |

The **total work is fixed** and the workers divide it, so the numbers are speedups
rather than throughput at different sizes. Each kernel slices itself by asking the
VM which worker it is — `Processor activeWorkerIndex` and `workerCount`, primitives
243 and 244, which exist for exactly this.

And **every worker computes something only it can check**: the parts are summed and
compared against what one thread computed alone. A benchmark that is only timed
will happily report a beautiful speedup for work that came out wrong.

## The numbers, on 8 P-cores

```
kernel        workers         ms  speedup stopped ms  pauses  worst ms   answer
arithmetic          1      143.9    1.00x        0.0       0      0.00       ok
arithmetic          2       75.9    1.90x        0.0       0      0.00       ok
arithmetic          4       37.9    3.80x        0.0       0      0.00       ok
arithmetic          8       19.1    7.52x        0.0       0      0.00       ok
mandelbrot          1      339.0    1.00x        0.0       0      0.00       ok
mandelbrot          2      173.9    1.95x        0.0       0      0.00       ok
mandelbrot          4       87.5    3.87x        0.0       0      0.00       ok
mandelbrot          8       44.6    7.60x        0.0       0      0.00       ok
intervals           1      148.7    1.00x        0.0       0      0.00       ok
intervals           2       88.5    1.68x        0.0       0      0.00       ok
intervals           4       55.8    2.66x        0.0       0      0.00       ok
intervals           8       46.7    3.18x        0.0       0      0.00       ok
collections         1      668.1    1.00x        0.0       0      0.00       ok
collections         2      349.0    1.91x        0.0       0      0.00       ok
collections         4      179.7    3.72x        0.0       0      0.00       ok
collections         8       95.3    7.01x        0.0       0      0.00       ok
```

**Phase K's gate — mandelbrot ≥ 4.0× and intervals ≥ 3.0× at eight workers — is met**,
at 7.60× and 3.18×, with every answer checked against what one thread computed alone.
It was the exit criterion `doc/PLAN.md` set for Phase 7 and never reached.

`collections` deserves a note. It was named in the plan as "the one that will not scale
at first, and its number is the honest headline rather than the one to bury". It is
**7.01×** — the best of the four after arithmetic, on the kernel designed to be worst.

The `stopped ms` column is zero everywhere. That is not a tuned pause; it is the
consequence of a worker no longer needing the world stopped to reclaim anything.

### A note on measuring this at all

The machine these were taken on is not quiet, and one run in three is contaminated.
`arithmetic` is the canary: it allocates nothing and touches no shared line, so it
should read ~7.5× at eight workers. A run where it reads 5× is a run where something
else had the cores — discard it rather than believe it. This was very nearly recorded
as a regression once.

## What was actually wrong: reference counting on `true` and `false`

The first version of this file named reference counting on shared objects as the
prime suspect. The second declared that **refuted** on the strength of a control
kernel. The second was wrong, and the way it was wrong is the useful part.

The control was `arithmetic`: an inlined `whileTrue:` of SmallInteger arithmetic,
written to touch no reference count at all. It does not scale either, so reference
counting was eliminated.

**But a comparison answers a boolean, and `true` and `false` are objects.** Every
iteration of `[i < last] whileTrue:` pushes one and pops it — an atomic increment and
an atomic decrement, on one of two objects, from every core at once. The control
kernel exercised the suspected mechanism as hard as anything could. It disproved
nothing.

`perf c2c` settled it in one run: **99.81% of all cross-core stalls on a single cache
line**, at `OM_increase_ref_object` and `OM_decrease_ref_object`, offset `0x10` — the
`refcount` field of one object header.

### The fix

The guaranteed pointers — `nil`, `true`, `false`, the fixed classes and selectors —
are created once at bootstrap and never freed. **Nothing reference-counts them any
more.** The check is one comparison in the already-inlined fast path and touches no
memory.

It is safe because it is *symmetric*: neither the increment nor the decrement
happens, so a count that is never raised can never be lowered to zero and freed. The
sweep and `OM_deallocate` refuse to free anything in that range regardless of what
its count says, because for these objects the count no longer means anything — and a
sweep that believed a zero there would free `nil`.

| | before | after |
|---|---|---|
| cross-core HITM loads, 8 workers | 89,825,447 | 106,474 |
| cycles for the same work, 1 → 8 workers | 341e9 → 716e9 | 158.6e9 → 159.0e9 |
| `arithmetic` speedup at 8 | 2.17× | **7.55×** |

The cycle row is the one that matters: the same instructions now take the same cycles
at eight workers as at one. The per-core stall is gone rather than reduced.

## How it was found, and the two wrong turns

Worth recording, because the wrong turns cost more than the fix.

1. **Guessed** reference counting. Right, but unproven, and written down as a
   suspicion.
2. **Built a control kernel to test it** — and the control was wrong in exactly the
   way that made it look like a refutation. Recorded the refutation confidently.
3. Ruled out cache traffic, address translation, branch prediction, machine clears
   and object-table false sharing — all correctly, all irrelevant.
4. Established it was **in the process, not the machine**: eight independent
   processes got 2.80 IPC where eight threads got 1.50.
5. `perf c2c` named the line and the symbol in one run.

The lesson is not "guess better". It is that **a control kernel is a test of the
hypothesis only if you can say what it does not do** — and "touches no reference
count" was an assumption about generated code, not a fact that had been checked.

## Also fixed along the way

- An **allocator that gave up with four million table entries free**: `OM_collect`
  answers how many objects it freed, and both retry sites treated zero as "cannot
  allocate". Two workers collecting at once meant the second freed nothing and gave
  up. It produced a *wrong answer*, about once in a dozen runs.
- A **safepoint deadlock**: two workers requesting a stop-the-world at the same
  moment each counted the other as a thread that had to park. Unseen because the only
  test that collected under load collected from worker zero.
- The **object-table indirection** in the bytecode fetch, and the out-of-line tag
  test in the reference-count fast path: inlining the latter was worth 18% in serial.

## The pause was the collection after all, and the first measurement said otherwise

Collection pauses were the dominant remaining cost. Measuring them produced a confident
wrong answer first, and the way it was wrong is the most useful thing in this section.

`ST_COLLECT_LOG=1` times the phases of a collection. It said:

```
st80: collect 15134 entries, 14260 live: zero 0.0 ms, mark 0.4 ms, sweep 0.0 ms, freed 871
```

0.4 ms of work against a 74 ms pause — 180 times the work — so the cost had to be
**getting every worker to a safepoint**, and the next move was obviously to chase
workers that were slow to poll.

That was wrong, and one number gave it away: **`intervals` at ONE worker paused for
102 ms.** There is nobody to wait for. Splitting the pause into time-spent-waiting and
time-spent-in-the-collector settled it:

```
st80: safepoint 87.39 ms = 0.00 waiting for 0 worker(s) + 87.39 doing the work
```

Zero waiting. All of it inside the collector. The 15,134-entry log line was real, but it
came from the **bootstrap**, before the benchmark had allocated anything. The collections
that mattered happened mid-run and looked like this:

```
st80: collect 2989815 entries, 14638 live: zero 20.0 ms, mark 19.4 ms, sweep 18.9 ms
```

**Three million slots walked three times to find fourteen thousand objects.** The lesson
worth keeping is not about collectors: a log line sampled from the wrong phase of the run
refuted the right hypothesis, and the single-worker case — where the competing explanation
is *impossible*, not merely unlikely — is what broke the tie.

Two faults were hiding behind each other:

- `st_om_table_limit` was a **high-water mark that never came down**. The free list was
  working, so the peak was genuine; but every later collection walked to it anyway,
  for ever. The sweep now hands back trailing free entries and lowers the limit,
  rebuilding the free list beneath it.
- A collection happened only when the table was **completely full**, so the limit ran to
  its four-million ceiling before the collector ran once — which is where the three
  million came from. It now collects when the table must *grow* past twice what survived
  the previous collection. Reusing a free entry never triggers one, so a program in a
  steady state still never collects.

| | before | after |
|---|---|---|
| repeat collection | 59 ms (2,989,815 entries) | **0.4 ms** (15,132 entries) |
| mandelbrot, 1 worker | 754 ms | **538 ms** |
| intervals, 1 worker | 301 ms | **190 ms** |
| worst pause | 128 ms | **16 ms** |
| collections, 8 workers | 2.39× | **3.54×** |

The threshold's floor was measured, not guessed, and the result is counter-intuitive
enough to record. Collecting *less* often loses on both total pause and worst pause:

| floor | total stopped | pauses | worst |
|---|---|---|---|
| 64k entries | 458 ms | 56 | 23.6 ms |
| **128k entries** | **410 ms** | 135 | **12.6 ms** |
| 1M entries | 1310 ms | 27 | 84.3 ms |

Total collection time is supposed to be threshold-invariant — each collection costs
O(threshold) and happens every O(threshold) allocations. It is not, because **a table
that fits in cache is swept several times faster per entry than one that does not**. A
million entries is eight megabytes walked three times; 128k is one megabyte, and stays
resident.

A side effect worth knowing: because nothing had ever collected during the bootstrap,
every image ever written carried the garbage. Images are now **a tenth of the size**
(28.6 MB → 2.9 MB) with the same 4,521 methods, the same 15,599 live objects and the
same refcount sum — the 25 MB that went away were thousands of duplicate copies of
strings like `accessing untypeable characters`.

## What perf said, and the send that cost 7x

`mandelbrot` sat at 1.03x on eight cores with the collector no longer to blame. Rather
than name a fourth bottleneck by reasoning, `perf` was pointed at it — with one
correction that mattered: a benchmark run is about 60% bootstrap, so both the profile and
the counters have to be restricted to the kernel phase. `perf record -D` skips the
bootstrap; differencing two `ST_BENCH_STRESS` levels isolates it in `perf stat`.

Twenty kernel runs, differenced:

| | 1 worker | 8 workers |
|---|---|---|
| wall | 10.75 s | 11.35 s |
| cycles | 62.4e9 | **290.9e9** |
| instructions | 210.5e9 | 304.3e9 |
| IPC | 3.37 | **1.05** |
| context switches | 158 | **8,708,986** |

Eight cores burning 4.7x the cycles for 1.45x the instructions, at a third of the IPC,
switching context 767,000 times a second. The kernel-phase profile named it:

```
19.20%  native_queued_spin_lock_slowpath   [kernel]
 3.67%  futex_hash                         [kernel]
 3.01%  __GI___lll_lock_wait
 2.32%  __pthread_mutex_unlock_usercnt
 2.28%  futex_wait_setup                   [kernel]
 2.15%  pthread_mutex_lock
```

**A third of all cycles in mutex and futex machinery**, with `table_alloc_locked` in the
same profile — so the contended lock was the object table's, taken by every allocation.

Which left one question: what was a kernel documented as "almost no allocation"
allocating? Tallying by class answered it — **32 million MethodContexts, 94% of
everything**. Tallying activations by selector, and differencing *that* across stress
levels to separate bootstrap from kernel, named the send:

```smalltalk
[done not and: [n < limit]] whileTrue: [ ... ]
```

`not` is not one of the Blue Book's special selectors. It is a real send to
`Boolean>>not`, and **a real send builds a MethodContext** — one per inner iteration,
every one of them through a single global lock. An integer flag and `done < 1`, which
the compiler inlines, leaves the loop allocating nothing:

| workers | before | after |
|---|---|---|
| 1 | 538.5 ms | **352.8 ms** |
| 2 | 1.12× | **1.95×** |
| 4 | 1.11× | **3.82×** |
| 8 | 1.03× | **7.53×** |

**Phase K's mandelbrot gate — 4.0× at eight workers — is met at 7.53×.**

The bug is worth staring at, because it is invisible. `done not` is the idiomatic way to
write it. Nothing about it looks like an allocation, and the kernel's own comment boasts
of avoiding exactly this trap by choosing fixed point over `Float`.

## What was actually left: nothing could be freed without stopping the world

The magazine took the global lock off allocation and bought `intervals` 0.88× → 1.2×.
It should have bought more. Adding body recycling on top — keep a released entry's
allocation and reuse it, so malloc is never called — bought nothing at all, and
counting the hits said why:

```
reuse 0, too-small 2,693,309, empty 2,107,123
```

It never fired once. The reason was four lines at the end of `OM_decrease_ref_object`:

```c
if (WORKER_count() == 0)
    OM_deallocate(p);
```

**With a worker pool running, reference counting reclaimed nothing.** Every object a
worker dropped waited for a stop-the-world sweep, so the collector was not *a*
reclamation mechanism, it was the *only* one — and `intervals` retires 800,056 contexts
per run, two per element.

The deferral was there for a real reason: a thread can hold an object pointer it never
counted, freshly loaded from a field and not yet pushed, and be about to dereference it.
Freeing on a zero count pulls the body out from under it.

### The observation

That argument does not need a stopped world. It needs a **moment when the holder cannot
still be holding**, and there is one between every pair of bytecodes — which is exactly
the property the safepoint collector already depends on, since it frees everything
unreachable from precise roots at a poll point.

So the safety condition is *identical to one already load-bearing*. If a worker could
hold a raw pointer across a bytecode boundary and resurrect it, the existing sweep would
already be wrong. Epoch reclamation adds no hazard that was not there.

A zero count now **retires** the object. Each worker publishes the epoch it last saw at
a bytecode boundary, every 1024 bytecodes from the poll site the safepoint already uses.
The epoch advances when every worker has published the current one, and on reaching
epoch *g* the bucket retired at *g − 2* is nobody's to hold: its objects go into the
retiring worker's own magazine, without a lock and without stopping anyone. Three
buckets, the classic arrangement; a worker can never fall more than one epoch behind,
because advancing requires its publication.

| | before | after |
|---|---|---|
| intervals, 8 workers | 1.28× | **3.18×** |
| collections, 8 workers | 3.28× | **7.01×** |
| intervals, 1 worker | 211.2 ms | **148.7 ms** |
| collection pauses | 13.9% of profile | **none in these runs** |

Buckets are bounded at 4096 and overflow is not an error: the object stays unreachable
with a zero count and the next sweep takes it, which is precisely the old behaviour.
Degrading into what already worked is the right failure mode for this part of a memory
manager. The buckets are dropped at every collection too, which is what stops a sweep
and a bucket from both freeing the same entry.

## What is left

Nothing on this benchmark. The remaining scaling work is to find out whether these four
kernels were the right four — they are small, synthetic, and chosen before any of the
above was understood. Pharo's kernel, when it loads, is the real test.

## And the benchmark still divides work evenly

Which is wrong on a hybrid CPU — the E-cores set the wall time. Handing work out
dynamically would fix the measurement.


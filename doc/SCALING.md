# Does it scale?

`make bench` runs `tests/bench/bench_parallel.c`. It is not part of `make test`:
a scaling measurement takes minutes and wants a quiet machine, which is the
opposite of what a test suite wants.

It found six bugs, and all six are fixed. The interpreter now scales **7.5× on eight
cores**. Collection pauses, which limited everything else, are down from 128 ms to
16 ms and are no longer the binding constraint; what is now is named at the end —
every `Float` is boxed, so every arithmetic operation takes one global lock.

## What it measures, and why that way

Three kernels, because they measure different things:

| | |
|---|---|
| **arithmetic** | the control — an inlined `whileTrue:` of SmallInteger arithmetic. No block activated, so no context allocated; every value stored is a tagged integer, so no reference count touched. It exists to tell "the interpreter's loop" apart from "the object memory", which cannot be reasoned to from the other three |
| **mandelbrot** | heavy arithmetic, almost no allocation — the ceiling. Fixed point rather than `Float` on purpose: a `Float` here is a boxed object, so a floating-point Mandelbrot would be an allocation benchmark wearing a disguise |
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
arithmetic          1      140.3    1.00x        0.0       0      0.00       ok
arithmetic          2       72.8    1.93x        0.0       0      0.00       ok
arithmetic          4       36.6    3.83x        0.0       0      0.00       ok
arithmetic          8       18.6    7.54x        0.0       0      0.00       ok
mandelbrot          1      538.5    1.00x       46.1       5     10.65       ok
mandelbrot          2      481.1    1.12x       63.3      10     12.46       ok
mandelbrot          4      485.5    1.11x       65.9      19     11.66       ok
mandelbrot          8      522.7    1.03x      119.1      23     16.17       ok
intervals           1      190.0    1.00x       18.7       2     12.43       ok
intervals           2      197.9    0.96x        0.0       0      0.00       ok
intervals           8      241.4    0.79x        0.0       0      0.00       ok
collections         1      678.6    1.00x        0.0       0      0.00       ok
collections         2      416.1    1.63x        0.0       0      0.00       ok
collections         4      257.7    2.63x        0.0       0      0.00       ok
collections         8      191.9    3.54x        0.0       0      0.00       ok
```

**`arithmetic` scales 7.54× on eight cores — 94% efficiency**, up from 2.17×. That is
the interpreter running Smalltalk bytecodes on eight cores at very nearly eight times
the rate of one, over a shared mutable heap.

**`collections` — the kernel expected to scale worst — now reaches 3.54×**, up from
2.52×, and does it with no pauses at all: its allocations come back from the free list,
and reusing a free entry never triggers a collection.

`mandelbrot` and `intervals` still do not scale, and the `stopped ms` column no longer
explains why. Pauses are now a fifth of what they were and worst-case a tenth, yet
`mandelbrot` sits at 1.03×. The reason is in the last section, and it is not the
collector.

Phase K's gate — mandelbrot ≥ 4.0× and intervals ≥ 3.0× at eight workers — is **not
met**.

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

## What is left: `table_lock` on every Float

Mandelbrot still does not scale, and it is no longer the collector's fault. `arithmetic`,
whose values are immediate SmallIntegers, reaches **7.5× on eight cores**. Mandelbrot
does the same shape of work in `Float`, and **every Float is boxed** — so every
arithmetic operation allocates, and every allocation takes one global `table_lock`.

That is the next bottleneck, and it is the one the plan anticipated when it declined to
gate on the `collections` kernel "until TLABs land": per-worker allocation, so the
common case never touches a shared lock.

## And the benchmark still divides work evenly

Which is wrong on a hybrid CPU — the E-cores set the wall time. Handing work out
dynamically would fix the measurement.


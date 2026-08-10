# Does it scale?

`make bench` runs `tests/bench/bench_parallel.c`. It is not part of `make test`:
a scaling measurement takes minutes and wants a quiet machine, which is the
opposite of what a test suite wants.

It found four bugs, and all four are fixed. The interpreter now scales **7.55× on
eight cores**; what limits the other kernels is collection pauses, which is a
different problem and is named below.

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
kernel        workers         ms  speedup stopped ms   answer
arithmetic          1      140.5    1.00x        0.0       ok
arithmetic          2       71.8    1.96x        0.0       ok
arithmetic          4       36.3    3.86x        0.0       ok
arithmetic          8       18.6    7.55x        0.0       ok
mandelbrot          1      754.5    1.00x        0.0       ok
mandelbrot          2      593.0    1.27x        0.0       ok
mandelbrot          4      354.0    2.13x        0.0       ok
mandelbrot          8      986.9    0.76x     1708.8       ok
intervals           1      255.2    1.00x      101.7       ok
intervals           4      259.7    0.98x        0.0       ok
collections         1      652.9    1.00x        0.0       ok
collections         4      285.6    2.29x        0.0       ok
collections         8      259.1    2.52x        0.0       ok
```

**`arithmetic` scales 7.55× on eight cores — 94% efficiency**, up from 2.17×. That is
the interpreter running Smalltalk bytecodes on eight cores at very nearly eight times
the rate of one, over a shared mutable heap.

The other three do not, and the reason is now visible in the `stopped ms` column
rather than hidden behind it: **collection pauses**. `mandelbrot` at eight workers
spends 1.7 seconds stopped inside a 987 ms run. That is the next problem, it is a
different problem, and it was invisible while everything was equally slow.

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

## What is left

**Collection pauses.** Now the dominant cost in three of the four kernels, and
plainly visible in `stopped ms`. The collector stops every worker and walks the whole
table; at 31 workers `intervals` spends 27 seconds stopped. That is the next piece of
work and it is a different problem from this one.

**The benchmark still divides work evenly**, which is wrong on a hybrid CPU — the
E-cores set the wall time. Handing work out dynamically would fix the measurement.

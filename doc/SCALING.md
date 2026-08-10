# Does it scale?

`make bench` runs `tests/bench/bench_parallel.c`. It is not part of `make test`:
a scaling measurement takes minutes and wants a quiet machine, which is the
opposite of what a test suite wants.

**It currently fails, and the failure is the most important line in it.**

## What it measures, and why that way

Three kernels, because they measure different things:

| | |
|---|---|
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

## The numbers, 32 CPUs

```
kernel        workers         ms  speedup stopped ms   answer
mandelbrot          1     1033.3    1.00x        0.0       ok
mandelbrot          2      822.9    1.26x        0.0       ok
mandelbrot          4      762.9    1.35x        0.0       ok
mandelbrot          8     1123.4    0.92x      937.2       ok
mandelbrot         16      906.4    1.14x        0.0       ok
mandelbrot         31      936.6    1.10x        0.0       ok
intervals           1      414.3    1.00x      106.3       ok
intervals           2      418.0    0.99x        0.0       ok
intervals           4      437.0    0.95x        0.0       ok
intervals           8      488.9    0.85x        0.0       ok
intervals          16      576.4    0.72x        0.0       ok
intervals          31      848.6    0.49x      532.9       ok
collections         1      811.7    1.00x        0.0       ok
collections         2      546.9    1.48x        0.0       ok
collections         4      453.7    1.79x        0.0       ok
collections         8      457.8    1.77x        0.0       ok
collections        16      522.5    1.55x        0.0       ok
collections        31     1006.7    0.81x     1718.2    WRONG
```

**The gate is not met and is not close.** `doc/PLAN-PHARO.md` asks for mandelbrot
≥ 4.0× at 8 workers and intervals ≥ 3.0×. The measured figures are 0.92× and 0.85×.
`intervals` gets steadily *slower* with more workers.

## The two findings

### 1. `collections` at 31 workers computes the wrong answer

This is a correctness failure under concurrent allocation and collection, and it is
the reason the benchmark exits non-zero. It is not a timing artefact: the kernels
sum to a value one thread produced, and at 31 workers the sum is different.

It appears only at the widest setting and only in the kernel that allocates hardest,
which is where a lost object, a torn field or a miscounted reference would first
show. **Nothing should be built on top of the parallel runtime until this is
understood.** It is the next thing to work on, ahead of any optimisation.

### 2. The safepoint protocol deadlocked, and now does not

Found by this benchmark on its first complete run. Two workers could request a
stop-the-world at the same moment and wait for each other for ever: each counted the
other as a thread that still had to park, and neither could park, because both were
inside `WORKER_request_safepoint` rather than at a poll. Nothing timed out and
nothing crashed — the process simply stopped, with every worker still marked
running.

It went unseen because the only existing test that collected under load collected
from **worker zero and nowhere else**. This benchmark has every worker allocating,
which is the ordinary case, and two collections were wanted at once within seconds.

A request is now exclusive. A worker that loses the race does **not** queue behind
the winner — that is the same deadlock wearing a mutex — it parks for the winner's
safepoint like any other worker and asks again afterwards.

## Why the speedups are poor, as far as is known

The `stopped ms` column exists so this cannot be guessed at, and it already rules
things out: at most widths it is **zero**, so the world is not being stopped and the
collector is not the bottleneck. Whatever is serialising these kernels is doing it
while every worker is nominally running.

The prime suspect is reference counting on **shared** objects. Every field store
adjusts a count, and the counts of the objects every worker touches — `nil`, `true`,
`false`, the small literals, the class objects — live on cache lines that all 32
cores then fight over. That would produce exactly this shape: no safepoints, no
lock contention visible, and a per-worker slowdown that grows with the worker count.

It is a suspicion and not a measurement. Confirming it wants a profile, and the fix
— if it is right — is the *reorganize* that `doc/PLAN-PHARO.md`'s Phase L already
anticipates for reference counts under threads.

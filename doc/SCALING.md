# Does it scale?

`make bench` runs `tests/bench/bench_parallel.c`. It is not part of `make test`:
a scaling measurement takes minutes and wants a quiet machine, which is the
opposite of what a test suite wants.

It found two bugs on its first complete runs, and both are fixed. The scaling
numbers below are still poor and the gate is still not met — that part is
unfinished, and the file says what is known about why.

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

## The numbers, 32 CPUs

```
kernel        workers         ms  speedup stopped ms   answer
mandelbrot          1     1029.7    1.00x        0.0       ok
mandelbrot          2      877.6    1.17x        0.0       ok
mandelbrot          4      816.9    1.26x        0.0       ok
mandelbrot          8     1456.6    0.71x     2894.8       ok
mandelbrot         16      975.2    1.06x        0.0       ok
mandelbrot         31      987.7    1.04x        0.0       ok
intervals           1      419.0    1.00x      105.9       ok
intervals           2      402.2    1.04x        0.0       ok
intervals           4      492.8    0.85x        0.0       ok
intervals           8      553.6    0.76x        0.0       ok
intervals          16      618.9    0.68x        0.0       ok
intervals          31      851.1    0.49x      503.6       ok
collections         1      810.6    1.00x        0.0       ok
collections         2      559.2    1.45x        0.0       ok
collections         4      477.4    1.70x        0.0       ok
collections         8      499.4    1.62x        0.0       ok
collections        16      553.6    1.46x        0.0       ok
collections         31     819.2    0.99x      516.2       ok
```

**The gate is not met and is not close.** `doc/PLAN-PHARO.md` asks for mandelbrot
≥ 4.0× at 8 workers and intervals ≥ 3.0×. The measured figures are 0.71× and 0.76×.
`intervals` gets steadily *slower* with more workers.

## The three bugs it found

### 1. An allocator that gave up with four million entries free — FIXED

`collections` at 31 workers computed the **wrong answer**, about once in a dozen
runs. The total was short by exactly one worker's share, and that worker had
printed:

```
st80: out of memory activating a method: 1914321 words and 4177478 object
      table entries free
```

Not exhaustion. `OM_collect` answers *how many objects it freed*, and the allocator
treated zero as "cannot allocate". With one mutator that is the same thing. With
several it is not: two workers both find the table full, the first collects and frees
plenty, the second then collects and frees **nothing** — because the first already
did — and gives up, with the memory it needed sitting there free.

What a collection freed is not the question. Whether the retry succeeds is. Both
retry sites now collect and try again regardless of the answer. **Zero failures in
100 stress rounds of the case that was failing three times in forty.**

A wrong arithmetic answer was the only symptom that reached the surface. The message
went to stderr in the middle of a benchmark and would have been read as
"the machine ran out of memory".

### 2. The safepoint protocol deadlocked — FIXED

Two workers could request a stop-the-world at the same moment and wait for each other
for ever: each counted the other as a thread that still had to park, and neither could
park, because both were inside `WORKER_request_safepoint` rather than at a poll.
Nothing timed out and nothing crashed — the process simply stopped, with every worker
still marked running.

It went unseen because the only existing test that collected under load collected from
**worker zero and nowhere else**. This benchmark has every worker allocating, which is
the ordinary case, and two collections were wanted at once within seconds.

A request is exclusive now. A worker that loses the race does **not** queue behind the
winner — that is the same deadlock wearing a mutex — it parks for the winner's
safepoint like any other worker and asks again afterwards.

### 3. Two benchmarks that measured the wrong thing — FIXED

The first sizing ran for 36 ms and measured mostly the cost of starting thirty-one
threads; `intervals` came out slower on more workers, which was true of the
measurement and said nothing about the interpreter. The second overcorrected into
something whose **single-threaded reference run** had not finished after twenty
minutes. Both are recorded in the source so the next person does not repeat them.

## Why the speedups are poor: two causes found, one still open

The first version of this file named a prime suspect — reference counting on the
**shared** objects every worker touches, whose counts would sit on cache lines all
thirty-two cores fight over. Plausible, and **wrong**. What actually turned up was
three separate things, and only one of them is a bug in this system.

### The machine is not 32 equal cores

An **Intel i9-14900KF: 8 P-cores (16 threads) and 16 E-cores.** The benchmark
divides the work **evenly**, so on unequal cores the slowest worker sets the wall
time — and past eight workers, some of the work lands on an E-core. That is a
static-partitioning artefact, not a VM limit.

Pinning to one thread per P-core removes it completely:

```
                    all 32 CPUs        8 P-cores only
arithmetic   1      1.00x              1.00x
arithmetic   2      1.66x              1.61x
arithmetic   4      2.67x              2.57x
arithmetic   8      2.12x              2.62x
arithmetic  16      1.91x              2.58x
arithmetic  31      1.59x              2.59x
```

The *degradation* is gone. A benchmark that reports throughput on a hybrid machine
has to hand work out dynamically rather than slice it evenly; that is a fix to the
measurement, and it is not made yet.

### Reference counting was in the way, just not the way that was guessed

`OM_increase_ref` and `OM_decrease_ref` are the hottest pair of calls in the system:
every push, pop and field store goes through them, and for a **SmallInteger** — most
of what an interpreter moves — the entire job is to notice the tag bit and return.
They were out of line, so that cost a call and a return every time. A profile of a
loop doing nothing but SmallInteger arithmetic put `OM_increase_ref` at **six times
the cost of the whole interpreter loop calling it**.

The tag test is now inline and only a real object reaches the call. **The
single-worker `arithmetic` kernel went from 172 ms to 141 ms, 18% faster**, and the
1983 library still compiles byte-for-byte identically. It is a serial win: the
eight-worker time was 65.8 ms before and 64.7 ms after, so it does not touch the
ceiling below.

### The ceiling itself is still unexplained

On **eight distinct physical P-cores** — verified through
`topology/thread_siblings_list`, not assumed — the speedup plateaus at about 2.6×,
and `perf stat` says something specific about why:

| | 1 worker | 8 workers |
|---|---|---|
| instructions | 1.0742 × 10¹² | 1.0743 × 10¹² |
| cycles | 341 × 10⁹ | 716 × 10⁹ |
| IPC | 3.15 | **1.50** |
| cache-misses | 1.614 × 10⁹ | 1.608 × 10⁹ |
| LLC-load-misses | 382 × 10⁶ | 380 × 10⁶ |

**Identical instructions. Identical cache misses. Half the IPC.** The work is
exactly the same and the memory traffic is exactly the same; the cores are simply
stalling twice as much. That rules out coherence traffic and cache pressure along
with the reference-counting theory, and it is as far as the evidence goes today.

What remains shared in that loop is short — the method's bytecodes and the object
table, both read-only for this kernel, and the safepoint poll's one relaxed load per
bytecode. Annotating the stalls to an instruction is the next step, and it is a
measurement rather than an argument.

**What is honest to say today:** the parallel runtime is **correct** — the answers
check at every width, including a hundred stress rounds of the case that used to fail
— it is 18% faster in serial than it was, and it still does not go faster on eight
cores than on four. The gate is not met. Two causes are found and named; the third
is narrowed and open.

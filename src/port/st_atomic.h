/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Atomics, behind our own names.
 *
 *  Unlike <threads.h>, C11 <stdatomic.h> is in good shape everywhere we care
 *  about: gcc since 4.9, clang since 3.6 (including Apple's), MSVC since
 *  Visual Studio 2022 17.5.  We still wrap it so that a future MSVC edge or
 *  an older toolchain can be handled by swapping _Interlocked* intrinsics in
 *  here without touching a single call site.
 *
 *  Two self-imposed restrictions keep us on the lock-free path on every
 *  platform, and sidestep MSVC's initially locking-atomics-free support:
 *
 *      1.  Only naturally-aligned int-, bool- and pointer-sized types.
 *      2.  Never _Atomic on a struct.
 *
 *  Memory ordering is always explicit.  There is no default-seq_cst
 *  shorthand on purpose: in the interpreter's hot path the difference
 *  between relaxed and seq_cst is the difference between a load and a
 *  fence, and that should be visible at the call site.
 */

#ifndef ST_ATOMIC_H
#define ST_ATOMIC_H

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef atomic_int          st_atomic_int;
typedef atomic_uint         st_atomic_uint;
typedef atomic_bool         st_atomic_bool;
typedef atomic_uintptr_t    st_atomic_ptr;
typedef atomic_size_t       st_atomic_size;
/*  Wide enough for a nanosecond total that runs for years.  */
typedef _Atomic int64_t     st_atomic_i64;

/*  Loads  */

#define ST_load_relaxed(p)          atomic_load_explicit((p), memory_order_relaxed)
#define ST_load_acquire(p)          atomic_load_explicit((p), memory_order_acquire)
#define ST_load_seq(p)              atomic_load_explicit((p), memory_order_seq_cst)

/*  Stores  */

#define ST_store_relaxed(p, v)      atomic_store_explicit((p), (v), memory_order_relaxed)
#define ST_store_release(p, v)      atomic_store_explicit((p), (v), memory_order_release)
#define ST_store_seq(p, v)          atomic_store_explicit((p), (v), memory_order_seq_cst)

/*  Read-modify-write.  Each returns the value held before the operation.  */

#define ST_fetch_add_relaxed(p, v)  atomic_fetch_add_explicit((p), (v), memory_order_relaxed)
#define ST_fetch_add_acq_rel(p, v)  atomic_fetch_add_explicit((p), (v), memory_order_acq_rel)
#define ST_fetch_sub_relaxed(p, v)  atomic_fetch_sub_explicit((p), (v), memory_order_relaxed)
#define ST_fetch_sub_acq_rel(p, v)  atomic_fetch_sub_explicit((p), (v), memory_order_acq_rel)
#define ST_fetch_or_relaxed(p, v)   atomic_fetch_or_explicit((p), (v), memory_order_relaxed)
#define ST_fetch_and_relaxed(p, v)  atomic_fetch_and_explicit((p), (v), memory_order_relaxed)
#define ST_exchange_acq_rel(p, v)   atomic_exchange_explicit((p), (v), memory_order_acq_rel)

/*
 *  Compare and swap.  `expected` is a pointer; on failure it is updated with
 *  the value actually found, which is what makes the retry-loop idiom work:
 *
 *      uintptr_t old = ST_load_relaxed(&slot);
 *      do {
 *          new = derive(old);
 *      } while (!ST_cas_weak(&slot, &old, new));
 *
 *  Use the weak form in such a loop (it may fail spuriously but is cheaper
 *  on LL/SC machines like ARM), and the strong form when there is no loop.
 */

#define ST_cas_weak(p, expected, desired)                       \
    atomic_compare_exchange_weak_explicit((p), (expected), (desired),   \
                                          memory_order_acq_rel, memory_order_acquire)

#define ST_cas_strong(p, expected, desired)                     \
    atomic_compare_exchange_strong_explicit((p), (expected), (desired), \
                                            memory_order_acq_rel, memory_order_acquire)

/*  Fences  */

#define ST_fence_acquire()          atomic_thread_fence(memory_order_acquire)
#define ST_fence_release()          atomic_thread_fence(memory_order_release)
#define ST_fence_seq()              atomic_thread_fence(memory_order_seq_cst)

/*
 *  Spin hint for short contended waits.  Lowers power and helps the memory
 *  subsystem on hyperthreaded cores.  Purely advisory.
 */
#if defined(__x86_64__) || defined(__i386__)
#define ST_spin_hint()              __builtin_ia32_pause()
#elif defined(__aarch64__) || defined(__arm__)
#define ST_spin_hint()              __asm__ __volatile__("yield" ::: "memory")
#else
#define ST_spin_hint()              ((void) 0)
#endif

#ifdef __cplusplus
}
#endif

#endif  /*  ST_ATOMIC_H  */

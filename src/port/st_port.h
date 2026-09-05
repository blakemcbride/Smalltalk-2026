/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Portability layer: native threads, mutexes, condition variables,
 *  thread-local storage, and timing.
 *
 *  We deliberately do NOT use C11 <threads.h>.  Apple does not ship it in
 *  any macOS SDK, and macOS is a required target.  This is a thin shim over
 *  pthreads on POSIX and the Win32 API on Windows, in the same spirit as
 *  Dynace's CRITICALSECTION abstraction but covering threads and condition
 *  variables as well.
 *
 *  Convention: functions returning int return 0 on success and -1 on
 *  failure.  Functions that cannot fail return void.
 */

#ifndef ST_PORT_H
#define ST_PORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#define ST_WINDOWS      1
#else
#define ST_POSIX        1
#endif

#ifdef ST_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*  Threads  */

#ifdef ST_WINDOWS
typedef HANDLE          st_thread;
typedef DWORD           st_thread_id;
#else
typedef pthread_t       st_thread;
typedef pthread_t       st_thread_id;
#endif

/*
 *  Thread bodies take a void * and return nothing.  Smalltalk workers never
 *  return a value to C; they signal completion through the object memory.
 */
typedef void (*st_thread_fn)(void *arg);

int             ST_thread_create(st_thread *t, st_thread_fn fn, void *arg);
int             ST_thread_join(st_thread t);
int             ST_thread_detach(st_thread t);
st_thread_id    ST_thread_self(void);
int             ST_thread_id_equal(st_thread_id a, st_thread_id b);
void            ST_thread_yield(void);

/*
 *  Best effort; used only to make debuggers and profilers readable.  Names
 *  are silently truncated on platforms with a length limit (15 chars on
 *  Linux).  Never fails in a way the caller should care about.
 */
void            ST_thread_set_name(const char *name);

/*  Number of CPUs available for scheduling.  Never returns less than 1.  */
int             ST_cpu_count(void);

/*  Mutexes  */

#ifdef ST_WINDOWS
typedef SRWLOCK             st_mutex;
#else
typedef pthread_mutex_t     st_mutex;
#endif

int     ST_mutex_init(st_mutex *m);
void    ST_mutex_destroy(st_mutex *m);
void    ST_mutex_lock(st_mutex *m);
void    ST_mutex_unlock(st_mutex *m);
int     ST_mutex_trylock(st_mutex *m);      /*  0 if acquired, -1 if not  */

/*  Condition variables  */

#ifdef ST_WINDOWS
typedef CONDITION_VARIABLE  st_cond;
#else
typedef pthread_cond_t      st_cond;
#endif

int     ST_cond_init(st_cond *c);
void    ST_cond_destroy(st_cond *c);
void    ST_cond_wait(st_cond *c, st_mutex *m);
int     ST_cond_timedwait(st_cond *c, st_mutex *m, int64_t ns);   /*  -1 on timeout  */
void    ST_cond_signal(st_cond *c);
void    ST_cond_broadcast(st_cond *c);

/*  Thread-local storage
 *
 *  Prefer the _Thread_local keyword for hot per-worker state; it is
 *  supported by gcc, clang and MSVC and costs far less than a key lookup.
 *  These dynamic keys exist for state whose lifetime needs a destructor.
 */

#ifdef ST_WINDOWS
typedef DWORD           st_tls_key;
#else
typedef pthread_key_t   st_tls_key;
#endif

int     ST_tls_create(st_tls_key *k, void (*destructor)(void *));
void    ST_tls_delete(st_tls_key k);
void   *ST_tls_get(st_tls_key k);
int     ST_tls_set(st_tls_key k, void *value);

/*  Time  */

/*  Monotonic nanoseconds; suitable for intervals, not wall clock.  */
int64_t ST_time_monotonic_ns(void);

/*
 *  The Smalltalk millisecond clock, in its two widths.  One counter, read
 *  two ways, and the two agree modulo 2^30 by construction.
 *
 *  One counter because there is one clock.  Primitive 99
 *  (Time>>millisecondClockInto:) read the monotonic counter and primitive
 *  135 (Squeak's millisecondClockValue) read milliseconds since 1901, and
 *  both called themselves the millisecond clock -- so the image computed a
 *  Delay's resumption time on one and the VM's timer compared it against
 *  the other, eight hours apart.  Every delay was already in the past and
 *  fired at once, which looks exactly like a delay that works until
 *  something measures it.
 *
 *  Monotonic because every caller is timing an interval --
 *  millisecondsToRun:, millisecondsSince:, uptime, a socket deadline --
 *  and none wants a wall clock that an operator can wind backwards.  Dates
 *  come from ST_time_smalltalk_ms and primitive 240 instead.
 *
 *  ST_time_ms_clock is the Blue Book's width: thirty bits, wrapping every
 *  12.43 days, which is what primitive 99 stores into four bytes and what
 *  primitive 136's target time is compared against.  ST_time_ms_wide is
 *  the same count unmasked, and it is what primitive 135 now answers.
 *
 *  The second one exists because the first one wraps and the image did not
 *  know it.  A Delay whose resumption time straddled the wrap was compared
 *  `resumptionTime <= now' against a clock that had just gone back to
 *  zero: never true again, so its waiter was never resumed, and the timing
 *  process -- the highest priority process in the image -- re-armed, woke,
 *  found nothing due and re-armed for ever.  On one worker that is the
 *  whole image hung, every 12.43 days of machine uptime (Bugs4 CHRON-1).
 *  Time millisecondsToRun: answered a negative number across the same
 *  boundary, and every socket deadline in lib/Network was computed the
 *  same unreduced way.
 *
 *  The repair could have been modular arithmetic in a dozen places in the
 *  image, each of which had to remember; widening the clock instead
 *  retires the question.  A SmallInteger here is 62 bits, so the wide
 *  clock runs for 146 million years, and the only place the narrow one is
 *  still needed is the four-byte word primitive 136 takes -- which
 *  Delay>>armTimer masks at the moment of arming, where the modulus is
 *  local and visible.
 */
uint32_t ST_time_ms_clock(void);
int64_t  ST_time_ms_wide(void);

/*  Milliseconds since the Smalltalk-80 epoch (1 January 1901).  */
int64_t ST_time_smalltalk_ms(void);

/*
 *  Seconds from the Smalltalk-80 epoch (1 January 1901) to the Unix epoch
 *  (1 January 1970).  69 years = 25185 days, plus 17 leap days (1904 through
 *  1968), = 25202 days = 2177452800 seconds.  Note 1900 was not a leap year
 *  and in any case falls outside the range.  Here rather than in st_port.c
 *  because a file's modification time crosses the same gap.
 */
#define ST_EPOCH_OFFSET_SEC     INT64_C(2177452800)

void    ST_sleep_ns(int64_t ns);

/*  Files
 *
 *  The two operations a snapshot writer needs that C leaves to the
 *  platform.  ST_file_sync pushes a stream's buffered bytes through the
 *  kernel to the device, so that a rename which follows it never publishes
 *  a file whose tail is still in a cache.  ST_file_replace renames `from'
 *  over `to' in one step, replacing an existing `to': POSIX rename does
 *  that and Win32's does not, so the Windows half is MoveFileEx with
 *  MOVEFILE_REPLACE_EXISTING.  Both are here so that OM_image_save, which
 *  is what needs them, can be written once; see the note there on why a
 *  snapshot is never written in place (Bugs3 B10).
 */
int     ST_file_sync(FILE *f);
int     ST_file_replace(const char *from, const char *to);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_PORT_H  */

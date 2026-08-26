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
 *  The Smalltalk millisecond clock: free-running, monotonic, thirty bits.
 *
 *  One function because there is one clock.  Primitive 99
 *  (Time>>millisecondClockInto:) read the monotonic counter and primitive
 *  135 (Squeak's millisecondClockValue) read milliseconds since 1901, and
 *  both called themselves the millisecond clock -- so the image computed a
 *  Delay's resumption time on one and the VM's timer compared it against
 *  the other, eight hours apart.  Every delay was already in the past and
 *  fired at once, which looks exactly like a delay that works until
 *  something measures it.
 *
 *  Thirty bits because the Blue Book's is a SmallInteger and must roll
 *  over where the image expects; monotonic because every caller is timing
 *  an interval -- millisecondsToRun:, millisecondsSince:, uptime -- and
 *  none wants a wall clock that an operator can wind backwards.  Dates
 *  come from ST_time_smalltalk_ms and primitive 240 instead.
 */
uint32_t ST_time_ms_clock(void);

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

#ifdef __cplusplus
}
#endif

#endif  /*  ST_PORT_H  */

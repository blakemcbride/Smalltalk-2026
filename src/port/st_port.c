/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Portability layer implementation.  See st_port.h for the contract.
 */

#include "st_port.h"
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef ST_WINDOWS
#include <io.h>         /*  _commit and _fileno, for ST_file_sync  */
#endif

#include <time.h>

#ifdef ST_POSIX
#include <unistd.h>
/*
 *  sched_yield's own header.  Nothing here included it: glibc's
 *  <pthread.h> pulls it in, which is why this has always built, and
 *  that is a property of glibc rather than a promise POSIX makes.
 *  With -Werror=implicit-function-declaration in the warning set, a
 *  C library that does not do the same favour is not a warning on the
 *  first build of a new platform -- it is the end of it.
 */
#include <sched.h>
#include <fcntl.h>      /*  open, for ST_file_lock_for_life  */
#include <sys/file.h>   /*  flock, the same  */
#include <sys/mman.h>   /*  mmap and mprotect, for ST_vm_reserve  */
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS   MAP_ANON    /*  the older BSD spelling  */
#endif
#endif

/*  ST_EPOCH_OFFSET_SEC is in st_port.h: the file primitives want it too.  */

#define ST_NS_PER_SEC           INT64_C(1000000000)
#define ST_NS_PER_MS            INT64_C(1000000)

/*
 *  The platform thread entry points want a signature we do not.  Carry the
 *  real function and its argument across in a small heap closure; thread
 *  creation is rare enough that the allocation does not matter.
 */
typedef struct {
    st_thread_fn    fn;
    void           *arg;
} thread_start;

/*  ----------  Threads  ----------  */

#ifdef ST_WINDOWS

static DWORD WINAPI
thread_trampoline(LPVOID raw)
{
    thread_start   *start = (thread_start *) raw;
    st_thread_fn    fn    = start->fn;
    void           *arg   = start->arg;

    free(start);
    fn(arg);
    return 0;
}

int
ST_thread_create(st_thread *t, st_thread_fn fn, void *arg)
{
    thread_start   *start;
    HANDLE          h;

    if (!t || !fn)
        return -1;
    start = (thread_start *) malloc(sizeof *start);
    if (!start)
        return -1;
    start->fn  = fn;
    start->arg = arg;
    h = CreateThread(NULL, 0, thread_trampoline, start, 0, NULL);
    if (!h) {
        free(start);
        return -1;
    }
    *t = h;
    return 0;
}

int
ST_thread_join(st_thread t)
{
    if (WaitForSingleObject(t, INFINITE) != WAIT_OBJECT_0)
        return -1;
    CloseHandle(t);
    return 0;
}

int
ST_thread_detach(st_thread t)
{
    return CloseHandle(t) ? 0 : -1;
}

st_thread_id
ST_thread_self(void)
{
    return GetCurrentThreadId();
}

int
ST_thread_id_equal(st_thread_id a, st_thread_id b)
{
    return a == b;
}

void
ST_thread_yield(void)
{
    SwitchToThread();
}

void
ST_thread_set_name(const char *name)
{
    /*
     *  SetThreadDescription arrived in Windows 10 1607 and lives in
     *  kernel32.  Resolve it dynamically so we still run on older systems.
     */
    typedef HRESULT (WINAPI *set_desc_fn)(HANDLE, PCWSTR);
    static set_desc_fn  set_desc;
    static int          resolved;
    wchar_t             wide[64];
    int                 n;

    if (!name)
        return;
    if (!resolved) {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");

        if (k32)
            set_desc = (set_desc_fn) (void *) GetProcAddress(k32, "SetThreadDescription");
        resolved = 1;
    }
    if (!set_desc)
        return;
    n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide,
                            (int) (sizeof wide / sizeof wide[0]));
    if (n > 0)
        set_desc(GetCurrentThread(), wide);
}

int
ST_cpu_count(void)
{
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors < 1)
        return 1;
    return (int) si.dwNumberOfProcessors;
}

#else   /*  ST_POSIX  */

static void *
thread_trampoline(void *raw)
{
    thread_start   *start = (thread_start *) raw;
    st_thread_fn    fn    = start->fn;
    void           *arg   = start->arg;

    free(start);
    fn(arg);
    return NULL;
}

int
ST_thread_create(st_thread *t, st_thread_fn fn, void *arg)
{
    thread_start   *start;

    if (!t || !fn)
        return -1;
    start = (thread_start *) malloc(sizeof *start);
    if (!start)
        return -1;
    start->fn  = fn;
    start->arg = arg;
    if (pthread_create(t, NULL, thread_trampoline, start) != 0) {
        free(start);
        return -1;
    }
    return 0;
}

int
ST_thread_join(st_thread t)
{
    return pthread_join(t, NULL) == 0 ? 0 : -1;
}

int
ST_thread_detach(st_thread t)
{
    return pthread_detach(t) == 0 ? 0 : -1;
}

st_thread_id
ST_thread_self(void)
{
    return pthread_self();
}

int
ST_thread_id_equal(st_thread_id a, st_thread_id b)
{
    return pthread_equal(a, b) != 0;
}

void
ST_thread_yield(void)
{
    sched_yield();
}

void
ST_thread_set_name(const char *name)
{
    if (!name)
        return;
#if defined(__APPLE__)
    /*  Apple's variant names the calling thread and takes one argument.  */
    pthread_setname_np(name);
#elif defined(__linux__)
    /*  Linux caps the name at 16 bytes including the NUL.  */
    {
        char    buf[16];

        strncpy(buf, name, sizeof buf - 1);
        buf[sizeof buf - 1] = '\0';
        pthread_setname_np(pthread_self(), buf);
    }
#else
    (void) name;
#endif
}

int
ST_cpu_count(void)
{
    long    n;

#if defined(__linux__)
    /*
     *  What this process is ALLOWED to run on, not what the machine has.
     *
     *  The two differ whenever anybody says taskset, or a container sets a
     *  cpuset, or a benchmark pins itself to one thread per physical core.
     *  Sizing the worker pool from _SC_NPROCESSORS_ONLN there starts a
     *  thread per CPU the machine owns and then crowds them all onto the
     *  handful the process may use, which reads as a scaling failure and
     *  is a counting one.
     */
    {
        cpu_set_t   allowed;

        CPU_ZERO(&allowed);
        if (sched_getaffinity(0, sizeof allowed, &allowed) == 0) {
            int     count = CPU_COUNT(&allowed);

            if (count > 0)
                return count;
        }
    }
#endif
    n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1)
        return 1;
    return (int) n;
}

#endif  /*  ST_WINDOWS  */

/*  ----------  Mutexes  ----------  */

#ifdef ST_WINDOWS

int
ST_mutex_init(st_mutex *m)
{
    InitializeSRWLock(m);
    return 0;
}

void
ST_mutex_destroy(st_mutex *m)
{
    /*  An SRWLOCK owns no resources.  */
    (void) m;
}

void
ST_mutex_lock(st_mutex *m)
{
    AcquireSRWLockExclusive(m);
}

void
ST_mutex_unlock(st_mutex *m)
{
    ReleaseSRWLockExclusive(m);
}

int
ST_mutex_trylock(st_mutex *m)
{
    return TryAcquireSRWLockExclusive(m) ? 0 : -1;
}

#else

int
ST_mutex_init(st_mutex *m)
{
    return pthread_mutex_init(m, NULL) == 0 ? 0 : -1;
}

void
ST_mutex_destroy(st_mutex *m)
{
    pthread_mutex_destroy(m);
}

void
ST_mutex_lock(st_mutex *m)
{
    pthread_mutex_lock(m);
}

void
ST_mutex_unlock(st_mutex *m)
{
    pthread_mutex_unlock(m);
}

int
ST_mutex_trylock(st_mutex *m)
{
    return pthread_mutex_trylock(m) == 0 ? 0 : -1;
}

#endif

/*  ----------  Condition variables  ----------  */

#ifdef ST_WINDOWS

int
ST_cond_init(st_cond *c)
{
    InitializeConditionVariable(c);
    return 0;
}

void
ST_cond_destroy(st_cond *c)
{
    (void) c;
}

void
ST_cond_wait(st_cond *c, st_mutex *m)
{
    SleepConditionVariableSRW(c, m, INFINITE, 0);
}

/*
 *  Milliseconds rounded UP, and clamped below INFINITE (Bugs07 FILES-15),
 *  by the helper FILES-7 made for Sleep.  Rounded down, a wait of under a
 *  millisecond was a wait of none: SleepConditionVariableSRW(..., 0)
 *  returns at once, so the delay timer, whose last wait before a deadline
 *  is always the fraction left, went round its loop without sleeping
 *  until the deadline came -- a spin through the last millisecond of
 *  every Delay.  And a wait of more than 49.7 days was taken modulo 2^32
 *  milliseconds, a wake the timer's loop then had to recompute.
 */
int
ST_cond_timedwait(st_cond *c, st_mutex *m, int64_t ns)
{
    DWORD   ms = (DWORD) ST_sleep_ms_rounded_up(ns);

    if (SleepConditionVariableSRW(c, m, ms, 0))
        return 0;
    return -1;
}

void
ST_cond_signal(st_cond *c)
{
    WakeConditionVariable(c);
}

void
ST_cond_broadcast(st_cond *c)
{
    WakeAllConditionVariable(c);
}

#else

/*
 *  Every timed wait here is on the MONOTONIC clock, never the wall clock
 *  (Bugs07 FILES-9).
 *
 *  A timed wait is how long to wait, and the caller has already measured
 *  it on the monotonic clock: the delay timer works out what is left as
 *  `deadline - ST_time_monotonic_ns()'.  pthread_cond_timedwait takes an
 *  ABSOLUTE deadline, though, and with a default condattr that deadline is
 *  read on CLOCK_REALTIME -- so the wait was turned into a wall-clock time
 *  and the wall clock was then trusted to get there.  A wall clock stepped
 *  back while the timer waited (an NTP step after a resume from suspend, a
 *  VM restored from a snapshot, `date -s', somebody correcting a clock that
 *  ran fast) moved the deadline away by the size of the step: the Delay
 *  due next fired that much late, and every Delay behind it with it, since
 *  one timer serves them all.  A ten-minute step back stopped every
 *  timeout, retry pause and `Delay' in the image for ten minutes.
 *
 *  So the condition variable is made on CLOCK_MONOTONIC, and the deadline
 *  below is read on the same clock; the two must agree, which is why every
 *  st_cond is made here.  A deadline on one clock compared against the
 *  other is never out by a little: monotonic time counts from boot and the
 *  wall clock from 1970, so the wait would end at once, or not for decades.
 *
 *  macOS has no pthread_condattr_setclock.  It has a RELATIVE timed wait
 *  instead, pthread_cond_timedwait_relative_np, which hands the kernel an
 *  interval and never reads the wall clock at all, so there the condvar is
 *  made plainly and the wait is relative.
 */
int
ST_cond_init(st_cond *c)
{
#if defined(__APPLE__)
    return pthread_cond_init(c, NULL) == 0 ? 0 : -1;
#else
    pthread_condattr_t  attr;
    int                 rc;

    if (pthread_condattr_init(&attr) != 0)
        return -1;
    rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0
      && pthread_cond_init(c, &attr) == 0 ? 0 : -1;
    pthread_condattr_destroy(&attr);
    return rc;
#endif
}

void
ST_cond_destroy(st_cond *c)
{
    pthread_cond_destroy(c);
}

void
ST_cond_wait(st_cond *c, st_mutex *m)
{
    pthread_cond_wait(c, m);
}

int
ST_cond_timedwait(st_cond *c, st_mutex *m, int64_t ns)
{
#if defined(__APPLE__)
    struct timespec relative;

    if (ns < 0)
        ns = 0;
    relative.tv_sec  = (time_t) (ns / ST_NS_PER_SEC);
    relative.tv_nsec = (long) (ns % ST_NS_PER_SEC);
    return pthread_cond_timedwait_relative_np(c, m, &relative) == 0 ? 0 : -1;
#else
    struct timespec deadline;
    int64_t         total;

    /*
     *  pthread_cond_timedwait wants an absolute deadline, on the clock the
     *  condition variable was made with -- CLOCK_MONOTONIC, see
     *  ST_cond_init.  Convert here rather than making every caller think
     *  about it.
     */
    if (ns < 0)
        ns = 0;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    total = (int64_t) deadline.tv_nsec + ns;
    deadline.tv_sec  += (time_t) (total / ST_NS_PER_SEC);
    deadline.tv_nsec  = (long) (total % ST_NS_PER_SEC);
    return pthread_cond_timedwait(c, m, &deadline) == 0 ? 0 : -1;
#endif
}

void
ST_cond_signal(st_cond *c)
{
    pthread_cond_signal(c);
}

void
ST_cond_broadcast(st_cond *c)
{
    pthread_cond_broadcast(c);
}

#endif

/*  ----------  Thread-local storage  ----------  */

#ifdef ST_WINDOWS

/*
 *  Fiber-local storage rather than TlsAlloc: FLS is the only Win32 flavour
 *  that supports a destructor callback, which is what pthread_key_create
 *  gives us on the other side.
 */

int
ST_tls_create(st_tls_key *k, void (*destructor)(void *))
{
    DWORD   key = FlsAlloc((PFLS_CALLBACK_FUNCTION) destructor);

    if (key == FLS_OUT_OF_INDEXES)
        return -1;
    *k = key;
    return 0;
}

void
ST_tls_delete(st_tls_key k)
{
    FlsFree(k);
}

void *
ST_tls_get(st_tls_key k)
{
    return FlsGetValue(k);
}

int
ST_tls_set(st_tls_key k, void *value)
{
    return FlsSetValue(k, value) ? 0 : -1;
}

#else

int
ST_tls_create(st_tls_key *k, void (*destructor)(void *))
{
    return pthread_key_create(k, destructor) == 0 ? 0 : -1;
}

void
ST_tls_delete(st_tls_key k)
{
    pthread_key_delete(k);
}

void *
ST_tls_get(st_tls_key k)
{
    return pthread_getspecific(k);
}

int
ST_tls_set(st_tls_key k, void *value)
{
    return pthread_setspecific(k, value) == 0 ? 0 : -1;
}

#endif

/*  ----------  Time  ----------  */

#ifdef ST_WINDOWS

int64_t
ST_time_monotonic_ns(void)
{
    static LARGE_INTEGER    freq;
    LARGE_INTEGER           now;

    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    /*  Split the division to keep the multiply from overflowing.  */
    return (now.QuadPart / freq.QuadPart) * ST_NS_PER_SEC
         + ((now.QuadPart % freq.QuadPart) * ST_NS_PER_SEC) / freq.QuadPart;
}

int64_t
ST_time_smalltalk_ms(void)
{
    FILETIME        ft;
    ULARGE_INTEGER  u;
    int64_t         unix_ms;

    GetSystemTimeAsFileTime(&ft);
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /*  FILETIME counts 100ns ticks from 1 January 1601.  */
    unix_ms = (int64_t) (u.QuadPart / 10000) - INT64_C(11644473600000);
    return unix_ms + ST_EPOCH_OFFSET_SEC * 1000;
}

/*
 *  Sleep for ns nanoseconds -- really sleep, however short (Bugs07 FILES-7).
 *
 *  This was `Sleep(ns / 1000000)', and every sleep the scheduler takes is
 *  shorter than a millisecond: the idle slice is 0.1 ms, the waits for
 *  another worker 1 us, and the main thread's poll 0.1 ms.  Each one
 *  truncated to Sleep(0), which gives up the rest of the time slice to
 *  another ready thread and returns AT ONCE when there is none -- the
 *  normal state of an idle server.  So on Windows the idle loop polled
 *  flat out: every worker of a -serve pool with no requests held a core
 *  at a hundred percent, a single worker waiting on a Delay spun one for
 *  the length of the wait, and the hundred confirmations the scheduler
 *  takes as ten milliseconds of a genuinely still system before an
 *  all-idle verdict shrank to a hundred back-to-back loads.  nanosleep on
 *  the other platforms sleeps the 0.1 ms it is asked for.
 *
 *  A high-resolution waitable timer does the same here: due times are in
 *  100 ns units, and CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10
 *  1803 and later) makes it keep them rather than round them up to the
 *  15.6 ms system tick.  One per thread, made on that thread's first
 *  sleep and kept for its life -- the threads that sleep are the workers,
 *  the main thread and the timer and I/O threads, which live as long as
 *  the process, so the handle is not closed.  Where the flag is refused
 *  (older Windows) the thread remembers and sleeps with Sleep, rounded UP
 *  to whole milliseconds: a sleep longer than asked, at the tick's
 *  granularity, but a sleep, and an idle pool no longer spins.  The two
 *  roundings are ST_sleep_100ns_ticks and ST_sleep_ms_rounded_up, which
 *  are portable so that tests/unit/test_port.c checks them everywhere.
 */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION   0x00000002
#endif

static _Thread_local HANDLE sleep_timer;
static _Thread_local int    sleep_timer_refused;

void
ST_sleep_ns(int64_t ns)
{
    if (ns <= 0)
        return;
    if (sleep_timer == NULL && !sleep_timer_refused) {
        sleep_timer = CreateWaitableTimerExW(NULL, NULL,
                                             CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                             TIMER_ALL_ACCESS);
        if (sleep_timer == NULL)
            sleep_timer_refused = 1;
    }
    if (sleep_timer != NULL) {
        LARGE_INTEGER   due;

        /*  Negative: relative to now, in 100 ns units.  */
        due.QuadPart = -ST_sleep_100ns_ticks(ns);
        if (SetWaitableTimer(sleep_timer, &due, 0, NULL, NULL, FALSE)
         && WaitForSingleObject(sleep_timer, INFINITE) == WAIT_OBJECT_0)
            return;
    }
    Sleep(ST_sleep_ms_rounded_up(ns));
}

#else

int64_t
ST_time_monotonic_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * ST_NS_PER_SEC + (int64_t) ts.tv_nsec;
}

int64_t
ST_time_smalltalk_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return ((int64_t) ts.tv_sec + ST_EPOCH_OFFSET_SEC) * 1000
         + (int64_t) ts.tv_nsec / ST_NS_PER_MS;
}

void
ST_sleep_ns(int64_t ns)
{
    struct timespec req;

    if (ns <= 0)
        return;
    req.tv_sec  = (time_t) (ns / ST_NS_PER_SEC);
    req.tv_nsec = (long) (ns % ST_NS_PER_SEC);
    while (nanosleep(&req, &req) != 0 && errno == EINTR)
        ;
}

#endif

/*
 *  The two roundings Windows' ST_sleep_ns makes (Bugs07 FILES-7), here and
 *  not inside it so that they are compiled, and tested, everywhere.  Both
 *  round UP: a positive request is never a sleep of nothing, which is the
 *  whole of the fault they replace.  The milliseconds stop one short of
 *  0xFFFFFFFF, which Sleep reads as INFINITE.
 */
uint32_t
ST_sleep_ms_rounded_up(int64_t ns)
{
    int64_t ms;

    if (ns <= 0)
        return 0;
    ms = ns / ST_NS_PER_MS + (ns % ST_NS_PER_MS != 0);
    return ms >= INT64_C(0xFFFFFFFE) ? UINT32_C(0xFFFFFFFE) : (uint32_t) ms;
}

int64_t
ST_sleep_100ns_ticks(int64_t ns)
{
    if (ns <= 0)
        return 0;
    return ns / 100 + (ns % 100 != 0);
}

/*
 *  The offset from UTC of the local clock reading, from the two readings
 *  of one instant (Bugs07 KERNA-3).
 *
 *  Windows has no tm_gmtoff, and primitive 219's Windows branch took the
 *  offset as `now - mktime(&utc)' with utc.tm_isdst cleared, on the theory
 *  that the zero kept mktime from correcting for daylight saving a second
 *  time.  It does something else: tm_isdst 0 tells mktime that the
 *  broken-down time IS standard time (C11 7.27.2.3; Microsoft's mktime
 *  says the same), so whenever the host was in daylight saving the answer
 *  was the standard offset, an hour short -- New York -18000 where it is
 *  -14400 all summer, London 0 for 3600, Sydney 36000 for 39600 -- and
 *  Time now, Date today and every DateAndTime on a Windows machine in a
 *  daylight-saving zone ran an hour behind from spring to autumn, saying
 *  nothing.  Setting tm_isdst to -1 instead lets mktime guess, and it
 *  guesses wrong for the hour a transition repeats.
 *
 *  No guess is needed.  localtime and gmtime of the same time_t are two
 *  readings of one instant, and the offset is simply how far apart the
 *  readings are: the hours, minutes and seconds, plus a whole day when the
 *  local date is the day before or the day after.  They are never further
 *  apart than that -- no zone is more than fourteen hours from UTC -- and
 *  across a year's end tm_yday jumps from 364 or 365 back to 0, so there
 *  the years decide which way the day goes.
 */
long
ST_utc_offset_between(const struct tm *local, const struct tm *utc)
{
    long    days;

    if (local->tm_year != utc->tm_year)
        days = local->tm_year > utc->tm_year ? 1 : -1;
    else
        days = (long) local->tm_yday - (long) utc->tm_yday;
    return days * 86400L
         + (long) (local->tm_hour - utc->tm_hour) * 3600L
         + (long) (local->tm_min - utc->tm_min) * 60L
         + (long) (local->tm_sec - utc->tm_sec);
}

int
ST_local_utc_offset(long *offset)
{
    time_t      now = time(NULL);
    struct tm   local;
#ifdef ST_WINDOWS
    struct tm   utc;

    if (localtime_s(&local, &now) != 0 || gmtime_s(&utc, &now) != 0)
        return 0;
    *offset = ST_utc_offset_between(&local, &utc);
#else
    if (localtime_r(&now, &local) == NULL)
        return 0;
    *offset = (long) local.tm_gmtoff;
#endif
    return 1;
}

/*
 *  Bugs6 OM-4.  Freeing a body gives it back to the C allocator, not to
 *  the operating system: glibc keeps freed memory in its arena against
 *  the next request, and raises its mmap threshold every time a large
 *  mapped chunk is freed, so a hundred five-megabyte Strings that died
 *  were freed in every sense but the one `ps' reports.  malloc_trim walks
 *  the free lists and returns whole free pages with madvise, which is the
 *  only way an arena gives anything back from its middle.  Once per
 *  collection it is cheap beside the collection.
 */
void
ST_memory_trim(void)
{
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

/*  ----------  Reserved address space  ----------  */

static size_t
vm_page_size(void)
{
    static size_t   page;

    if (page == 0) {
#ifdef ST_WINDOWS
        SYSTEM_INFO info;

        GetSystemInfo(&info);
        page = (size_t) info.dwPageSize;
#else
        long        n = sysconf(_SC_PAGESIZE);

        page = n > 0 ? (size_t) n : 4096u;
#endif
    }
    return page;
}

static size_t
vm_round_up(size_t bytes)
{
    size_t  page = vm_page_size();

    if (bytes > SIZE_MAX - (page - 1))
        return 0;
    return (bytes + page - 1) / page * page;
}

void *
ST_vm_reserve(size_t bytes)
{
    size_t  total = vm_round_up(bytes);

    if (total == 0)
        return NULL;
#ifdef ST_WINDOWS
    return VirtualAlloc(NULL, total, MEM_RESERVE, PAGE_NOACCESS);
#else
    {
        /*
         *  PROT_NONE, so the range costs no commit charge and a stray
         *  access past what has been committed faults rather than reads
         *  zeroes; MAP_NORESERVE where it exists says the same to a kernel
         *  that would otherwise count it against overcommit.
         */
        int     flags = MAP_PRIVATE | MAP_ANONYMOUS;
        void   *base;

#ifdef MAP_NORESERVE
        flags |= MAP_NORESERVE;
#endif
        base = mmap(NULL, total, PROT_NONE, flags, -1, 0);
        return base == MAP_FAILED ? NULL : base;
    }
#endif
}

int
ST_vm_commit(void *base, size_t bytes)
{
    size_t  total = vm_round_up(bytes);

    if (!base || total == 0)
        return bytes == 0 ? 0 : -1;
#ifdef ST_WINDOWS
    return VirtualAlloc(base, total, MEM_COMMIT, PAGE_READWRITE) ? 0 : -1;
#else
    return mprotect(base, total, PROT_READ | PROT_WRITE) == 0 ? 0 : -1;
#endif
}

void
ST_vm_release(void *base, size_t bytes)
{
    if (!base)
        return;
#ifdef ST_WINDOWS
    (void) bytes;
    VirtualFree(base, 0, MEM_RELEASE);
#else
    (void) munmap(base, vm_round_up(bytes));
#endif
}

/*  Platform-independent: one clock for every caller.  See st_port.h.  */
uint32_t
ST_time_ms_clock(void)
{
    return (uint32_t) (ST_time_ms_wide() & 0x3FFFFFFF);
}

/*
 *  The same count without the mask, which is what primitive 135 answers.
 *
 *  Written in terms of the same nanosecond read as the narrow one, and the
 *  narrow one written in terms of THIS, so that the two cannot drift: an
 *  image that computes a resumption time on the wide clock and a VM that
 *  compares the arming target against the narrow one have to be reading
 *  one counter, or the disagreement is the eight-hour one all over again.
 */
int64_t
ST_time_ms_wide(void)
{
    return ST_time_monotonic_ns() / 1000000;
}

/*  ----------  Files  ----------  */

/*
 *  Both halves flush the stream first, because the stdio buffer is the
 *  one cache the kernel cannot see: syncing the descriptor under a stream
 *  with bytes still in its buffer syncs everything but the end of the
 *  file.
 */

#ifdef ST_WINDOWS

int
ST_file_sync(FILE *f)
{
    if (fflush(f) != 0)
        return -1;
    return _commit(_fileno(f)) == 0 ? 0 : -1;
}

int
ST_file_replace(const char *from, const char *to)
{
    /*
     *  MOVEFILE_REPLACE_EXISTING is the whole reason this is not rename():
     *  the C library's rename fails on Windows when the target exists, and
     *  the target existing is the normal case for a snapshot.
     *  WRITE_THROUGH makes the call return only once the move is on disk.
     */
    if (MoveFileExA(from, to,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 0;
    /*
     *  Win32 reports through GetLastError and leaves errno alone, and the
     *  caller prints strerror(errno) in its message; give it the nearest
     *  errno so the message says "permission denied" rather than
     *  "success".
     */
    switch (GetLastError()) {
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:   errno = EACCES; break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:      errno = ENOENT; break;
    case ERROR_NOT_SAME_DEVICE:     errno = EXDEV;  break;
    default:                        errno = EIO;    break;
    }
    return -1;
}

/*
 *  See st_port.h.  LockFileEx on one byte of a file of its own, through a
 *  handle that is never closed: a Windows lock belongs to the handle that
 *  took it, so a second taker -- another process, or another handle of
 *  this one -- is refused with ERROR_LOCK_VIOLATION, and the lock goes when
 *  the process does, however it ends.  The file is shared for reading,
 *  writing and deleting, so that a second st2026 can open it to be refused
 *  rather than failing to open it, and it is not the changes file because
 *  a Windows lock is mandatory: on the changes file it would refuse the
 *  image's own reads and writes through its other handles, and the handle
 *  holding it would make condenseChanges' replacing rename fail.  The
 *  handle is not inheritable (no security attributes), so a program the
 *  image starts does not keep the lock after st2026 has gone.
 */
int
ST_file_lock_for_life(const char *path)
{
    HANDLE      h;
    OVERLAPPED  ov;
    DWORD       error;

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : ENOENT;
        return -1;
    }
    memset(&ov, 0, sizeof ov);
    if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                   0, 1, 0, &ov))
        return 1;
    error = GetLastError();
    CloseHandle(h);
    if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING)
        return 0;
    errno = EIO;
    return -1;
}

#else

int
ST_file_sync(FILE *f)
{
    if (fflush(f) != 0)
        return -1;
    return fsync(fileno(f)) == 0 ? 0 : -1;
}

int
ST_file_replace(const char *from, const char *to)
{
    return rename(from, to) == 0 ? 0 : -1;
}

/*
 *  See st_port.h.  An flock, on a descriptor kept open for the life of the
 *  process: it belongs to the open file, so a second open of the same file
 *  -- in another process or this one -- is refused it, and close-on-exec
 *  keeps a program the image starts from holding it after st2026 is gone.
 */
int
ST_file_lock_for_life(const char *path)
{
    int     fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    int     saved;

    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
        return 1;
    saved = errno;
    close(fd);
    errno = saved;
    return saved == EWOULDBLOCK || saved == EAGAIN ? 0 : -1;
}

#endif

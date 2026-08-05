/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Portability layer implementation.  See st_port.h for the contract.
 */

#include "st_port.h"

#include <stdlib.h>
#include <string.h>

#ifdef ST_POSIX
#include <unistd.h>
#include <time.h>
#include <errno.h>
#endif

/*
 *  Seconds from the Smalltalk-80 epoch (1 January 1901) to the Unix epoch
 *  (1 January 1970).  69 years = 25185 days, plus 17 leap days (1904 through
 *  1968), = 25202 days = 2177452800 seconds.  Note 1900 was not a leap year
 *  and in any case falls outside the range.
 */
#define ST_EPOCH_OFFSET_SEC     INT64_C(2177452800)

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
    long    n = sysconf(_SC_NPROCESSORS_ONLN);

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

int
ST_cond_timedwait(st_cond *c, st_mutex *m, int64_t ns)
{
    DWORD   ms;

    if (ns < 0)
        ns = 0;
    ms = (DWORD) (ns / ST_NS_PER_MS);
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

int
ST_cond_init(st_cond *c)
{
    return pthread_cond_init(c, NULL) == 0 ? 0 : -1;
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
    struct timespec deadline;
    int64_t         total;

    /*
     *  pthread_cond_timedwait wants an absolute deadline, and with a default
     *  condattr that deadline is on CLOCK_REALTIME.  Convert here rather
     *  than making every caller think about it.
     */
    if (ns < 0)
        ns = 0;
    clock_gettime(CLOCK_REALTIME, &deadline);
    total = (int64_t) deadline.tv_nsec + ns;
    deadline.tv_sec  += (time_t) (total / ST_NS_PER_SEC);
    deadline.tv_nsec  = (long) (total % ST_NS_PER_SEC);
    return pthread_cond_timedwait(c, m, &deadline) == 0 ? 0 : -1;
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

void
ST_sleep_ns(int64_t ns)
{
    if (ns <= 0)
        return;
    Sleep((DWORD) (ns / ST_NS_PER_MS));
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

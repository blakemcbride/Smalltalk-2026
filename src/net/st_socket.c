/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  TCP sockets.  See st_socket.h for what this is and why it is shaped
 *  this way.
 */

#include "st_socket.h"
#include "st_port.h"
#include "st_atomic.h"
#include "worker.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ST_HAVE_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif

#ifdef ST_WINDOWS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>

typedef SOCKET          net_fd;
#define NET_FD_INVALID  INVALID_SOCKET
#define NET_FD_IS_VALID(fd)     ((fd) != INVALID_SOCKET)
#define net_close_fd(fd)        closesocket(fd)
#define net_errno()             WSAGetLastError()
#define NET_EWOULDBLOCK         WSAEWOULDBLOCK
#define NET_EINPROGRESS         WSAEWOULDBLOCK
#define NET_EINTR               WSAEINTR
#define NET_EBADF               WSAEBADF
typedef int             net_socklen;
typedef WSAPOLLFD       net_pollfd;
#define net_poll(fds, n, ms)    WSAPoll((fds), (ULONG) (n), (ms))

#else   /*  POSIX  */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif

typedef int             net_fd;
#define NET_FD_INVALID  (-1)
#define NET_FD_IS_VALID(fd)     ((fd) >= 0)
#define net_close_fd(fd)        close(fd)
#define net_errno()             errno
#define NET_EWOULDBLOCK         EWOULDBLOCK
#define NET_EINPROGRESS         EINPROGRESS
#define NET_EINTR               EINTR
#define NET_EBADF               EBADF
typedef socklen_t       net_socklen;
typedef struct pollfd   net_pollfd;
#define net_poll(fds, n, ms)    poll((fds), (nfds_t) (n), (ms))

#endif

/*  ----------  The table  ----------  */

#define NET_INDEX_BITS      12
#define NET_INDEX_MASK      ((1u << NET_INDEX_BITS) - 1u)

/*
 *  The index must FIT, or two slots share a handle and a socket answers
 *  for another.  Checked here rather than trusted, because raising
 *  NET_MAX_SOCKETS is described in the header as "a recompile and nothing
 *  else" and this is the one thing that stops being true past 4096.
 */
typedef char net_index_fits[(NET_MAX_SOCKETS <= (1 << NET_INDEX_BITS)) ? 1 : -1];

/*
 *  One socket.  Every field is read and written under net_lock; the only
 *  things read without it are the atomic mirrors below, which exist so
 *  that the scheduler's idle loop can ask "is anybody waiting?" ten
 *  thousand times a second without taking a lock the I/O thread holds
 *  while it rebuilds its set.
 */
typedef struct {
    net_fd          fd;
    uint32_t        generation;
    unsigned char   in_use;
    unsigned char   listening;
    unsigned char   want_read;      /*  one-shot interest, see NET_arm  */
    unsigned char   want_write;
    uintptr_t       read_token;
    uintptr_t       write_token;
    void           *ssl;            /*  the TLS state, or NULL: see NET_tls_start  */
} net_socket;

static net_socket       table[NET_MAX_SOCKETS];
static st_mutex         net_lock;

#ifdef ST_HAVE_TLS
/*
 *  One context for the process, made on the first TLS socket.  And one
 *  lock PER SLOT, outside the slot because NET_close wipes the slot: an
 *  SSL object is not safe to use from two threads at once, and the two
 *  threads that can meet on one are a process reading through it and
 *  another process closing it -- which the plain socket tolerates
 *  (recv on a closed descriptor is EBADF) and which SSL_free would make
 *  a use after free.  The lock is held for the length of one OpenSSL
 *  call, none of which blocks on a non-blocking descriptor.
 */
static SSL_CTX         *tls_ctx;
static st_mutex         tls_locks[NET_MAX_SOCKETS];
#endif

static st_atomic_int    net_armed;      /*  slots with want_read or want_write  */
static st_atomic_int    net_open;       /*  slots in use  */

/*
 *  0 fresh, 1 somebody is initialising, 2 ready.  A compare-and-swap and
 *  not `if (!ready) init', because two workers can open their first socket
 *  in the same microsecond and the second would initialise a mutex the
 *  first was already holding.  TSAN reported exactly that shape in the
 *  scheduler's async lock once; this does not repeat it.
 */
static st_atomic_int    net_state;

static uint32_t         next_generation;

static int            (*signal_hook)(uintptr_t token);

/*  The I/O thread.  */
static st_thread        io_thread;
static int              io_started;
static st_atomic_int    io_stopping;
static net_fd           wake_read  = NET_FD_INVALID;
static net_fd           wake_write = NET_FD_INVALID;

/*  What Smalltalk asks for after -serve <image>.  */
static int              argument_count;
static char           **argument_vector;

/*
 *  ST_NET_TRACE=1 in the environment narrates arms, deliveries and
 *  closes on standard error, the way ST_GC_LOG narrates collections.  A
 *  wake that never came is invisible from Smalltalk, and this is what
 *  shows whether the thread saw the socket at all.
 */
static int
tracing(void)
{
    static int  decided, on;

    if (!decided) {
        on = getenv("ST_NET_TRACE") != NULL;
        decided = 1;
    }
    return on;
}

/*  ----------  Errors  ----------  */

/*
 *  Per thread, because the failure a caller wants explained is the one
 *  their own call just had.  A shared last-error reports another worker's
 *  problem as yours, and only under load.
 */
static _Thread_local int    last_errno;
static _Thread_local char   last_text[256];

static void
set_error(int code, const char *what)
{
    last_errno = code;
#ifdef ST_WINDOWS
    {
        char    message[200];
        DWORD   n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
                                 | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   NULL, (DWORD) code, 0,
                                   message, sizeof message, NULL);

        while (n > 0 && (message[n - 1] == '\r' || message[n - 1] == '\n'))
            message[--n] = '\0';
        if (n == 0)
            snprintf(message, sizeof message, "error %d", code);
        snprintf(last_text, sizeof last_text, "%s: %s", what, message);
    }
#else
    snprintf(last_text, sizeof last_text, "%s: %s", what, strerror(code));
#endif
}

static void
set_error_text(const char *text)
{
    last_errno = NET_EBADF;
    snprintf(last_text, sizeof last_text, "%s", text);
}

const char *
NET_last_error(void)
{
    return last_text;
}

int
NET_last_errno(void)
{
    return last_errno;
}

/*  ----------  Handles  ----------  */

static int64_t
handle_of(uint32_t index, uint32_t generation)
{
    return (int64_t) (((uint64_t) generation << NET_INDEX_BITS) | index);
}

/*
 *  The slot a handle names, or NULL.  Called with net_lock held.  A handle
 *  from a previous life of this process -- one an image kept across a
 *  snapshot -- fails here on in_use and on the generation both, which is
 *  what makes such a handle safe to hold: it answers "no such socket"
 *  rather than somebody else's connection.
 */
static net_socket *
slot_for(int64_t handle)
{
    uint32_t    index;
    uint32_t    generation;

    if (handle < 0)
        return NULL;
    index      = (uint32_t) ((uint64_t) handle & NET_INDEX_MASK);
    generation = (uint32_t) ((uint64_t) handle >> NET_INDEX_BITS);
    if (index >= NET_MAX_SOCKETS)
        return NULL;
    if (!table[index].in_use || table[index].generation != generation)
        return NULL;
    return &table[index];
}

/*
 *  Claim a slot for a descriptor.  Called with net_lock held.  Answers the
 *  handle, or -1 with the descriptor still the caller's to close.
 */
static int64_t
claim_slot(net_fd fd, int listening)
{
    uint32_t    i;

    for (i = 0; i < NET_MAX_SOCKETS; ++i) {
        net_socket *s = &table[i];

        if (s->in_use)
            continue;
        if (++next_generation == 0)
            next_generation = 1;
        s->fd          = fd;
        s->generation  = next_generation;
        s->in_use      = 1;
        s->listening   = (unsigned char) listening;
        s->want_read   = 0;
        s->want_write  = 0;
        s->read_token  = 0;
        s->write_token = 0;
        ST_fetch_add_relaxed(&net_open, 1);
        return handle_of(i, s->generation);
    }
    set_error_text("no free socket slot: NET_MAX_SOCKETS are open");
    return -1;
}

/*  ----------  Descriptors  ----------  */

static int
set_nonblocking(net_fd fd)
{
#ifdef ST_WINDOWS
    u_long  on = 1;

    return ioctlsocket(fd, FIONBIO, &on) == 0 ? 0 : -1;
#else
    int     flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
#if defined(FD_CLOEXEC)
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    return 0;
#endif
}

/*
 *  A write to a peer that has gone raises SIGPIPE on POSIX, which kills
 *  the process.  Linux says so per call; macOS and the BSDs say so per
 *  socket; main.c ignores the signal as well, belt and braces, because a
 *  server dying on a client's disconnect is the one failure nobody
 *  forgives.
 */
static void
quiet_sigpipe(net_fd fd)
{
#if defined(SO_NOSIGPIPE)
    int on = 1;

    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const void *) &on, sizeof on);
#else
    (void) fd;
#endif
}

static int
send_flags(void)
{
#if defined(MSG_NOSIGNAL)
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

/*
 *  Resolve a name.  This is the call that blocks -- DNS, NSS, /etc/hosts --
 *  so the worker is parked for it, exactly as it is for a database call,
 *  and the arguments are C strings the caller has already copied out of
 *  the object memory.  errno is captured before WORKER_leave_native, which
 *  takes a mutex and may clobber it.
 */
static struct addrinfo *
resolve(const char *host, int port, int passive)
{
    struct addrinfo     hints;
    struct addrinfo    *result = NULL;
    char                service[16];
    int                 rc;
    int                 saved;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = passive ? AI_PASSIVE : 0;
    if (passive && host && host[0] == '\0')
        host = NULL;
    snprintf(service, sizeof service, "%d", port);

    WORKER_enter_native();
    rc    = getaddrinfo(host, service, &hints, &result);
    saved = net_errno();
    WORKER_leave_native();

    if (rc != 0) {
        last_errno = saved;
#ifdef ST_WINDOWS
        set_error(rc, "getaddrinfo");
#else
        snprintf(last_text, sizeof last_text, "getaddrinfo: %s",
                 rc == EAI_SYSTEM ? strerror(saved) : gai_strerror(rc));
#endif
        return NULL;
    }
    return result;
}

/*  ----------  Bringing it up  ----------  */

static int
make_wake_pipe(void)
{
#ifdef ST_WINDOWS
    /*
     *  WSAPoll cannot wait on a pipe, so the wake channel is a UDP socket
     *  bound to the loopback interface and sent to by itself.
     */
    struct sockaddr_in  addr;
    net_socklen         len = sizeof addr;
    net_fd              fd  = socket(AF_INET, SOCK_DGRAM, 0);

    if (!NET_FD_IS_VALID(fd))
        return -1;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    if (bind(fd, (struct sockaddr *) &addr, sizeof addr) != 0
     || getsockname(fd, (struct sockaddr *) &addr, &len) != 0
     || connect(fd, (struct sockaddr *) &addr, sizeof addr) != 0
     || set_nonblocking(fd) != 0) {
        closesocket(fd);
        return -1;
    }
    wake_read = wake_write = fd;
    return 0;
#else
    int     fds[2];

#if defined(__linux__)
    if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0)
        return -1;
#else
    if (pipe(fds) != 0)
        return -1;
    if (set_nonblocking(fds[0]) != 0 || set_nonblocking(fds[1]) != 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
#endif
    wake_read  = fds[0];
    wake_write = fds[1];
    return 0;
#endif
}

int
NET_init(int (*hook)(uintptr_t token))
{
    int expected = 0;

    if (ST_cas_strong(&net_state, &expected, 1)) {
#ifdef ST_WINDOWS
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            ST_store_release(&net_state, 0);
            return -1;
        }
#endif
        if (ST_mutex_init(&net_lock) != 0 || make_wake_pipe() != 0) {
            ST_store_release(&net_state, 0);
            return -1;
        }
        memset(table, 0, sizeof table);
        {
            uint32_t    i;

            for (i = 0; i < NET_MAX_SOCKETS; ++i) {
                table[i].fd = NET_FD_INVALID;
#ifdef ST_HAVE_TLS
                ST_mutex_init(&tls_locks[i]);
#endif
            }
        }
        /*
         *  The generation starts at the clock, so that a handle saved in an
         *  image yesterday does not happen to name a slot claimed today with
         *  the same small count.
         */
        next_generation = (uint32_t) (ST_time_smalltalk_ms() / 1000)
                        & 0x00FFFFFFu;
        signal_hook = hook;
        ST_store_seq(&io_stopping, 0);
        io_started = 0;
        ST_store_release(&net_state, 2);
        return 0;
    }
    while (ST_load_acquire(&net_state) == 1)
        ST_spin_hint();
    if (ST_load_acquire(&net_state) != 2)
        return -1;
    if (hook && !signal_hook)
        signal_hook = hook;
    return 0;
}

int
NET_available(void)
{
    return 1;
}

static int
ready(void)
{
    if (ST_load_acquire(&net_state) == 2)
        return 1;
    set_error_text("the socket subsystem is not initialised");
    return 0;
}

/*  ----------  The I/O thread  ----------  */

void
NET_wake(void)
{
    if (!NET_FD_IS_VALID(wake_write))
        return;
#ifdef ST_WINDOWS
    {
        char    byte = 1;

        send(wake_write, &byte, 1, 0);
    }
#else
    {
        char        byte = 1;
        ssize_t     n;

        /*  A full pipe means a wake is already owed; EAGAIN is fine.  */
        do {
            n = write(wake_write, &byte, 1);
        } while (n < 0 && errno == EINTR);
    }
#endif
}

static void
drain_wake(void)
{
    char    bytes[64];

#ifdef ST_WINDOWS
    while (recv(wake_read, bytes, sizeof bytes, 0) > 0)
        ;
#else
    while (read(wake_read, bytes, sizeof bytes) > 0)
        ;
#endif
}

/*
 *  Signal the token, and say whether the signal went.  Under net_lock, so
 *  that a slot cannot be freed -- and its token released -- between the
 *  moment the thread decides to signal and the moment it does.  The lock
 *  order is net_lock then the scheduler's async lock, and nothing anywhere
 *  takes them the other way round.
 */
static int
deliver(uintptr_t token)
{
    int queued;

    if (!token || !signal_hook)
        return 1;                       /*  nobody to tell: nothing owed  */
    queued = signal_hook(token) != 0;
    if (tracing())
        fprintf(stderr, "net: deliver token %lx: %s\n", (unsigned long) token,
                queued ? "queued" : "DROPPED");
    return queued;
}

static void
io_main(void *arg)
{
    /*
     *  Static rather than on the stack: NET_MAX_SOCKETS + 1 pollfds and
     *  their map are a few tens of kilobytes, and there is one I/O thread.
     */
    static net_pollfd   fds[NET_MAX_SOCKETS + 1];
    static struct { uint32_t index; uint32_t generation; } map[NET_MAX_SOCKETS + 1];
    int                 retry_owed = 0;

    (void) arg;
    ST_thread_set_name("st-net-io");

    for (;;) {
        unsigned    n = 0;
        unsigned    k;
        uint32_t    i;
        int         r;

        ST_mutex_lock(&net_lock);
        if (ST_load_relaxed(&io_stopping)) {
            ST_mutex_unlock(&net_lock);
            break;
        }
        fds[n].fd      = wake_read;
        fds[n].events  = POLLIN;
        fds[n].revents = 0;
        ++n;
        for (i = 0; i < NET_MAX_SOCKETS; ++i) {
            const net_socket   *s = &table[i];

            if (!s->in_use || (!s->want_read && !s->want_write))
                continue;
            fds[n].fd      = s->fd;
            fds[n].events  = (short) ((s->want_read  ? POLLIN  : 0)
                                    | (s->want_write ? POLLOUT : 0));
            fds[n].revents = 0;
            map[n].index      = i;
            map[n].generation = s->generation;
            ++n;
        }
        ST_mutex_unlock(&net_lock);

        /*
         *  Wait for ever, unless a signal was dropped last pass -- then look
         *  again in a millisecond, by which time some worker has drained
         *  the queue (every bytecode does).
         */
        if (tracing())
            fprintf(stderr, "net: polling %u socket(s)%s\n", n - 1,
                    retry_owed ? " with a retry owed" : "");
        r = net_poll(fds, n, retry_owed ? 1 : -1);
        if (tracing())
            fprintf(stderr, "net: poll answered %d%s\n", r,
                    (r > 0 && (fds[0].revents & POLLIN)) ? " (woken)" : "");
        if (r < 0) {
            if (net_errno() == NET_EINTR)
                continue;
            ST_sleep_ns(1000000);
            continue;
        }
        if (fds[0].revents & POLLIN)
            drain_wake();

        ST_mutex_lock(&net_lock);
        retry_owed = 0;
        for (k = 1; k < n; ++k) {
            net_socket *s;
            short       got = fds[k].revents;

            if (!got)
                continue;
            s = &table[map[k].index];
            /*
             *  Closed since the set was built -- and perhaps the descriptor
             *  number already reissued to another slot.  Attributing by the
             *  generation captured with the set is what makes that harmless.
             */
            if (!s->in_use || s->generation != map[k].generation) {
                if (tracing())
                    fprintf(stderr, "net: slot %u gone, revents %d ignored\n",
                            map[k].index, (int) got);
                continue;
            }
            if (tracing())
                fprintf(stderr, "net: slot %u revents %d want r%d w%d\n",
                        map[k].index, (int) got, s->want_read, s->want_write);
            if (s->want_read
             && (got & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) {
                if (deliver(s->read_token)) {
                    s->want_read = 0;
                    ST_fetch_sub_relaxed(&net_armed, 1);
                }  else
                    retry_owed = 1;
            }
            if (s->want_write
             && (got & (POLLOUT | POLLHUP | POLLERR | POLLNVAL))) {
                if (deliver(s->write_token)) {
                    s->want_write = 0;
                    ST_fetch_sub_relaxed(&net_armed, 1);
                }  else
                    retry_owed = 1;
            }
        }
        ST_mutex_unlock(&net_lock);
    }
}

/*  Called with net_lock held.  */
static void
start_io_thread_locked(void)
{
    if (io_started)
        return;
    if (ST_thread_create(&io_thread, io_main, NULL) != 0) {
        fprintf(stderr, "st80: cannot start the network I/O thread\n");
        return;
    }
    io_started = 1;
}

void
NET_shutdown(void)
{
    uint32_t    i;

    if (ST_load_acquire(&net_state) != 2)
        return;
    ST_store_seq(&io_stopping, 1);
    NET_wake();
    if (io_started) {
        ST_thread_join(io_thread);
        io_started = 0;
    }
    ST_mutex_lock(&net_lock);
    for (i = 0; i < NET_MAX_SOCKETS; ++i) {
#ifdef ST_HAVE_TLS
        if (table[i].ssl)
            SSL_free((SSL *) table[i].ssl);
        ST_mutex_destroy(&tls_locks[i]);
#endif
        if (table[i].in_use && NET_FD_IS_VALID(table[i].fd))
            net_close_fd(table[i].fd);
        memset(&table[i], 0, sizeof table[i]);
        table[i].fd = NET_FD_INVALID;
    }
#ifdef ST_HAVE_TLS
    if (tls_ctx) {
        SSL_CTX_free(tls_ctx);
        tls_ctx = NULL;
    }
#endif
    ST_store_seq(&net_armed, 0);
    ST_store_seq(&net_open, 0);
    ST_mutex_unlock(&net_lock);
    if (NET_FD_IS_VALID(wake_read))
        net_close_fd(wake_read);
#ifndef ST_WINDOWS
    if (NET_FD_IS_VALID(wake_write))
        net_close_fd(wake_write);
#endif
    wake_read = wake_write = NET_FD_INVALID;
    ST_mutex_destroy(&net_lock);
#ifdef ST_WINDOWS
    WSACleanup();
#endif
    ST_store_seq(&io_stopping, 0);
    ST_store_release(&net_state, 0);
}

/*  ----------  Listening and connecting  ----------  */

int64_t
NET_listen(const char *host, int port, int backlog)
{
    struct addrinfo    *info;
    struct addrinfo    *p;
    net_fd              fd = NET_FD_INVALID;
    int64_t             handle;
    int                 saved = 0;

    if (!ready())
        return -1;
    info = resolve(host, port, 1);
    if (!info)
        return -1;
    for (p = info; p; p = p->ai_next) {
        int on = 1;

        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (!NET_FD_IS_VALID(fd)) {
            saved = net_errno();
            continue;
        }
#ifdef ST_WINDOWS
        setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   (const char *) &on, sizeof on);
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *) &on, sizeof on);
#endif
        if (bind(fd, p->ai_addr, (net_socklen) p->ai_addrlen) == 0
         && listen(fd, backlog > 0 ? backlog : 128) == 0
         && set_nonblocking(fd) == 0)
            break;
        saved = net_errno();
        net_close_fd(fd);
        fd = NET_FD_INVALID;
    }
    freeaddrinfo(info);
    if (!NET_FD_IS_VALID(fd)) {
        set_error(saved, "listen");
        return -1;
    }
    ST_mutex_lock(&net_lock);
    handle = claim_slot(fd, 1);
    ST_mutex_unlock(&net_lock);
    if (handle < 0)
        net_close_fd(fd);
    return handle;
}

int64_t
NET_accept(int64_t listener)
{
    net_socket *s;
    net_fd      lfd;
    net_fd      fd;
    int64_t     handle;
    int         saved;

    if (!ready())
        return -1;
    ST_mutex_lock(&net_lock);
    s = slot_for(listener);
    if (!s || !s->listening) {
        ST_mutex_unlock(&net_lock);
        set_error_text("no such listening socket");
        return -1;
    }
    lfd = s->fd;
    ST_mutex_unlock(&net_lock);

    /*
     *  Outside the lock: accept cannot block on a non-blocking listener,
     *  but it is a system call, and the lock is the I/O thread's too.
     */
    do {
        fd    = accept(lfd, NULL, NULL);
        saved = net_errno();
    } while (!NET_FD_IS_VALID(fd) && saved == NET_EINTR);
    if (!NET_FD_IS_VALID(fd)) {
        if (saved == NET_EWOULDBLOCK
#if defined(EAGAIN) && EAGAIN != EWOULDBLOCK
         || saved == EAGAIN
#endif
        )
            return NET_WOULD_BLOCK;
        set_error(saved, "accept");
        return -1;
    }
    if (set_nonblocking(fd) != 0) {
        set_error(net_errno(), "accept: non-blocking");
        net_close_fd(fd);
        return -1;
    }
    quiet_sigpipe(fd);
    ST_mutex_lock(&net_lock);
    handle = claim_slot(fd, 0);
    ST_mutex_unlock(&net_lock);
    if (handle < 0)
        net_close_fd(fd);
    return handle;
}

int64_t
NET_connect(const char *host, int port, int address_index)
{
    struct addrinfo    *info;
    struct addrinfo    *chosen;
    net_fd              fd;
    int64_t             handle;
    int                 rc;
    int                 saved;

    if (!ready())
        return -1;
    info = resolve(host, port, 0);
    if (!info)
        return -1;
    /*
     *  The address asked for, by its place in the list the resolver
     *  answered.  A non-blocking connect learns of a refusal only later,
     *  from NET_connect_result, so trying the next address cannot happen
     *  here; it is the caller's loop -- Socket class>>connectTo:port: asks
     *  NET_address_count and comes back with the next index.  This used
     *  to take the first address only, on the grounds that nobody needed
     *  more; then `localhost' resolved to ::1 before 127.0.0.1 on a
     *  machine whose Ollama listened on 127.0.0.1 alone, and a server that
     *  was up was reported as refusing.
     */
    for (chosen = info; chosen && address_index > 0; chosen = chosen->ai_next)
        --address_index;
    if (!chosen) {
        set_error_text("no such address for the host");
        freeaddrinfo(info);
        return -1;
    }
    fd = socket(chosen->ai_family, chosen->ai_socktype, chosen->ai_protocol);
    if (!NET_FD_IS_VALID(fd)) {
        set_error(net_errno(), "socket");
        freeaddrinfo(info);
        return -1;
    }
    if (set_nonblocking(fd) != 0) {
        set_error(net_errno(), "connect: non-blocking");
        net_close_fd(fd);
        freeaddrinfo(info);
        return -1;
    }
    quiet_sigpipe(fd);
    rc    = connect(fd, chosen->ai_addr, (net_socklen) chosen->ai_addrlen);
    saved = net_errno();
    freeaddrinfo(info);
    if (rc != 0 && saved != NET_EINPROGRESS && saved != NET_EWOULDBLOCK) {
        set_error(saved, "connect");
        net_close_fd(fd);
        return -1;
    }
    ST_mutex_lock(&net_lock);
    handle = claim_slot(fd, 0);
    ST_mutex_unlock(&net_lock);
    if (handle < 0)
        net_close_fd(fd);
    return handle;
}

/*  How many addresses the name resolves to at that port: what
 *  Socket class>>connectTo:port: loops over.  -1 when it does not resolve,
 *  with the resolver's words in NET_last_error.  */
int
NET_address_count(const char *host, int port)
{
    struct addrinfo    *info;
    struct addrinfo    *p;
    int                 count = 0;

    if (!ready())
        return -1;
    info = resolve(host, port, 0);
    if (!info)
        return -1;
    for (p = info; p; p = p->ai_next)
        ++count;
    freeaddrinfo(info);
    return count;
}

int
NET_connect_result(int64_t handle)
{
    net_socket *s;
    net_fd      fd;
    int         error = 0;
    net_socklen len   = sizeof error;
    net_pollfd  pfd;

    if (!ready())
        return -1;
    ST_mutex_lock(&net_lock);
    s = slot_for(handle);
    if (!s) {
        ST_mutex_unlock(&net_lock);
        set_error_text("no such socket");
        return -1;
    }
    fd = s->fd;
    ST_mutex_unlock(&net_lock);

    /*  Still connecting?  A zero-timeout poll says whether it is writable.  */
    pfd.fd      = fd;
    pfd.events  = POLLOUT;
    pfd.revents = 0;
    if (net_poll(&pfd, 1, 0) == 0)
        return NET_WOULD_BLOCK;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &error, &len) != 0) {
        set_error(net_errno(), "connect");
        return -1;
    }
    if (error != 0) {
        set_error(error, "connect");
        return -1;
    }
    return 0;
}

/*  ----------  Reading and writing  ----------  */

static net_fd
fd_of(int64_t handle)
{
    net_socket *s;
    net_fd      fd;

    ST_mutex_lock(&net_lock);
    s  = slot_for(handle);
    fd = s ? s->fd : NET_FD_INVALID;
    ST_mutex_unlock(&net_lock);
    if (!NET_FD_IS_VALID(fd))
        set_error_text("no such socket");
    return fd;
}

/*  The descriptor and whether TLS is on it, in one look at the table.  */
static net_fd
fd_and_tls_of(int64_t handle, void **ssl)
{
    net_socket *s;
    net_fd      fd;

    ST_mutex_lock(&net_lock);
    s    = slot_for(handle);
    fd   = s ? s->fd : NET_FD_INVALID;
    *ssl = s ? s->ssl : NULL;
    ST_mutex_unlock(&net_lock);
    if (!NET_FD_IS_VALID(fd))
        set_error_text("no such socket");
    return fd;
}

#ifdef ST_HAVE_TLS

/*
 *  OpenSSL's words for what just failed, into the thread's last error.  A
 *  certificate that did not verify says why in the verify result, which
 *  is the useful half: "hostname mismatch" or "certificate has expired"
 *  against the library's one line, "certificate verify failed".
 */
static void
tls_set_error(const char *what, SSL *ssl)
{
    unsigned long   code = ERR_peek_last_error();
    const char     *reason;
    char            text[160];

    last_errno = NET_EBADF;
    if (ssl && SSL_get_verify_result(ssl) != X509_V_OK) {
        snprintf(last_text, sizeof last_text, "%s: certificate verify failed: %s",
                 what, X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
        ERR_clear_error();
        return;
    }
    if (code) {
        reason = ERR_reason_error_string(code);
        if (!reason) {
            ERR_error_string_n(code, text, sizeof text);
            reason = text;
        }
        snprintf(last_text, sizeof last_text, "%s: %s", what, reason);
    } else
        snprintf(last_text, sizeof last_text, "%s: TLS failed", what);
    ERR_clear_error();
}

/*
 *  The process's one context, made under net_lock the first time it is
 *  wanted: the system's certificate store, peer verification that cannot
 *  be turned off, nothing older than TLS 1.2.  Two modes matter to a
 *  non-blocking caller: a partial write is answered as one, the way send
 *  answers one, and a retried write may come from another address --
 *  the bytes cross in a per-thread scratch buffer and the process that
 *  retries may be on another thread by then.
 */
static SSL_CTX *
tls_context_locked(void)
{
    SSL_CTX *ctx;

    if (tls_ctx)
        return tls_ctx;
    ERR_clear_error();
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        tls_set_error("TLS context", NULL);
        return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        tls_set_error("the system's certificate store", NULL);
        SSL_CTX_free(ctx);
        return NULL;
    }
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    /*
     *  A peer that closes the connection without the TLS close notice --
     *  which is most HTTP servers after `Connection: close' -- is end of
     *  stream and not an error.  HTTP frames its own bodies, so a
     *  truncation is caught one layer up, by the length or the chunks.
     */
    SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE
                        | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    tls_ctx = ctx;
    return ctx;
}

static int
is_ip_literal(const char *name)
{
    struct in_addr  v4;
    struct in6_addr v6;

    return inet_pton(AF_INET, name, &v4) == 1 || inet_pton(AF_INET6, name, &v6) == 1;
}

/*
 *  Take the TLS state of a handle, held against a close from another
 *  thread: answers the slot's index with tls_locks[index] HELD and *ssl
 *  set, or -1 holding nothing.  Two looks at the table: the first finds
 *  the state; the second, under the per-slot lock, confirms the slot
 *  still holds it, because NET_close frees the state only under that same
 *  lock, and a caller that got there first keeps it until it is done.
 *  The lock order is tls_locks[i] then net_lock.  NET_close takes
 *  net_lock, lets it go, and only then takes tls_locks[i], so neither
 *  ever waits on the other while holding what the other wants.
 */
static int
tls_acquire(int64_t handle, SSL **ssl)
{
    net_socket *s;
    uint32_t    index;
    SSL        *found;

    ST_mutex_lock(&net_lock);
    s     = slot_for(handle);
    found = s ? (SSL *) s->ssl : NULL;
    ST_mutex_unlock(&net_lock);
    if (!found) {
        set_error_text(s ? "TLS is not started on this socket" : "no such socket");
        return -1;
    }
    index = (uint32_t) ((uint64_t) handle & NET_INDEX_MASK);
    ST_mutex_lock(&tls_locks[index]);
    ST_mutex_lock(&net_lock);
    s = slot_for(handle);
    if (!s || (SSL *) s->ssl != found) {
        ST_mutex_unlock(&net_lock);
        ST_mutex_unlock(&tls_locks[index]);
        set_error_text("no such socket");
        return -1;
    }
    ST_mutex_unlock(&net_lock);
    *ssl = found;
    return (int) index;
}

static void
tls_release(int index)
{
    ST_mutex_unlock(&tls_locks[index]);
}

enum { TLS_HANDSHAKING, TLS_READING, TLS_WRITING };

/*
 *  What an OpenSSL call that did not succeed means, as this file's
 *  answers.  Which "wait" is the obvious one depends on the call: a read
 *  waiting to read is NET_WOULD_BLOCK, a read waiting to WRITE is
 *  NET_WANT_WRITE, and the handshake, which is neither, always says which.
 */
static long
tls_outcome(SSL *ssl, int rc, int saved, int mode, const char *what)
{
    switch (SSL_get_error(ssl, rc)) {
    case SSL_ERROR_WANT_READ:
        ERR_clear_error();
        return mode == TLS_READING ? NET_WOULD_BLOCK : NET_WANT_READ;
    case SSL_ERROR_WANT_WRITE:
        ERR_clear_error();
        return mode == TLS_WRITING ? NET_WOULD_BLOCK : NET_WANT_WRITE;
    case SSL_ERROR_ZERO_RETURN:
        ERR_clear_error();
        if (mode == TLS_READING)
            return 0;                   /*  the close notice: end of stream  */
        set_error_text(mode == TLS_WRITING ? "send: the TLS connection was closed"
                                           : "TLS handshake: the connection was closed");
        return -1;
    case SSL_ERROR_SYSCALL:
        ERR_clear_error();
        if (saved == 0) {
            if (mode == TLS_READING)
                return 0;               /*  gone without a word: end of stream  */
            set_error_text(mode == TLS_WRITING ? "send: the connection was closed"
                                               : "TLS handshake: the connection was closed");
            return -1;
        }
        set_error(saved, what);
        return -1;
    default:
        tls_set_error(what, ssl);
        return -1;
    }
}

#endif  /*  ST_HAVE_TLS  */

static int
would_block(int code)
{
    if (code == NET_EWOULDBLOCK)
        return 1;
#if defined(EAGAIN) && EAGAIN != EWOULDBLOCK
    if (code == EAGAIN)
        return 1;
#endif
    return 0;
}

long
NET_recv(int64_t handle, void *buffer, size_t max)
{
    net_fd  fd;
    long    n;
    int     saved;
    void   *tls;

    if (!ready())
        return -1;
    fd = fd_and_tls_of(handle, &tls);
    if (!NET_FD_IS_VALID(fd))
        return -1;
#ifdef ST_HAVE_TLS
    if (tls) {
        SSL    *ssl;
        int     index = tls_acquire(handle, &ssl);
        size_t  got   = 0;
        int     ok;

        if (index < 0)
            return -1;
        ERR_clear_error();
        ok    = SSL_read_ex(ssl, buffer, max, &got);
        saved = net_errno();
        n     = ok ? (long) got : tls_outcome(ssl, 0, saved, TLS_READING, "recv");
        tls_release(index);
        return n;
    }
#endif
    do {
        n     = (long) recv(fd, (char *) buffer, (int) max, 0);
        saved = net_errno();
    } while (n < 0 && saved == NET_EINTR);
    if (n < 0) {
        if (would_block(saved))
            return NET_WOULD_BLOCK;
        set_error(saved, "recv");
        return -1;
    }
    return n;
}

long
NET_send(int64_t handle, const void *buffer, size_t count)
{
    net_fd  fd;
    long    n;
    int     saved;
    void   *tls;

    if (!ready())
        return -1;
    fd = fd_and_tls_of(handle, &tls);
    if (!NET_FD_IS_VALID(fd))
        return -1;
#ifdef ST_HAVE_TLS
    if (tls) {
        SSL    *ssl;
        int     index = tls_acquire(handle, &ssl);
        size_t  sent  = 0;
        int     ok;

        if (index < 0)
            return -1;
        ERR_clear_error();
        ok    = SSL_write_ex(ssl, buffer, count, &sent);
        saved = net_errno();
        n     = ok ? (long) sent : tls_outcome(ssl, 0, saved, TLS_WRITING, "send");
        tls_release(index);
        return n;
    }
#endif
    do {
        n     = (long) send(fd, (const char *) buffer, (int) count, send_flags());
        saved = net_errno();
    } while (n < 0 && saved == NET_EINTR);
    if (n < 0) {
        if (would_block(saved))
            return NET_WOULD_BLOCK;
        set_error(saved, "send");
        return -1;
    }
    return n;
}

int
NET_shutdown_write(int64_t handle)
{
    net_fd  fd;
    void   *tls;

    if (!ready())
        return -1;
    fd = fd_and_tls_of(handle, &tls);
    if (!NET_FD_IS_VALID(fd))
        return -1;
#ifdef ST_HAVE_TLS
    if (tls) {
        /*
         *  The TLS close notice first, so that the peer's library reads a
         *  proper end rather than a truncation.  Not waited for: a
         *  notice the kernel cannot take this instant is not worth a
         *  wait, and the FIN behind it says the same thing.
         */
        SSL    *ssl;
        int     index = tls_acquire(handle, &ssl);

        if (index < 0)
            return -1;
        ERR_clear_error();
        SSL_shutdown(ssl);
        ERR_clear_error();
        tls_release(index);
    }
#endif
#ifdef ST_WINDOWS
    if (shutdown(fd, SD_SEND) != 0) {
#else
    if (shutdown(fd, SHUT_WR) != 0) {
#endif
        set_error(net_errno(), "shutdown");
        return -1;
    }
    return 0;
}

int
NET_close(int64_t handle, uintptr_t *old_read, uintptr_t *old_write)
{
    net_socket *s;
    net_fd      fd;
    void       *tls;

    if (old_read)
        *old_read = 0;
    if (old_write)
        *old_write = 0;
    if (!ready())
        return -1;
    ST_mutex_lock(&net_lock);
    s = slot_for(handle);
    if (!s) {
        ST_mutex_unlock(&net_lock);
        set_error_text("no such socket");
        return -1;
    }
    if (tracing())
        fprintf(stderr, "net: close handle %lld (slot %u), armed r%d w%d\n",
                (long long) handle, (unsigned) ((uint64_t) handle & NET_INDEX_MASK),
                s->want_read, s->want_write);
    if (old_read)
        *old_read = s->read_token;
    if (old_write)
        *old_write = s->write_token;
    if (s->want_read)
        ST_fetch_sub_relaxed(&net_armed, 1);
    if (s->want_write)
        ST_fetch_sub_relaxed(&net_armed, 1);
    fd  = s->fd;
    tls = s->ssl;
    memset(s, 0, sizeof *s);
    s->fd = NET_FD_INVALID;
    ST_fetch_sub_relaxed(&net_open, 1);
    ST_mutex_unlock(&net_lock);
#ifdef ST_HAVE_TLS
    if (tls) {
        /*
         *  Under the slot's TLS lock, AFTER the slot is cleared: a reader
         *  inside SSL_read on another thread holds that lock until its
         *  call returns, and its next call finds no slot.  The close
         *  notice is sent if the kernel takes it now and not waited for.
         */
        uint32_t    index = (uint32_t) ((uint64_t) handle & NET_INDEX_MASK);

        ST_mutex_lock(&tls_locks[index]);
        ERR_clear_error();
        SSL_shutdown((SSL *) tls);
        ERR_clear_error();
        SSL_free((SSL *) tls);
        ST_mutex_unlock(&tls_locks[index]);
    }
#else
    (void) tls;
#endif
    /*
     *  Closed AFTER the slot is cleared, so that a number the kernel
     *  reissues in the same instant cannot be attributed to a slot that
     *  still claims it.
     *
     *  AND THE I/O THREAD IS WOKEN, which is not tidiness.  On Linux a
     *  descriptor that another thread is blocked in poll() on is not
     *  released by close(): poll holds the open file until it returns, so
     *  the socket stays open in the kernel, no FIN is sent, and the peer
     *  never reads end of stream.  The first version of this file assumed a
     *  closed descriptor would yield POLLNVAL and wake the thread by itself.
     *  It did not, and a server that had timed an idle connection out and
     *  closed it left the client waiting for a close that never arrived,
     *  because the server had been parked on that very socket and it was
     *  still in the set.  The wake makes the thread rebuild its set without
     *  this slot and return from poll, which is what actually closes the
     *  socket.
     */
    net_close_fd(fd);
    NET_wake();
    return 0;
}

/*  ----------  Waiting  ----------  */

int
NET_set_tokens(int64_t handle, uintptr_t read_token, uintptr_t write_token,
               uintptr_t *old_read, uintptr_t *old_write)
{
    net_socket *s;

    if (old_read)
        *old_read = 0;
    if (old_write)
        *old_write = 0;
    if (!ready())
        return -1;
    ST_mutex_lock(&net_lock);
    s = slot_for(handle);
    if (!s) {
        ST_mutex_unlock(&net_lock);
        set_error_text("no such socket");
        return -1;
    }
    if (old_read)
        *old_read = s->read_token;
    if (old_write)
        *old_write = s->write_token;
    s->read_token  = read_token;
    s->write_token = write_token;
    ST_mutex_unlock(&net_lock);
    return 0;
}

int
NET_arm(int64_t handle, int mask)
{
    net_socket *s;
    int         changed = 0;

    if (!ready())
        return -1;
    ST_mutex_lock(&net_lock);
    s = slot_for(handle);
    if (!s) {
        ST_mutex_unlock(&net_lock);
        set_error_text("no such socket");
        return -1;
    }
    if (tracing())
        fprintf(stderr, "net: arm handle %lld (slot %u) mask %d, was r%d w%d\n",
                (long long) handle, (unsigned) ((uint64_t) handle & NET_INDEX_MASK),
                mask, s->want_read, s->want_write);
    if ((mask & NET_ARM_READ) && !s->want_read) {
        s->want_read = 1;
        ST_fetch_add_relaxed(&net_armed, 1);
        changed = 1;
    }
    if ((mask & NET_ARM_WRITE) && !s->want_write) {
        s->want_write = 1;
        ST_fetch_add_relaxed(&net_armed, 1);
        changed = 1;
    }
    start_io_thread_locked();
    ST_mutex_unlock(&net_lock);
    if (changed)
        NET_wake();
    return 0;
}

/*
 *  Give an interest back.  See NET_disarm in the header for why a timed
 *  wait has to, and why calling it on an interest already spent is fine.
 */
int
NET_disarm(int64_t handle, int mask)
{
    net_socket *s;
    int         changed = 0;

    if (!ready())
        return -1;
    ST_mutex_lock(&net_lock);
    s = slot_for(handle);
    if (!s) {
        ST_mutex_unlock(&net_lock);
        set_error_text("no such socket");
        return -1;
    }
    if (tracing())
        fprintf(stderr, "net: disarm handle %lld (slot %u) mask %d,"
                        " was r%d w%d\n",
                (long long) handle,
                (unsigned) ((uint64_t) handle & NET_INDEX_MASK),
                mask, s->want_read, s->want_write);
    if ((mask & NET_ARM_READ) && s->want_read) {
        s->want_read = 0;
        ST_fetch_sub_relaxed(&net_armed, 1);
        changed = 1;
    }
    if ((mask & NET_ARM_WRITE) && s->want_write) {
        s->want_write = 0;
        ST_fetch_sub_relaxed(&net_armed, 1);
        changed = 1;
    }
    ST_mutex_unlock(&net_lock);
    /*
     *  Wake the poll thread so it rebuilds its set without this
     *  descriptor; leaving it there would be harmless but would keep
     *  waking the thread on a readiness nobody asked about.
     */
    if (changed)
        NET_wake();
    return 0;
}

int
NET_waits_pending(void)
{
    return ST_load_relaxed(&net_armed) > 0;
}

int
NET_open_count(void)
{
    return ST_load_relaxed(&net_open);
}

/*  ----------  Asking about a socket  ----------  */

int
NET_local_port(int64_t handle)
{
    net_fd                  fd;
    struct sockaddr_storage addr;
    net_socklen             len = sizeof addr;

    if (!ready())
        return -1;
    fd = fd_of(handle);
    if (!NET_FD_IS_VALID(fd))
        return -1;
    if (getsockname(fd, (struct sockaddr *) &addr, &len) != 0) {
        set_error(net_errno(), "getsockname");
        return -1;
    }
    if (addr.ss_family == AF_INET)
        return (int) ntohs(((struct sockaddr_in *) &addr)->sin_port);
    if (addr.ss_family == AF_INET6)
        return (int) ntohs(((struct sockaddr_in6 *) &addr)->sin6_port);
    set_error_text("getsockname: not an internet socket");
    return -1;
}

int
NET_peer_address(int64_t handle, char *out, size_t max)
{
    net_fd                  fd;
    struct sockaddr_storage addr;
    net_socklen             len = sizeof addr;

    if (max)
        out[0] = '\0';
    if (!ready())
        return -1;
    fd = fd_of(handle);
    if (!NET_FD_IS_VALID(fd))
        return -1;
    if (getpeername(fd, (struct sockaddr *) &addr, &len) != 0) {
        set_error(net_errno(), "getpeername");
        return -1;
    }
    /*  Numeric only: no name lookup, so nothing here can block.  */
    if (getnameinfo((struct sockaddr *) &addr, len, out, (net_socklen) max,
                    NULL, 0, NI_NUMERICHOST) != 0) {
        set_error(net_errno(), "getnameinfo");
        return -1;
    }
    return 0;
}

int
NET_set_option(int64_t handle, int option, int value)
{
    net_fd  fd;
    int     on = value ? 1 : 0;
    int     rc;

    if (!ready())
        return -1;
    fd = fd_of(handle);
    if (!NET_FD_IS_VALID(fd))
        return -1;
    switch (option) {
    case NET_OPTION_NODELAY:
        rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *) &on,
                        sizeof on);
        break;
    case NET_OPTION_KEEPALIVE:
        rc = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *) &on,
                        sizeof on);
        break;
    default:
        set_error_text("no such socket option");
        return -1;
    }
    if (rc != 0) {
        set_error(net_errno(), "setsockopt");
        return -1;
    }
    return 0;
}

/*  ----------  For the collector  ----------  */

void
NET_visit_tokens(void (*visit)(uintptr_t token, void *user), void *user)
{
    uint32_t    i;

    if (ST_load_acquire(&net_state) != 2)
        return;
    ST_mutex_lock(&net_lock);
    for (i = 0; i < NET_MAX_SOCKETS; ++i) {
        if (!table[i].in_use)
            continue;
        if (table[i].read_token)
            visit(table[i].read_token, user);
        if (table[i].write_token)
            visit(table[i].write_token, user);
    }
    ST_mutex_unlock(&net_lock);
}

int
NET_holds_token(uintptr_t token)
{
    uint32_t    i;
    int         found = 0;

    if (!token || ST_load_acquire(&net_state) != 2)
        return 0;
    ST_mutex_lock(&net_lock);
    for (i = 0; i < NET_MAX_SOCKETS && !found; ++i)
        if (table[i].in_use
         && (table[i].read_token == token || table[i].write_token == token))
            found = 1;
    ST_mutex_unlock(&net_lock);
    return found;
}

/*  ----------  TLS  ----------  */

int
NET_tls_available(void)
{
#ifdef ST_HAVE_TLS
    return 1;
#else
    return 0;
#endif
}

#ifdef ST_HAVE_TLS

int
NET_tls_start(int64_t handle, const char *hostname)
{
    net_socket *s;
    SSL_CTX    *ctx;
    SSL        *ssl;
    char        name[256];
    size_t      n;
    int         ok;

    if (!ready())
        return -1;
    if (!hostname || !hostname[0]) {
        set_error_text("TLS needs the host's name, to check the certificate against");
        return -1;
    }
    /*  HttpUrl keeps an IPv6 literal's brackets; a certificate has none.  */
    n = strlen(hostname);
    if (hostname[0] == '[' && n > 2 && hostname[n - 1] == ']') {
        ++hostname;
        n -= 2;
    }
    if (n >= sizeof name) {
        set_error_text("TLS: the host name is too long");
        return -1;
    }
    memcpy(name, hostname, n);
    name[n] = '\0';

    ST_mutex_lock(&net_lock);
    s = slot_for(handle);
    if (!s) {
        ST_mutex_unlock(&net_lock);
        set_error_text("no such socket");
        return -1;
    }
    if (s->listening || s->ssl) {
        ST_mutex_unlock(&net_lock);
        set_error_text(s->ssl ? "TLS is already started on this socket"
                              : "TLS: not on a listener");
        return -1;
    }
    ctx = tls_context_locked();
    if (!ctx) {
        ST_mutex_unlock(&net_lock);
        return -1;
    }
    ERR_clear_error();
    ssl = SSL_new(ctx);
    if (!ssl) {
        ST_mutex_unlock(&net_lock);
        tls_set_error("TLS state", NULL);
        return -1;
    }
    /*
     *  The name goes two places: into the handshake as SNI, which is how a
     *  host serving many names picks its certificate, and into the verify
     *  parameters, which is what checks the certificate is for this name.
     *  An address literal goes only to the second, as an address -- SNI
     *  is names only, and a certificate carries an address in a field of
     *  its own.
     */
    ok = SSL_set_fd(ssl, (int) s->fd) == 1;
    if (ok) {
        if (is_ip_literal(name))
            ok = X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), name) == 1;
        else
            ok = SSL_set_tlsext_host_name(ssl, name) == 1
              && SSL_set1_host(ssl, name) == 1;
    }
    if (!ok) {
        SSL_free(ssl);
        ST_mutex_unlock(&net_lock);
        tls_set_error("TLS setup", NULL);
        return -1;
    }
    SSL_set_connect_state(ssl);
    s->ssl = ssl;
    ST_mutex_unlock(&net_lock);
    if (tracing())
        fprintf(stderr, "net: tls started on handle %lld for %s\n",
                (long long) handle, name);
    return 0;
}

int
NET_tls_handshake(int64_t handle)
{
    SSL    *ssl;
    int     index;
    int     rc;
    int     saved;
    long    outcome;

    if (!ready())
        return -1;
    index = tls_acquire(handle, &ssl);
    if (index < 0)
        return -1;
    ERR_clear_error();
    rc      = SSL_do_handshake(ssl);
    saved   = net_errno();
    outcome = rc == 1 ? 0 : tls_outcome(ssl, rc, saved, TLS_HANDSHAKING, "TLS handshake");
    tls_release(index);
    if (tracing())
        fprintf(stderr, "net: tls handshake on handle %lld: %ld%s%s\n",
                (long long) handle, outcome,
                outcome == -1 ? " " : "", outcome == -1 ? last_text : "");
    return (int) outcome;
}

int
NET_is_tls(int64_t handle)
{
    net_socket *s;
    int         yes;

    if (!ready())
        return 0;
    ST_mutex_lock(&net_lock);
    s   = slot_for(handle);
    yes = s != NULL && s->ssl != NULL;
    ST_mutex_unlock(&net_lock);
    return yes;
}

#else   /*  no TLS in this build  */

int
NET_tls_start(int64_t handle, const char *hostname)
{
    (void) handle;
    (void) hostname;
    set_error_text("this build has no TLS: OpenSSL was not found when it was built");
    return -1;
}

int
NET_tls_handshake(int64_t handle)
{
    (void) handle;
    set_error_text("this build has no TLS: OpenSSL was not found when it was built");
    return -1;
}

int
NET_is_tls(int64_t handle)
{
    (void) handle;
    return 0;
}

#endif  /*  ST_HAVE_TLS  */

/*  ----------  Two OS services  ----------  */

int
NET_random_bytes(void *out, size_t count)
{
    unsigned char  *p = (unsigned char *) out;

#if defined(ST_WINDOWS)
    if (BCryptGenRandom(NULL, p, (ULONG) count,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        set_error_text("BCryptGenRandom failed");
        return -1;
    }
    return 0;
#elif defined(__linux__)
    while (count > 0) {
        ssize_t n = getrandom(p, count, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;                      /*  fall through to /dev/urandom  */
        }
        p     += n;
        count -= (size_t) n;
    }
    if (count == 0)
        return 0;
#elif defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__)
    while (count > 0) {
        size_t  chunk = count > 256 ? 256 : count;

        if (getentropy(p, chunk) != 0)
            break;
        p     += chunk;
        count -= chunk;
    }
    if (count == 0)
        return 0;
#endif
#ifndef ST_WINDOWS
    {
        int     fd = open("/dev/urandom", O_RDONLY);

        if (fd < 0) {
            set_error(errno, "/dev/urandom");
            return -1;
        }
        while (count > 0) {
            ssize_t n = read(fd, p, count);

            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                set_error(errno, "/dev/urandom");
                close(fd);
                return -1;
            }
            p     += n;
            count -= (size_t) n;
        }
        close(fd);
        return 0;
    }
#endif
}

void
NET_set_arguments(int argc, char **argv)
{
    argument_count  = argc;
    argument_vector = argv;
}

int
NET_argument_count(void)
{
    return argument_count;
}

const char *
NET_argument(int index)
{
    if (index < 0 || index >= argument_count || !argument_vector)
        return NULL;
    return argument_vector[index];
}

const char *
NET_environment(const char *name)
{
    if (!name || !name[0])
        return NULL;
    return getenv(name);
}

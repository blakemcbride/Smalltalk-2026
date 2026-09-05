/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  TCP sockets, as much of them as a Smalltalk server needs and no more.
 *
 *  This file knows nothing about object memory, OOPs or primitives.  It
 *  answers C types and small-integer handles, and prim.c does the
 *  marshalling -- the split st_odbc.h has with primitive 129, and for the
 *  same reason: this file can be read, and tested, without an image.
 *
 *  WHAT IT IS FOR.  A server that runs one request per green process on a
 *  pool of native workers.  Everything below follows from one decision,
 *  which is that A POOL THREAD NEVER BLOCKS ON THE NETWORK.  Every socket
 *  is non-blocking; a call that cannot complete answers NET_WOULD_BLOCK and
 *  the caller ARMS the socket and waits on a Semaphore.  One thread of its
 *  own -- not a worker -- runs poll() over every armed socket and signals
 *  the Semaphore when the socket is ready.  So a hundred idle keep-alive
 *  connections cost a hundred parked green processes and no worker at
 *  all, and the pool is spent only on work.  The alternative, blocking a
 *  worker inside recv the way the database layer blocks one inside
 *  SQLExecute, is simpler and was rejected because with eight workers it
 *  is eight idle browsers that stall the server.
 *
 *  WHY A THREAD AND NOT A WORKER.  The I/O thread is outside the safepoint
 *  protocol: it never runs bytecodes, never allocates, and touches the
 *  object memory in exactly one way -- it hands a Semaphore's token to the
 *  hook prim.c installs, which is SCHED_asynchronous_signal, whose only
 *  look at the object memory is OM_is_present.  That is the timer thread's
 *  shape and the SDL pump's, both of which run under ThreadSanitizer today.
 *  The one rule that keeps it that way is kept in this file by construction:
 *  a token is handed to the hook only while its slot is registered, and
 *  prim.c drops its reference count only after the slot is gone.
 *
 *  WHY HANDLES ARE SMALL INTEGERS and not descriptors.  This system writes
 *  its memory to a file and reads it back in another process, so a
 *  descriptor kept in an object would look plausible and be somebody
 *  else's.  A handle here is an index into a table plus a GENERATION that
 *  every call checks; a resumed image finds every slot empty and every
 *  handle it kept refused, which is the truth.  The generation also closes
 *  the ABA hole the kernel leaves open: a descriptor number is reused the
 *  instant it is closed, and the I/O thread attributes a poll result by
 *  (index, generation) captured when the set was built, never by number.
 *
 *  THE ONE CALL THAT BLOCKS is getaddrinfo, which consults DNS and can take
 *  seconds.  It is bracketed with WORKER_enter_native and WORKER_leave_
 *  native like a database call, and its arguments are C strings prim.c has
 *  already copied out, so nothing in here holds a pointer into an object's
 *  bytes across it.  accept, recv, send and connect on a non-blocking
 *  descriptor are NOT bracketed: they cannot wait, and the bracket is a
 *  global mutex and a broadcast per call.
 *
 *  Two small OS services live here because they have the same portability
 *  shape (Winsock beside BSD sockets, BCrypt beside getrandom) and the
 *  server is what needs them: random bytes for session identifiers, and
 *  the words after `-serve <image>' on the command line.
 */

#ifndef ST_SOCKET_H
#define ST_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  How many at once.  The pollfd array is this plus one, so raising it is
 *  a recompile and nothing else.  A handle is (generation << 12) | index,
 *  which is why the index has twelve bits -- and why 4096 is the ceiling
 *  this number may be raised to without changing what a handle is.
 *
 *  It was 1024, and that was the whole of the table AND of the loopback
 *  denial of service Bugs4 NET-1 found: a thousand idle connections filled
 *  it, and the accept loop above then died of the first refusal.  The loop
 *  no longer dies (HttpServer>>acceptLoop), but a `-serve' pool that
 *  defaults to four workers per CPU is sized for a machine that can hold
 *  more than a thousand connections open, so the table is now the full
 *  width of the index.  The cost is a linear scan of this length per poll
 *  pass and 4096 mutexes for the TLS slots -- microseconds and a couple of
 *  hundred kilobytes, against a poll() syscall.
 *
 *  It is not the only ceiling: a host with the usual `ulimit -n 1024' runs
 *  out of DESCRIPTORS first, and the failure then reads as EMFILE from the
 *  operating system instead of "no free socket slot" from here.  Both are
 *  now one accept refused rather than a server that has stopped.
 */
#define NET_MAX_SOCKETS     4096

/*
 *  Answers from calls that can be asked too early.  Distinct from -1,
 *  which is a failure NET_last_error explains, because "not yet" is the
 *  ordinary case on a non-blocking socket and must not read as an error.
 *
 *  NET_WOULD_BLOCK means "wait for the direction this call is about":
 *  readable for a recv, writable for a send.  The other two exist for TLS,
 *  where a read can need the socket WRITABLE first and a write can need it
 *  READABLE -- the handshake, and a key update, are conversations that run
 *  underneath whichever call the caller made.  They say which way to wait
 *  when it is not the obvious way; a plain socket never answers them.
 */
#define NET_WOULD_BLOCK     (-2)
#define NET_WANT_READ       (-3)
#define NET_WANT_WRITE      (-4)

/*  What NET_arm takes.  */
#define NET_ARM_READ        1
#define NET_ARM_WRITE       2

/*  What NET_set_option takes.  */
#define NET_OPTION_NODELAY      0
#define NET_OPTION_KEEPALIVE    1

/*
 *  Bring the subsystem up: Winsock, the table lock, the wake pipe.  Safe
 *  to call from several threads at once and more than once; the first
 *  caller does the work and the rest wait for it.  The hook is what the
 *  I/O thread calls with a Semaphore's token; it answers non-zero if the
 *  signal was queued and zero if it was dropped, in which case the socket
 *  stays armed and is signalled again on the next pass.
 */
int         NET_init(int (*signal_hook)(uintptr_t token));

/*
 *  Take everything down: stop and join the I/O thread, close every
 *  descriptor, empty the table.  Tokens are dropped WITHOUT being touched
 *  -- by the time this runs the workers are gone and nobody will release
 *  the counts, which is fine, because so is the image.
 */
void        NET_shutdown(void);

int         NET_available(void);

/*
 *  The most recent failure, for the calling thread: as text, and as the
 *  errno or WSAGetLastError value behind it.  Per thread and not per
 *  socket, because the call that fails most often is the one that was
 *  going to produce the socket.
 */
const char *NET_last_error(void);
int         NET_last_errno(void);

/*  ----------  Listening and connecting  ----------  */

/*
 *  Listen on host:port -- host NULL or empty for every interface, port 0
 *  for one the system picks -- and answer a handle or -1.
 */
int64_t     NET_listen(const char *host, int port, int backlog);

/*  A connection, or NET_WOULD_BLOCK, or -1.  */
int64_t     NET_accept(int64_t listener);

/*
 *  Begin a connection and answer its handle at once.  The connect itself
 *  completes in the background: arm the handle for writing, wait, and ask
 *  NET_connect_result, which answers 0 when connected, NET_WOULD_BLOCK
 *  while still in progress, and -1 when the attempt failed.
 */
int64_t     NET_connect(const char *host, int port, int address_index);
int         NET_address_count(const char *host, int port);
int         NET_connect_result(int64_t handle);

/*  ----------  Reading and writing  ----------  */

/*
 *  Bytes read (more than zero), 0 at end of stream, NET_WOULD_BLOCK, or -1.
 *  On a TLS socket also NET_WANT_WRITE: wait for writable, then read again.
 */
long        NET_recv(int64_t handle, void *buffer, size_t max);

/*
 *  Bytes sent (possibly fewer than asked), NET_WOULD_BLOCK when the
 *  kernel's buffer is full, or -1.  The caller loops.  On a TLS socket
 *  also NET_WANT_READ: wait for readable, then send again -- with the
 *  same bytes, which the caller's loop from `start' does anyway.
 */
long        NET_send(int64_t handle, const void *buffer, size_t count);

/*  Half-close: no more will be sent; the peer reads end of stream.  */
int         NET_shutdown_write(int64_t handle);

/*
 *  Close and free the slot.  The two tokens the slot held are handed back
 *  so that the caller can release the counts it took -- AFTER this returns,
 *  outside the lock, when no thread can hand them to the hook any more.
 */
int         NET_close(int64_t handle, uintptr_t *old_read, uintptr_t *old_write);

/*  ----------  Waiting  ----------  */

/*
 *  Give the socket the two Semaphores the I/O thread will signal: one for
 *  readable (and for a listener, acceptable), one for writable (and for a
 *  connect, completed).  Tokens are opaque here; prim.c counts them.  The
 *  previous tokens come back for the caller to release.
 */
int         NET_set_tokens(int64_t handle, uintptr_t read_token,
                           uintptr_t write_token,
                           uintptr_t *old_read, uintptr_t *old_write);

/*
 *  Ask to be signalled once, when the socket is next readable and/or
 *  writable.  ONE-SHOT: the interest is cleared as the signal is queued, so
 *  a socket that stays readable is signalled once per arm rather than once
 *  per poll.  The contract for a caller is therefore
 *
 *      call -> NET_WOULD_BLOCK -> arm -> wait on the Semaphore -> call again
 *
 *  never arming twice without waiting in between.  A signal that lands
 *  before the wait is an excess signal on the Semaphore and the wait
 *  returns at once, which is correct.
 */
int         NET_arm(int64_t handle, int mask);

/*
 *  Take back an interest nobody is waiting for any more.
 *
 *  The arm above is one-shot and is normally spent by the delivery that
 *  wakes the waiter.  A TIMED wait is the case where it is not: the
 *  watchdog signals the Semaphore, the waiter gives up and walks away, and
 *  the interest stays in the poll set with no process behind it for as long
 *  as the socket is open.
 *
 *  That is not only a slot in the poll set.  NET_waits_pending is what the
 *  scheduler asks before it will say the image is deadlocked, and one arm
 *  left over makes the answer yes for ever -- so a single-worker image that
 *  really has stopped waits in silence instead of saying so, which is a
 *  hang with no diagnosis rather than a message and an exit.
 *
 *  Idempotent by design: clearing an interest already spent by a delivery
 *  is a no-op, so a caller that cannot tell which of the two happened --
 *  and the timed wait cannot, that is the race it is written around -- may
 *  simply always call it.
 */
int         NET_disarm(int64_t handle, int mask);

/*
 *  How many sockets are armed -- somebody is parked waiting for one.  The
 *  scheduler asks before declaring that nothing can ever run again: a
 *  server with a listener armed and every worker idle is a server with no
 *  clients yet, not a deadlock.  Armed and not open, on purpose: an open
 *  socket nobody is waiting on plus every worker idle IS a deadlock.
 */
int         NET_waits_pending(void);

int         NET_open_count(void);

/*  Wake the I/O thread so it rebuilds its set; also how a stop reaches it.  */
void        NET_wake(void);

/*  ----------  TLS  ----------
 *
 *  A client's side of TLS over a connected socket, through OpenSSL when
 *  the build found it (ST_HAVE_TLS) and refused by name when it did not.
 *  The socket stays non-blocking and the contract above stays the whole
 *  contract: NET_tls_start attaches the TLS state and does no I/O;
 *  NET_tls_handshake advances the handshake one step and answers 0 when
 *  it is done, NET_WANT_READ or NET_WANT_WRITE when it must wait, and -1
 *  with OpenSSL's words when it failed -- a certificate the system's
 *  store does not vouch for, a name the certificate is not for, a peer
 *  that does not speak TLS.  After the handshake, NET_recv and NET_send
 *  carry the bytes through the TLS state, and NET_close sends the close
 *  notice and frees it.
 *
 *  The certificate is checked against the system's own store -- what
 *  SSL_CTX_set_default_verify_paths finds, which SSL_CERT_FILE and
 *  SSL_CERT_DIR in the environment override -- and its name against the
 *  host asked for, as a browser checks them.  There is no way to turn
 *  either check off, on purpose: a client that can be asked to trust
 *  anything will one day be asked to.
 *
 *  Server-side TLS is not here.  A server on this system sits behind a
 *  reverse proxy that terminates TLS, which is the arrangement Kiss has
 *  with Tomcat and nginx.
 */
int         NET_tls_available(void);
int         NET_tls_start(int64_t handle, const char *hostname);
int         NET_tls_handshake(int64_t handle);
int         NET_is_tls(int64_t handle);

/*  ----------  Asking about a socket  ----------  */

int         NET_local_port(int64_t handle);
int         NET_peer_address(int64_t handle, char *out, size_t max);
int         NET_set_option(int64_t handle, int option, int value);

/*  ----------  For the collector  ----------  */

/*
 *  Every token the table holds, under the lock.  The root walk visits them:
 *  a Semaphore the image has dropped every reference to is still one the
 *  I/O thread will hand to the scheduler, and must not be reclaimed.
 */
void        NET_visit_tokens(void (*visit)(uintptr_t token, void *user),
                             void *user);

/*  Whether the table holds this token, for the one-way become refusal.  */
int         NET_holds_token(uintptr_t token);

/*  ----------  Two OS services  ----------  */

/*  Fill `out' from the system's entropy source.  0 or -1.  */
int         NET_random_bytes(void *out, size_t count);

/*  The words after `-serve <image>', kept for Smalltalk to ask for.  */
void        NET_set_arguments(int argc, char **argv);
int         NET_argument_count(void);
const char *NET_argument(int index);

/*
 *  An environment variable's value, or NULL when there is none.  Here
 *  because it has the arguments' shape and the same customer: a program
 *  reading an API key from where the shell put it, which is the one place
 *  a key should be.
 */
const char *NET_environment(const char *name);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_SOCKET_H  */

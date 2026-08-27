# The network

TCP sockets for a Smalltalk that runs on every core, and the run mode that
puts a server on them.

```smalltalk
| listener client server |
listener := ServerSocket listenOn: 8080.
client := Socket connectTo: 'localhost' port: 8080.
server := listener accept.
client send: 'hello'.
server receiveUpTo: 16                    "'hello'"
```

Four classes in `lib/Network` — `Socket`, `ServerSocket`, `SocketStream`,
`NetError` — over one primitive and one C file, `src/net/st_socket.c`; 28
tests in `lib/Network-Tests`; and `tests/unit/test_parallel_net.c`, which is
the gate.

## The one decision

**A pool thread never blocks on the network.** Everything below follows from
it.

A server here is one green `Process` per connection on a fixed pool of native
workers — `st80 -serve` starts N of them, four per core by default. If a worker
sat in `recv()` waiting for a browser to send its next request, eight idle
browsers on eight workers would stop the server, and a keep-alive connection
is idle nearly all of its life. So no worker ever waits in a socket call.
Every socket is non-blocking; a call that cannot complete answers `false`;
the process then **arms** the socket and waits on a `Semaphore`, which costs
the process its turn and the worker nothing at all.

One thread of the VM's own, `st-net-io`, runs `poll()` over every armed
socket and signals the semaphore when the socket is ready. It is not a
worker: it never runs a bytecode, never allocates, and touches the object
memory in exactly one way — it hands a semaphore's oop to
`SCHED_asynchronous_signal`, whose only look at the memory is
`OM_is_present`. That is the delay timer's shape and the SDL pump's, both
TSAN-clean before it, and it means the I/O thread is outside the safepoint
protocol entirely: the collector need not stop it and cannot be stopped by it.

The alternative — park the worker in `recv()` with `WORKER_enter_native`,
the way a database call parks it — is simpler and was rejected for the
eight-browsers reason. It is still what `getaddrinfo` does, because DNS can
take seconds and there is no non-blocking form of it.

## The contract every wait keeps

Interest is **one-shot**. An arm is answered by exactly one signal, and the
thread clears the interest as it queues the signal, so a socket that stays
readable is signalled once per arm and not once per pass of `poll()`. Every
wait in `Socket` is therefore the same loop:

```
call -> false -> arm -> wait -> call again
```

and it tolerates a spurious wake — the call answers `false` again, the arm is
already in place, the wait resumes. That tolerance is load-bearing: it is
what lets a timed wait be written with no VM support at all. A watchdog
process signals the same semaphore when the time is up, and if the data came
first the watchdog's signal is one excess signal that the next loop absorbs.
The *receive* is what decides whether anything arrived; the deadline is kept
by whoever asked for it, and a receive that still answers `false` after the
deadline is the timeout. `waitReadableTimeout:` was first written to decide
from the clock, and a wake that landed on the deadline read as "data" and
parked the reader for ever.

**One reader and one writer per socket.** Two processes receiving from one
socket would both arm and both wait, and one signal wakes one of them. That
is a program error, and the class does not try to make it work.

**Closing wakes a waiter.** `close` signals both semaphores after the handle
is gone, so a process parked on a socket another process closed finds it
closed on its next call rather than sleeping on.

## What the VM does

`Socket class>>primCommand:with:with:with:` is primitive 208 — one number
with a command integer, the shape 129 has for the database and 130 for the
file system — with `Odbc`'s contract: the primitive **fails** when its
arguments were wrong (a bug in `Socket`, and the fallback raises), answers
**nil** when the operating system said no (`Socket class>>lastError` has its
words), and answers **false** when a non-blocking call could not complete
yet.

Handles are small integers — a slot index under a generation counter — never
descriptors, for the reason `st_odbc.h` gives: an image is written to disk
and read back in another process, where a descriptor number would be somebody
else's socket. A resumed image finds every handle it kept refused. The
generation also settles the kernel's habit of reissuing a descriptor number
the instant it is closed: the I/O thread attributes every `poll()` result by
`(slot, generation)` captured when it built its set, never by number.

`SCHED_asynchronous_signal` now answers whether it queued the signal. Its
queue was 64 entries and dropped on overflow, which for a keystroke is a lost
keystroke and for a socket is a request that never wakes; it is 1024 now, and
when it is full the I/O thread keeps the socket armed and tries again a
millisecond later.

The scheduler's idle verdict — every worker idle and nothing pending, so
nothing can ever run again — asks the network layer, through
`SCHED_set_external_wait_hook`, whether any socket is armed. A quiet server
with a listener armed is a server with no clients yet, not a deadlock. Armed
and not open, deliberately: an open socket nobody is waiting on plus every
worker idle *is* a deadlock.

The semaphores a socket holds are C-held roots: `provide_roots` in `interp.c`
visits them beside the timer semaphore, and so are the signals sitting in the
async queue between an enqueue and a drain. `ST_interp_forward_forbidden`
refuses to forward them.

### Two things found the hard way

**Linux `close()` does not release a descriptor another thread is blocked in
`poll()` on.** `poll` holds the open file until it returns, so the socket
stays open in the kernel, no FIN is sent, and the peer never reads end of
stream. The first version of `NET_close` assumed a closed descriptor would
yield `POLLNVAL` and wake the thread by itself. It did not: a server that had
timed an idle connection out and closed it — while parked on that very
socket, which was therefore in the thread's set — left its client waiting for
a close that never came. `NET_close` wakes the thread, which rebuilds its set
without the slot and returns from `poll`, and that is what closes the socket.
`ST_NET_TRACE=1` narrates arms, deliveries and closes on standard error, and
is how this was seen.

**`SCHED_timer_semaphore()` was read without the timer's lock**, from the
root walk on a worker at a safepoint, while the timer thread — which no
safepoint parks — wrote it. ThreadSanitizer saw the two meet under a gate
that collects while delays are pending. It takes the lock now.

### A name with two addresses

`Socket connectTo: 'localhost' port: 11434` answered *connection refused*
from an Ollama that was up. `localhost` resolves to `::1` and then
`127.0.0.1` here, Ollama listens on `127.0.0.1` alone, and `NET_connect`
took the first address only, on the grounds that no caller had needed
more. Now `connectTo:port:` asks how many addresses the name has
(`NET_address_count`) and connects to each in turn (`NET_connect`'s third
argument), raising the last one's error; the loop is in Smalltalk because a
non-blocking connect learns of a refusal only later, from
`NET_connect_result`. `SocketTest>>testEveryAddressOfANameIsTried` listens
on `127.0.0.1` alone and connects by name.

## TLS

The client side, through OpenSSL when the build found it (`ST_HAVE_TLS`)
and refused by name when it did not. `NET_tls_start` attaches the TLS state
to a connected socket and does no I/O; `NET_tls_handshake` advances the
handshake one step; after it, `NET_recv` and `NET_send` carry the bytes
through the state, and `NET_close` sends the close notice and frees it.
The certificate is checked against the system's store and its name against
the host — `Socket>>startTls:` and `doc/HTTP-CLIENT.md` say what is
refused and how it reads.

The socket stays non-blocking and the contract above stays the whole
contract. What TLS adds is that a read can need the socket **writable**
first and a write can need it **readable** first — the handshake, and a
key update, are conversations that run underneath whichever call the
caller made — so beside `NET_WOULD_BLOCK` there are `NET_WANT_READ` and
`NET_WANT_WRITE`, which say which way to wait when it is not the obvious
way. The primitive answers them as the SmallIntegers −1 and −2, which no
count can be; `false` still means what it always meant, so a plain
socket's caller reads exactly what it always read. `Socket>>waitOn:default:`
is the one place that knows.

The TLS state has a lock of its own per slot, outside the slot because
`NET_close` wipes the slot: an `SSL` object is not safe to use from two
threads at once, and the two that can meet on one are a process reading
through it and another process closing it — which the plain socket
tolerates (`recv` on a closed descriptor is `EBADF`) and which `SSL_free`
would make a use after free. The lock is held for the length of one
OpenSSL call, none of which blocks. Lock order is the slot's TLS lock, then
`net_lock`; `NET_close` takes `net_lock`, lets it go, and only then takes
the TLS lock, so neither waits on the other while holding what the other
wants. The state is created and freed under it; the handshake, a read and
a write take it, re-check under `net_lock` that the slot still holds the
state they were given, and let it go.

Two OpenSSL modes matter to a non-blocking caller and are set on the one
context: a partial write is answered as one, the way `send` answers one,
and a retried write may come from another address — the bytes cross in a
per-thread scratch buffer and the process that retries may be on another
thread by then. And `SSL_OP_IGNORE_UNEXPECTED_EOF`, because most HTTP
servers close after `Connection: close` without the TLS close notice, and
HTTP frames its own bodies, so a truncation is caught one layer up.

`tests/unit/test_socket.c` checks the shape without a TLS server: the
state attaches to a connection and not to a listener, a stranger or a
socket that has it already; the handshake waits and says which way; a peer
answering in plain text is refused with *wrong version number*. The real
thing is `lib/HTTP-Client-Live-Tests`, on the internet.

`NET_environment` is the third OS service beside random bytes and the
arguments: an environment variable, which is where an API key belongs and
the one place `Smalltalk environmentAt:` reads one from.

## `st80 -serve`

```
st80 -serve <image> [-workers n] [args...]
```

Runs the image on a pool of native threads, no window, until `SIGINT`,
`SIGTERM` or `Smalltalk quitPrimitive`. Worker 0 resumes the image's own
startup process; every other worker joins the scheduler with nothing to run
— `SCHED_enter_idle`, which is new — and takes ready processes as they
appear. The `args` are what `Smalltalk arguments` answers; a server reads
its configuration file's name there.

Nothing is compiled by `-serve`: `BOOT_install_scheduler` resolves globals
through the bootstrap's own tables, which a loaded image does not have. The
startup is given to `-bootstrap` and saved in the image:

```
./st80 -bootstrap -profile profiles/st2026.profile -startup 'RestServer serve' -o server.im
./st80 -serve server.im -workers 8 server.json
```

This is the first run mode that starts the worker pool. Before it, only the
parallel test suites and `make bench` ever called `WORKER_start`; `-run` and
`-bootstrap -eval` are single-threaded and still are.

A stop request is one atomic store — `SCHED_request_stop`, async-signal-safe
— seen by every worker at its next process-switch check or idle slice. The
exit code is 0 for a stop that was asked for and 1 for an image that stopped
on its own: the scheduler's verdict, a frame that overflowed, memory that ran
out.

## Testing

`lib/Network-Tests` runs in the `st2026` profile, over the loopback interface
on a port the system picks, so it needs no network and no fixed port. Under
`-bootstrap -tests` there is one interpreter and every process is green; the
arm-wait-retry contract is the same there as on thirty-one workers, because
the VM's network thread is the same thread in both.

`tests/unit/test_socket.c` drives the C layer with no image: the table, the
handles, the one-shot arm, a dropped signal delivered later, a stale handle
refused.

`tests/unit/test_parallel_net.c` is the gate: eight and then thirty-one
workers, an accept loop on one, a forked echo process per connection picked
up by whichever worker is idle, twice as many native client threads sending
forty messages each with their own identity in every one, a thread stopping
the world throughout, and a full collection forced by every connection's
first message while its semaphores are armed. Every check has one right
value; a root the collector does not see arrives as a wake that never comes,
which the alarm turns into a failure. On this machine 62 clients are served
on 27 of 31 workers.

### What the tests found

**`whileTrue:` bodies are inlined**, so a temporary declared inside one is a
single variable shared by every iteration. The first version of the gate's
accept loop forked an echo process capturing such a temporary, and a process
forked in one iteration read the socket the next iteration accepted — two
processes on one socket, one of them finding it closed under it.
`HttpServer` binds the socket through a method argument for exactly this
reason, and says so.

**An unhandled error in a forked process prints and carries on with nil.**
A process left waiting on a closed listener went round its loop accepting
nothing and reporting it, for ever. Loops that wait on a socket end when the
socket is closed, by handling `NetError`.

## Portability

POSIX and Winsock halves under `ST_WINDOWS`, as in `src/port/st_port.c`:
`poll`/`WSAPoll`, a self-pipe / a loopback UDP socket for the wake channel,
`SO_REUSEADDR` / `SO_EXCLUSIVEADDRUSE`, `MSG_NOSIGNAL` and `SO_NOSIGPIPE`
where they exist and `SIGPIPE` ignored in `-serve` regardless, `getrandom` /
`getentropy` / `BCryptGenRandom` / `/dev/urandom` for the random bytes a
session id is made of. `Makefile.msvc` links `ws2_32` and `bcrypt`. Windows
is compiled for and not yet run.

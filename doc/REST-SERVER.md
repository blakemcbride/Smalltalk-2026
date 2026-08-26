# The REST server

Kiss's JSON-RPC protocol, served from this image on every core, with the
services loaded from Tonel files on first use.

```smalltalk
RestServer new
    port: 8080;
    backendDirectory: 'backend';
    documentRoot: 'frontend';
    database: 'DSN=shop;UID=app;PWD=secret';
    start
```

```
backend/services/Adder.class.st

Class { #name : 'Adder', #superclass : 'RestService', #category : 'services' }
Adder >> addNumbers [
    outjson at: 'num3' put: (injson integerAt: 'num1') + (injson integerAt: 'num2')
]
```

```
$ curl -d '{"_class":"services.Adder","_method":"addNumbers","num1":3,"num2":4}' http://localhost:8080/rest
{"num3":7,"_Success":true,"_ErrorCode":0,"_BootId":"fcec7be8-66e8-4a7b-905d-8bae66cf6d23"}
```

Three categories: `JSON-RPC-Server` is what Tomcat was to Kiss — the
listener, the process per connection, the request parser, the static files;
`Rest-Server` is Kiss's `org.kissweb.restServer` — the envelope, sessions,
login, service loading, one transaction per request; and `Tonel` reads a
service's file into the running image and writes it back when the class is
edited. Below them all is `lib/Network` (`doc/NETWORK.md`).

## Where Kiss stops and this begins

Kiss's `restServer/` is 3,976 lines of Java in fourteen files, and
`lib/Database/PROVENANCE.md` said it would not be ported because it was
servlet-container and JVM-reflection architecture rather than functionality.
That was half right. The container half — `HttpServlet`, `AsyncContext`,
`@MultipartConfig`, the thread pool in `QueueManager`, Tomcat's default
servlet serving `index.html` — is architecture, and it was not ported; it was
replaced, by `HttpServer`, which is what this system has instead of Tomcat.
The reflection half — `GroovyClassLoader`, `javax.tools` compiling `.java`
files in memory, ABCL — is how a JVM loads code it did not have at start, and
in Smalltalk that is a message: `TonelReader` and `compile:classified:`.

What crossed is the protocol, and it crossed unchanged so that Kiss's
`Server.js` works as it is:

| request | |
|---|---|
| `_class` | the service, as `services.Adder` |
| `_method` | the method |
| `_uuid` | the session, from `Login` |
| `_file-0`, `_file-1`... | uploads, as multipart parts |

| reply | |
|---|---|
| `_Success` | true or false |
| `_ErrorCode` | 0; 2 for *log in first*; −1 or the service's own otherwise |
| `_ErrorMessage` | when `_Success` is false |
| `_BootId` | a uuid made at start, so a front end can tell the server restarted |

Always HTTP 200; errors are in the document. A multipart form's scalars are
decoded the way `Server.js` encodes them — strings prefixed with `S`, and
`true`, `false`, `null` and numbers bare. A binary reply is
`application/octet-stream`: the JSON, one byte 3, the bytes. Three methods on
the empty class are the framework's — `LoginRequired`, `Login`, `Logout` —
and the application's `backend/Login.class.st` does the actual checking:

```smalltalk
Login class >> login: username password: password outjson: out request: request
    ...answer a RestUserData from request server userCache newUser:..., or nil
Login class >> checkLogin: aUser request: request
    ...answer whether the user is still allowed in; asked again after two minutes
```

## A service is a class, and its method has no arguments

Kiss hands a service method four parameters — `injson`, `outjson`, the
`Connection`, the `ProcessServlet` — because Java has no other way to give a
method its context. A Smalltalk service is a subclass of `RestService`, and
those four are instance variables the dispatcher sets on a **new instance for
every request** before it sends `_method` as a unary selector. State that
belongs to nobody else needs no lock; a service that keeps something in a
class variable is choosing to share it and must lock it, like everything else
on this system.

Only a unary method defined in a subclass of `RestService` can be reached
from outside. `printString`, `halt`, the package's own `requireLogin`, and
anything with a colon in it answer *No such method*. The three faults in
Kiss's dispatcher this one does not repeat: a request without `_class` hung
the client for ever (an error reply here); a method registered as allowed
without authentication converted its dots to slashes on the way in and not on
the way out, so the exemption could never match (matched as registered here);
and a call that failed the login check when no login was required ran the
service anyway after writing a failure (here it runs, on purpose, with no
user).

## Loaded on first use, reloaded on change, written back on edit

`services.Adder` names `backend/services/Adder.class.st`. The first request
for it reads the file into the image through `TonelSource`, which records the
file's time and size; every later request compares them and reads the file
again if either changed. That is Kiss's microservice property — edit the file
on a running server and the next call runs the new code — with a whole class
as the unit. The other direction is the Browser: a method accepted or removed
in it, on a class that came from a file, writes the file, through hooks on
the three messages the Browser sends. `doc/TONEL.md` is not a file; the
package's class comments are the document, and `lib/Tonel/TonelReader.class.st`
is where to start.

The name is checked before it is a path: only letters, digits and underscores
between the dots reach the file system, so `../../etc/passwd` is refused by
name. Two requests arriving for a class nobody has loaded load it once; the
loader holds its lock across the load.

Two things the in-image compiler could not do until this needed them: read
`:=` as assignment — every file in `lib/` is written that way and every one
was compiled by the C bootstrap, so nothing had ever compiled that spelling
*here* — and fail a compile without opening a window. A syntax error with no
requestor opened 1983's `SyntaxError` notifier and suspended the process,
which in a headless server is a hang; `TonelReader` is the compiler's
requestor now and a compile error is a `TonelError` naming the file, the
method and the character.

## One transaction per request

A connection is taken from `RestConnectionPool` before the service runs and
put back after; the service's method runs inside a transaction that is
committed when it returns and rolled back when it raises. A service that
wants the connection gone sooner sends `request closeConnection: aBoolean`.
The pool is a `SharedQueue` of open `DbConnection`s, which bounds the
requests inside the database at once to the pool's size; `lib/Database` asks
that a connection belong to one process at a time, and that is what the
queue enforces. Login is required when the configuration says so, and
otherwise whenever there is a database — Kiss's rule.

`lib/Rest-Server-Live-Tests`, in the `database-live` profile with the rest of
that kind, watches a row inserted by a request that returned stay and a row
inserted by a request that raised go.

## What is shared, and what guards it

Kiss keeps its server state in static fields, some of them plain `HashSet`s
read by every request and written at startup on the assumption that startup
is over. Here every piece is an object with a lock, or immutable after start:

| | |
|---|---|
| the environment (what the configuration file said) | `Dictionary` under a `Mutex`, reads too |
| the methods allowed without a login | `Set` under a `Mutex` |
| the sessions (`RestUserCache`) | `Dictionary` under a `Mutex`; a purge process every minute |
| a user's own data (`RestUserData`) | its own `Mutex` |
| the loader | a `Mutex`, held across a load |
| the connections | a `SharedQueue` |
| `bootId` | written once at start |
| a request, a service instance | nobody's but the process serving it |

`HttpServer`'s own table of handlers is under a `Mutex` too, and its count of
open connections. Nothing in either package is a class variable written after
`initialize`.

## The HTTP server

`HttpServer` is HTTP/1.1 with what a JSON-RPC front end needs and nothing
else: `Content-Length` bodies (chunked is refused with 411, since a JSON-RPC
client always knows how long its document is), keep-alive with an idle
timeout, multipart/form-data, and files under a document root for every path
no handler claims — `..` refused, a directory answering its `index.html`,
the content type by extension. Every reading is bounded: a line over 8,192
bytes, more than a hundred headers, a body over the limit (64 MB by default)
are refused before they are read. Kiss's `SecurityHeadersFilter` headers are
sent with every response.

One `Process` per connection, forked by the accept loop and picked up by
whichever worker is idle: that is the whole of the threading. The socket
reaches the process as a method argument, because `whileTrue:` is inlined and
a temporary declared in its body is one variable for every iteration — the
gate on the socket layer found a forked process reading the socket the next
iteration accepted.

TLS is not here; put a reverse proxy in front. Server-sent events, which Kiss
has, are not here yet.

## Running it

```
./st80 -bootstrap -profile profiles/st2026.profile -startup 'RestServer serve' -o server.im
./st80 -serve server.im -workers 8 server.json
```

`server.json`:

```json
{ "port": 8080, "bindAddress": "0.0.0.0", "backendDirectory": "backend",
  "documentRoot": "frontend", "database": "DSN=shop;UID=app;PWD=secret",
  "requireAuthentication": true, "userInactiveSeconds": 3600,
  "connectionPoolSize": 12, "keepAliveSeconds": 15,
  "allowWithoutAuthentication": [ "services.Catalog:list" ] }
```

Every name in the file is also in `request environment`, so a service reads
its own settings from the same place. `RestServer class>>serve` starts the
server and stays up until `SIGINT` or `SIGTERM`, then stops it and quits with
exit code 0. The log is standard error, one line per event, timestamped.

## Testing

| | where | what |
|---|---|---|
| `HttpRequestTest`, `HttpResponseTest` | `st2026` | the parser on text — more than a third of it refusals — and the writer |
| `HttpServerTest` | `st2026` | a server on the loopback interface driven by a client written on the raw socket |
| `RestServerTest` | `st2026` | the whole stack against `tests/rest-backend`: the envelope, sessions, uploads, a binary reply, error codes, a service edited and reloaded, sixteen calls at once |
| `RestLiveTest` | `database-live` | commit on return, rollback on raise, through SQLite |
| `tests/unit/test_parallel_rest.c` | `make test` | 31 workers, 62 native clients, 1,240 requests on kept-alive connections, the world stopped 3,000 times meanwhile; every sum checked, every worker seen |

### What the tests found

**The idle keep-alive test** found that Linux `close()` does not release a
descriptor another thread is blocked in `poll()` on — `doc/NETWORK.md` has
it — and, before that, that a timed wait must let the receive decide.

**The Tonel round trip** found that a bootstrapped image logged every method
compiled in it over the last one's source: 1983's `ReadWriteStream>>setToEnd`
goes to a `readLimit` that only `contents` ever raised. `sourceCodeAt:`
answered the last method compiled whatever it was asked for, in every test
run since the bootstrap was written.

**Ten connections at once**, run after Pharo's chronology tests, found that
the scheduler's list removal cleared a link's `nextLink` before the list had
taken hold of what it pointed at. A process in the middle of a list is held by
nothing but the link before it, so with three or more queued the second
one's only reference went, and releasing it released its next, and so on down
the chain: the list's new head named a freed object, and the scheduler later
resumed a `MethodContext`'s instruction pointer as a suspended context. It had
been there since the first commit; it needed a heap busy enough to reuse the
slots before the processes ran, which is what several hundred chronology
tests supplied. `set_active_context` now reports a non-context and stops
instead of dereferencing it.

**The 31-worker gate, with the collector forced along**, found that the
directory-listing primitive (command 3 of 131) made every name a String
first and the Array to hold them last, keeping the Strings meanwhile in a C
array with a count of zero. An allocation in this object memory can run a
full collection, which frees whatever no root reaches, and a C array is not
a root. `TonelSource reloadIfChanged:` lists the back-end directory on every
request, so 1,240 requests on 31 workers found the window: a listing came
back with a name that was no longer an object, the worker died inside the
loader's critical section, and every other request queued behind the lock
for ever. The Array is now made first and pushed on the stack — the pattern
`Smalltalk arguments` already used — and each String stored the moment it
exists; the ODBC column-describe primitive had the same shape and was fixed
with it.

**The same gate, once the listing was fixed**, still stopped one run in
three with every worker idle, the timer disarmed and 31 drivers asleep in
`Delay forMilliseconds: 5`: the Delay timing process — the one process every
worker runs in turn — had been freed while linked on a semaphore. Tracing its
reference count through every scheduling event found the count going down
by two on one switch and by one with no event at all. The decrement nobody
traced was the `Processor activeProcess` instance variable: one slot in one
object, written by every worker on every switch through `OM_store_pointer`,
which reads the old value, stores the new and releases the old as three
steps. Two workers switching at once both read the same old occupant and
both released it — one count lost, about once a minute on 31 workers — and
the value one of them stored in between was never released at all. The slot
is now exchanged atomically (`OM_exchange_pointer`), so each old value is
released once, by one worker. The same run showed a smaller hazard that
`-serve` cannot meet but the harness can: a worker whose run ends with a
nominee pending went home holding it; `SCHED_check_process_switch` no
longer drains or switches on a run that has ended, and `ST_interp_run` hands
a nominee back to its ready list on the way out.

**ThreadSanitizer over the same gates** reported two more: `someInstance`
walking the object table while other workers allocated into headers reused
in place (`Symbol rehash` does that walk; it is at a safepoint now), and the
Delay timer's once-init flag read by idle workers as a plain int (atomic
now). `doc/CONCURRENCY.md` has all of them.

**The same run under `pharo-weak`** found that the bootstrap resolved the
file-mode pool variables — `Read`, `Write`, `Shorten` — by reading a pool
`Dictionary`'s indexed part, which is where 1983's keeps its associations and
not where Pharo's does. Every profile that superseded `Dictionary` had a
`FileStream` that could not open a file, and no test in those profiles had
opened one.

## Provenance

`lib/Rest-Server/PROVENANCE.md`. The protocol is Kiss's; the code is not a
translation of any file in it.

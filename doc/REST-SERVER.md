# The REST server

Kiss's JSON-RPC protocol, served from this image on every core, with the
services loaded from Tonel files on first use.

```smalltalk
Smalltalk at: #Rest put: (RestServer new
    port: 8080;
    backendDirectory: 'backend';
    documentRoot: 'frontend';
    database: 'DSN=shop;UID=app;PWD=secret';
    yourself).
Rest start                                     "and later, Rest stop"
```

A global and not a temporary, so that a later doit can reach the server to
stop it. The bare name works in the same doit: a name nothing has defined
compiles quietly as a global, and `Smalltalk at:put:` fills that binding.

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

`services.Adder` names `backend/services/Adder.class.st`, and so does
`services/Adder` — the spelling Kiss's front end sends, and one Kiss reads
the same way (`_className.replace(".", "/")`). The first request
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

Two more, found when the demo ran under a real `-serve` — a loaded image,
which no test suite runs in. A method sending a selector defined later in
its own file compiled in every test and would not compile on the server:
1983's `Parser>>makeNewSymbol:startingAt:` puts a selector no method has yet
to the editor's user (*proceed / correct / abort*) and skips the menu only
for an editor of nil, and `TonelReader` answered no `editor` at all, so the
outcome was whatever an unhandled `doesNotUnderstand:` answered — nil in the
bootstrap process, not nil in a loaded image. A reader that
`internsNewSelectors` now gets the selector interned
(`lib/Tonel/Parser.extension.st`). And the image's own compiler is 1983's,
with no block-local temporaries: a service declares `| row |` at the method,
not inside `[:each | ...]`, which the bootstrap's closures dialect accepts
and every test therefore passed. Both are in `TonelReaderTest`.

And one found only by a browser, after every test, the manual's `curl` and
the smoke runs had passed: the loader took `services.Adder` and refused
`services/Adder` as *not a class name*, and the slash is what every screen
of Kiss's front end sends — so on a browser every screen but the login
failed, while the CRUD grid simply stayed empty. The loader takes both now,
`RestServerTest` sends both, and the demo's tests send what the front end
sends.

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

## Startup

Kiss's `KissInit.groovy` has two static methods the container calls on the
way up: `init()` before the database is opened and `init2(db)` after, with
a connection — or null when there is no database. Here that is
`backend/Init.class.st`, optional, and two class-side messages
`RestServer>>start` sends if the file exists:

```smalltalk
Init class >> init: aRestServer
    "before anything is opened: a logout handler, an exemption, a setting read"
    aRestServer logoutHandler: [:user | ...].
    aRestServer allowWithoutAuthentication: 'services.Catalog' method: 'list'

Init class >> init2: aConnectionOrNil
    "once the pool is open and before the listener is: schema, seed rows"
    (aConnectionOrNil tableExists: 'users') ifFalse: [...]
```

`init2:` runs with a pooled connection in a transaction of its own,
committed when it returns and rolled back when it raises, and gets nil when
the server has no database. Either method raising **stops the start**: the
pool is closed again, nothing is listening, and the error reaches whoever
sent `start` — an application that could not initialise must not be taking
requests. A class that defines only one of the two is sent only that one.
`RestServerTest` and `RestLiveTest` check all of it against
`tests/rest-init-backend`, including that a row written before `init2:`
raised is not there afterwards.

The demo's `Init` (`doc/WebDemo.md`) uses `init2:` to create its tables from
`schema.sqlite` the first time the server starts, which is why the demo has
no separate database step.

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

**Framing is strict**, because the documented way to run this is behind a
reverse proxy and two parsers that divide the same bytes into different
messages is the whole of request smuggling. Each of these is 400: a line ended
by a bare line feed, a field name with whitespace in it (`Content-Length : 25`),
a second `Content-Length`, a `Content-Length` that is not all digits
(`5, 5`), and `Transfer-Encoding` together with `Content-Length`. A malformed
percent escape in the target — `%zz`, `%2`, a trailing `%` — is 400 as well,
rather than passed through as the characters that spell it.

**A header is one header.** A response header value holding a CR, an LF or a
NUL, and a header name holding whitespace or a colon, raise — so a handler that
copies request data into a `Location` or a `Set-Cookie` gets a 500 and a line on
standard error rather than a reply with headers of the client's choosing. The
refusal is deliberate: stripping the line ending would leave a redirect to a URL
the handler did not write, with nothing anywhere saying so.

**A file must really be under the document root.** `..` is caught by reading
the path's segments; a symbolic link inside the root that points out of it is
not visible in the path at all, so the file's resolved name and the root's are
asked of the file system and compared, and anything outside is 403.

**A `HEAD` answers the length a `GET` would have.** The response is built
exactly as the `GET`'s and sent without its body.

One `Process` per connection, forked by the accept loop and picked up by
whichever worker is idle: that is the whole of the threading. The socket
reaches the process as a method argument, because `whileTrue:` is inlined and
a temporary declared in its body is one variable for every iteration — the
gate on the socket layer found a forked process reading the socket the next
iteration accepted.

### Another port is another origin

Kiss's development layout serves the front end from its own static server on
port 8000 and expects the back end on 8001. To the browser those are two
origins, so it sends `Origin` with every request, a preflight `OPTIONS`
before a `POST` with a JSON body, and shows the page the reply only if the
response says the origin is allowed. Kiss gets that from Tomcat's
`CorsFilter`, set to `*` for development with a warning not to ship it.

The rule here is fixed, not configured — Blake's, 2026-08-26: **the same
host, on any port.** An `Origin` whose host equals the host of the request's
own `Host` header (port stripped, case ignored) is allowed and echoed back in
`Access-Control-Allow-Origin` — never `*` — with `Vary: Origin`; its
preflight is answered 204 with `Allow-Methods: POST, GET, OPTIONS`, the
headers it asked for, and a day's `Max-Age`, before any handler sees it. Any
other host gets its response with no such header, which is what makes the
browser withhold it from the page, and a 403 for its preflight. A request
with no `Origin` is served exactly as before. `index.js` builds the back-end
address from the page's own hostname, so the two name the same host by
construction; `localhost` and `127.0.0.1` are different names to the rule.
The scheme is not compared, because a page on `https:` calling `http:` is
stopped by the browser as mixed content before it arrives.
`HttpCodec class>>hostOf:` is the comparison; `HttpServerTest`'s origin
tests are the contract on the wire.

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
| `HttpServerTest`'s origin tests | `st2026` | a preflight answered with no handler run, this host on another port echoed, another host given no permission |
| `WebDemoTest` | `st2026` | the demo without a database: Kiss's front end served by the browser's paths, `addNumbers`, an upload, the one-button REST Services screen, what the database screens say — the services spelt as the front end spells them, `services/…` |
| `WebDemoLiveTest` | `database-live` | the demo with SQLite: Kiss's user logging in with the hash Java made, the phone list and the users screen through a session, legacy passwords, a second start finding its tables |
| `HttpClientTest` | `st2026` | the client against `HttpServer` and against a listener of the test's own: chunked, to-the-close, 204, not HTTP, a body read by the line, https to a plain peer refused |
| `AnthropicTest`, `OpenAITest`, `OpenRouterTest`, `OllamaTest`, `LLMConversationTest`, `QdrantTest` | `st2026` | each service's wire against a listener that keeps every request: headers, bodies, streams, tool round trips, embeddings, errors — `doc/LLM.md` |
| `CryptoTest`, `Base64Test`, `PasswordHashTest`, `tests/unit/test_crypto.c` | `st2026`, `make test` | the published answers, and the Kiss hash |

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

## The demo

Kiss's demo application runs on all of this, under `demo/`: the back end in
Smalltalk (`demo/backend`, category `Web-Demo`), Kiss's own front end copied
whole (`demo/frontend`, one line stamped and the name on its pages changed,
`PROVENANCE.md` beside it), one
`RestServer` serving both from one port. `make demo-image`, then
`./st80 -serve demo.im demo/server.json`, then `http://localhost:8080` and
`smalltalk` / `password`. The first start makes the database from Kiss's
`schema.sqlite` through the `Init` hook, and the user's stored password is
the PBKDF2 hash Java made, verified byte for byte by `lib/Crypto`.
`demo/README.md` is the guide; `doc/WebDemo.md` is the plan it was built
from, with every decision and everything the building found.

## Provenance

`lib/Rest-Server/PROVENANCE.md`. The protocol is Kiss's; the code is not a
translation of any file in it.

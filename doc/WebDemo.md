# Plan: the Kiss demo application on Smalltalk-2026

Kiss ships as a running demo — a phone-list CRUD screen, a users screen, a
file upload, three "add two numbers" services, an SQL-access check, and an
Ollama chat — whose back end can be edited while the server runs. This plan
puts that back end on `lib/Rest-Server`, unchanged on the wire, and copies
Kiss's front end (`src/main/frontend`) in beside it, so that **one
`RestServer` serves the whole application** — the pages from its document
root, the services from `/rest` — with no change to the JavaScript beyond
one flag. The two-server layout (Kiss's own static server on another port)
keeps working too.

Category for every demo class: **`Web-Demo`**. The Kiss sources are
`/home/blake/GitHub.blakemcbride/Kiss/src/main/backend` (592 lines) and
`src/main/frontend` (80 files, 3.4 MB, of which 2.6 MB is two third-party
libraries).

## 0. Status

| phase | state |
|---|---|
| 1 Crypto | **built 2026-08-26**: `src/crypto/`, primitive 209, `lib/Crypto` (+`-Tests`, 29 tests), `tests/unit/test_crypto.c` (38 checks incl. the Kiss hash), ratchets moved; `test_image` 595/595, st2026 332/332; `make test` / `OM=bb` / ASAN / TSAN — see the end of this table's row when they finish |
| 2 Init hook | **built 2026-08-26**: `RestServer>>start` sends `Init class>>init:` before the pool and `init2:` (a pooled connection in its own transaction; nil with no database, as Kiss passes null) before the listener; either raising stops the start and closes the pool; `RestServiceLoader>>classFor:ifAbsent:`; fixture `tests/rest-init-backend`; +2 tests st2026 (334), +2 database-live (364); `doc/REST-SERVER.md` *Startup* |
| 3 demo + front end | **built 2026-08-26**: `demo/` (back end in `Web-Demo`, Kiss's front end copied with one stamped line, `schema.sqlite`, `server.json`, README), `make demo-image`; `lib/Web-Demo-Tests` (9, st2026 345) and `lib/Web-Demo-Live-Tests` (8, database-live 383); driven end to end under a real `-serve` with curl — every screen's call — which found two faults no suite could (§7, *found on the way*); TSAN from phase 1 still running |
| 4 CORS | **built 2026-08-26**: the same-host-any-port rule in `HttpServer` (`handle:` answers a preflight 204/403 before any handler and echoes an allowed `Origin` on every response; `dispatch:` is the old body), `HttpRequest>>origin hostName isPreflight`, `HttpCodec class>>hostOf:`; 6 tests (st2026 351); `doc/REST-SERVER.md` *Another port is another origin*; smoked on the real server with curl from `localhost:8000` (allowed), `evil.example` (no permission, preflight 403) and no Origin |
| 5 HTTP client + Ollama | **built 2026-08-26**: `lib/HTTP-Client` (`HttpUrl`, `HttpClient`, `HttpClientResponse`: Content-Length, chunked and read-to-close bodies, one connection per request, TLS refused by name), `lib/LLM` (`Ollama`: `isUp models send:`, `toHtml:`), the demo's `OllamaQuery`, `RestRequest>>environmentAt:`; 19 tests + 1 (st2026 371); against the real local Ollama: up, fifteen models listed, an answer from `llama3.2` — after the socket fault below was fixed |
| 6 documents | **built 2026-08-26**: chapter 19 — *Another port is another origin*, *Startup*, a second stnote on what the demo found, *An HTTP client*, *The demo application* (a numbered *Step by step* — driver, `make demo-image`, `-serve`, what the terminal prints, the login, the screens, a live edit that answers 107, Ctrl-C, starting over — the desktop variant, the screens table, the two-server layout as steps, the threads in OS-thread terms, the SQLite-driver stnote); appendix A (`make demo-image`), B (Crypto, PasswordHash, Base64, Init, origins, HttpClient, Ollama rows), D (*The demo application*); 12 doctests (`ManualExamples>>webDemo`, 370/370); `doc/HTTP-CLIENT.md` (new), `doc/REST-SERVER.md` (*The demo*, the two things found, tests table), `lib/Rest-Server/PROVENANCE.md` (the demo back end), `README.md` (paragraph, tree, doc table), `demo/README.md` (the threads); `manual/check.py` clean, PDF built |

Also done 2026-08-26, at Blake's direction: `-serve` defaults to **four
workers per CPU** (§3.5), capped silently at the table's 63.

Also done 2026-08-26, at Blake's request after using the demo from a
workspace: **the system clipboard** — `lib/Clipboard` (primitive 210 over
SDL3's clipboard; `ParagraphEditor`'s `copySelection`, `cut` and `paste`
superseded so copy and cut also fill the system's clipboard and paste
prefers it; line ends translated), chapter 20 *The clipboard is the
system's*, `doc/Display.md`. Not part of the demo plan, recorded here
because it is in the same uncommitted tree.

All six phases are built and every gate is clean on the final tree: `make
test` 0 (25 unit suites, five profiles at their recorded scores), `make
OM=bb test` 0, `make ASAN=1 unit-test` 0, `make TSAN=1 unit-test` 0 with 0
ThreadSanitizer warnings across 24 suites, `database-live` 409/409, the
manual's 370 doctests. Nothing is committed: Blake commits, on `main`.

## 1. What the demo is, file by file

| Kiss (`src/main/backend/`) | Called by | Here | Verdict |
|---|---|---|---|
| `Login.groovy` | `login.js` → `''`/`Login` | `demo/backend/Login.class.st` | port; needs PBKDF2 (§3.2) |
| `KissInit.groovy` | startup | `demo/backend/Init.class.st` | port; needs a startup hook (§3.3) |
| `application.ini` | startup | `demo/server.json` | already the port's config format |
| `DB.sqlite` (binary) + top-level `schema.sqlite` | Login, Crud, Users | `demo/schema.sqlite`, DB made on first start | port |
| `services/Crud.groovy` | `CRUD.js` | `demo/backend/services/Crud.class.st` | port, without `runReport`/`runExport` (§7) |
| `services/Users.groovy` | `Users.js` | `services/Users.class.st` | port; needs PBKDF2 |
| `services/MyGroovyService.groovy` | `RestServices.js`, `SQLAccess.js` | `services/MyService.class.st` | port, as the one service (§6, decision 3) |
| `services/MyJavaService.java` | `RestServices.js` | — | folded into `MyService`; the screen has one button (§6, decision 3) |
| `services/MyLispService.lisp` | `RestServices.js` | — | same |
| `services/FileUpload.groovy` | `FileUploadScreen.js` | `services/FileUpload.class.st` | port |
| `services/OllamaQuery.groovy` | `Ollama.js` | `services/OllamaQuery.class.st` | port; needs an HTTP client (§3.4) — last phase |
| `scripts/MyScript.groovy` | commented-out call in MyService | `demo/backend/scripts/MyScript.class.st` | port as an example of `TonelSource loadFile:` from a service |
| `CronTasks/crontab`, `EveryMinute.groovy` | nothing (crontab line is commented out) | — | not ported (§7) |
| `services/xxx.groovy` | nothing | — | not ported: a stray import script, not part of the demo |
| `src/main/frontend/` (80 files) | the browser | `demo/frontend/` | copied verbatim but for one flag (§2.1), the name on its pages, and the one-button `RestServices` screen (§6, decision 3) |

Every front-end call, so nothing is missed (from `grep Server.call` over
`src/main/frontend`): `Login`, `LoginRequired`, `Crud` × `getRecords
addRecord updateRecord deleteRecord runReport runExport`, `Users` ×
`getRecords addRecord updateRecord deleteRecord`, `MyGroovyService` ×
`addNumbers hasDatabase`, `MyJavaService addNumbers`, `MyLispService
addNumbers`, `FileUpload upload` (via `fileUploadSend`), `OllamaQuery` ×
`isOllamaUp listModels ask`.

## 2. Where it lives and how it is reached

```
demo/
  README.md              how to run it; login smalltalk / password
  server.json            port 8080, backend demo/backend, documentRoot demo/frontend, database demo/DB.sqlite
  schema.sqlite          the two tables and the one user (copied from Kiss)
  backend/
    Login.class.st       Web-Demo
    Init.class.st        Web-Demo
    services/*.class.st  Web-Demo
    scripts/MyScript.class.st
  frontend/              Kiss's src/main/frontend, as it is (§2.1)
    PROVENANCE.md        which Kiss commit, what was changed, the licences
    index.html login.js routes.js SystemInfo.js ...
    kiss/                the Kiss front-end framework: Server.js, Utils.js, the components
    screens/             the demo's screens, one directory each
    mobile/
    lib/                 ag-grid (MIT), ckeditor (GPL/commercial; kept, §6)
```

**Not a `lib/` package.** A `lib/` package is compiled into the image by a
profile at bootstrap. The demo's whole point — the property Blake chose the
Tonel-on-first-use design for — is that the code is *not* in the image until
a request names it and is edited on disk while the server runs. So the demo
is a back-end directory like `tests/rest-backend`, and `Web-Demo` is the
`#category` in every file, which is where the classes appear in the Browser
once loaded. The tests are a `lib/` package (`Web-Demo-Tests`) and do move
the ratchets.

**One server.** `RestServer documentRoot:` already serves files under a
directory for every path no handler claims — `..` refused, a directory
answering its `index.html`, the content type by extension, HEAD and GET only
— so `server.json` names `demo/frontend` as the document root and the
browser goes to `http://localhost:8080`. Pages and `/rest` are one origin;
CORS never enters into it. Running:

```
make demo-image      # ./st80 -bootstrap -profile profiles/st2026.profile -startup 'RestServer serve' -o demo.im
./st80 -serve demo.im -workers 8 demo/server.json
```

Or, green, from a workspace: `RestServer fromConfigFile: 'demo/server.json'`
then `start`.

**Two servers, still possible.** Kiss's own `serve` script runs
`SimpleWebServer.jar` on port 8000 against a front-end directory, and
`index.js` then assumes the back end is at port + 1. That works here with
the demo on 8001, the front end's `SystemInfo.sameOriginBackend` set back
to `false` (§2.1), and the CORS rule of §3.1 admitting the second port.
`SystemInfo.backendUrl` overrides everything, for a front end anywhere.

### 2.1 The front end, copied

`src/main/frontend` is copied to `demo/frontend` as it is — `index.html`,
`login.html`, `index.js`, `login.js`, `routes.js`, `SystemInfo.js`, the two
`normalize` sheets, `robots.txt`, `kiss/` (the framework: `Server.js`,
`Utils.js`, `Router.js`, `AppState.js`, `AGGrid.js`, the components),
`screens/` (ten screens), `mobile/`, `lib/`. Nothing in it reaches outside
itself: no CDN, no font host, no `<script src="http…">`; every load goes
through `kiss/bootstrap.js` with a cache-busting query string, and
`index.html` redirects itself to `index.html?now=<time>` on every visit.
`HttpStaticFileHandler` matches on the path alone, so the query strings
cost nothing; and `HttpCodec`'s table already names all four file types
the tree holds (`html css js txt`).

**One edit**: `SystemInfo.sameOriginBackend = true`. Kiss's source ships it
`false` and its build stamps `true` into the copy Tomcat serves; this copy
is served by the back end, so it is stamped here, by hand, with the comment
saying so. With it set, `index.js` uses the page's own origin whatever the
port, and the port-plus-one rule is never consulted.

**One rename** (Blake, 2026-08-26): where the pages' text said `Kiss` it
says `Smalltalk-2026` — `index.html`'s title, both login pages, and the
`Framework`, `SQLAccess`, `Export` and `Report` screens. The framework
directory `kiss/`, its globals and the comments keep their name;
`PROVENANCE.md` lists the files.

**Not edited**, and why it does not matter here: `index.html` says its
inline bootstrap kernel's hash is pinned in a Content-Security-Policy that
Kiss's `SecurityHeadersFilter` sends. `HttpServer` sends no CSP, so the
kernel runs unpinned. A CSP is one more standing header when it is wanted,
and is not part of this plan.

**Provenance and licences.** `demo/frontend/PROVENANCE.md` records the Kiss
commit the copy was taken at, the edit and the rename, and the licences: Kiss itself
is BSD 2-Clause by the same author — the same licence as this tree —
`lib/ag-grid-community.min.noStyle.js` is AG Grid Community 34.0.1, MIT;
`lib/ckeditor.js` is CKEditor 5 (CKSource, "all rights reserved" in its
header; CKEditor 5 is GPL-2.0-or-later or commercial), kept as Kiss ships
it (§6).

**Keeping it current.** It is a copy, and Kiss will move on. The
provenance file names the commit; bringing it up to date is `cp -r` and
the one-line stamp, and `PROVENANCE.md` says exactly that.

## 3. What is missing underneath

Four things the demo needs that `lib/` does not have. Each is a phase.
(Kiss's report and export files needed a fifth — a way to serve files the
back end makes — and are skipped at Blake's direction; §7.)

### 3.1 CORS — for the two-server layout

`Server.js` sends `fetch(url + '/rest', {method: 'POST', headers:
{'Content-Type': 'application/json'}})`. From an origin on port 8000 to a
server on 8001 the browser sends a preflight `OPTIONS /rest` first, and
`RestDispatcher>>handle:` answers 405 to anything but POST, so in that
layout nothing in the front end works until this exists. `HttpServer` has
no `Access-Control-*` header anywhere; Kiss gets it from Tomcat's
`CorsFilter` in `web.xml`, `cors.allowed.origins = *` for development.
With the front end served from the document root (§2) there is one origin
and none of this is consulted, which is why this phase comes after the
demo and not before it.

**The rule, Blake's (2026-08-26): same host, any port.** Not a
configuration — a fixed policy of `HttpServer`, with no wildcard and no
list to get wrong:

- A request with no `Origin` header (same-origin, `curl`, the tests) is
  served as now, with no CORS header.
- A request with an `Origin` whose host equals the host of the request's
  own `Host` header — both with the port stripped, compared without case —
  is served, and the response carries `Access-Control-Allow-Origin: <that
  Origin>` (echoed, never `*`) and `Vary: Origin`. The scheme is not
  compared: a page on `https:` calling `http:` is refused by the browser as
  mixed content before it reaches here.
- An `Origin` from any other host gets the response with **no**
  `Access-Control-Allow-Origin`, which is what makes the browser withhold
  it from the page; a preflight from such an origin is 403.
- An `OPTIONS` request with an `Origin` and
  `Access-Control-Request-Method` is a preflight: answered in
  `HttpServer>>handle:` before any handler sees it — 204 with
  `Allow-Origin` as above, `Allow-Methods: POST, GET, OPTIONS`,
  `Allow-Headers: Content-Type`, `Max-Age: 86400`. No credentials mode:
  `Server.js` sends none.

Why this is exactly the two-server demo's shape: `index.js` builds the
back-end URL from `window.location.hostname` and port + 1, so `Origin` and
`Host` name the same host by construction — `localhost` to `localhost`, or
the machine's name to itself from a phone on the LAN. (`localhost` and
`127.0.0.1` are different names to this rule; the front end never mixes
them.) Kiss's own filter is Tomcat's, set to `*` for development with a
warning not to ship it; this rule needs no warning.

- `HttpRequest` gains `origin` and `hostName` (the `Host` header without its
  port); `HttpServer>>handle:` gains the preflight and the header. Nothing
  on `RestServer`, nothing in `server.json`.
- Tests in `HttpServerTest`, on the wire: a preflight from
  `http://127.0.0.1:1` to a server on `127.0.0.1` answered 204 without the
  handler running; a POST from it carries the echoed header; a POST from
  `http://elsewhere:1` carries none and its preflight is 403; a request with
  no `Origin` carries none; the host comparison is case-blind.

### 3.2 Password hashing

`schema.sqlite` stores user `smalltalk` (Kiss's `kiss`, renamed) as
`pbkdf2$600000$<base64 salt>$<base64 hash>` — PBKDF2-HMAC-SHA256, 16-byte
salt, 256-bit key, Base64 without padding — and `Users.addRecord` hashes
new passwords the same way. The VM has no SHA-256, no HMAC and no Base64
(`grep -ri sha256 src lib` finds nothing). Six hundred thousand HMAC
rounds in the interpreter is minutes per login; it has to be C.

- `src/crypto/st_crypto.{c,h}`: SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, a
  constant-time compare. Self-contained, ~300 lines, no OpenSSL: the
  dependency is not worth it for one function, and Windows/macOS builds
  stay as they are. Primitive **209** (208 is sockets), command-numbered
  like 129/208. ~0.3 s per call at 600,000 rounds — read the note at
  `src/db/st_odbc.c:323` on safepoints and give this call the same treatment
  the ODBC calls get, so a stop-the-world does not wait on a login.
- `tests/unit/test_crypto.c`: FIPS 180-4 SHA-256 vectors, RFC 4231 HMAC
  vectors, RFC 7914 §11 PBKDF2-SHA256 vectors, and the demo's own stored
  hash for `password` — that last one is the compatibility proof: Kiss's
  `DB.sqlite` logs in here unchanged.
- `lib/Crypto`: `Base64` (`encode:`, `encodeWithoutPadding:`, `decode:`;
  pure Smalltalk, small) and `PasswordHash` (`hash:`, `verify:against:`,
  `isHashed:`, `needsRehash:`), the format string for string with Kiss's
  `PasswordHash.java`. Salt from `Socket randomBytes:`, which exists.
  `lib/Crypto-Tests`: the same vectors from Smalltalk, a round trip, a wrong
  password, a malformed stored value refused.
- Login parity: Kiss's `passwordMatches` accepts PBKDF2, a 64-hex SHA-256
  (legacy) and plain text. The primitive gives SHA-256 for nothing, so keep
  all three tiers — it is one line each.

### 3.3 An application startup hook

`KissInit.init()` runs before serving (reads the ini, an
`allowWithoutAuthentication` example, a logout-handler example) and
`init2(db)` runs once the database is open. The ini and the exemption list
are `server.json` already; `logoutHandler:` exists on `RestServer`. What is
missing is the hook itself.

- `RestServer>>start`: if `<backendDirectory>/Init.class.st` exists, load it
  through the loader and send `Init class>>init: server` before the pool
  opens and `Init class>>init2: db` with a pooled connection after it
  (committed after, rolled back if it raises — the `withConnection:` shape
  the dispatcher already has). Absent file, nothing happens; the test back
  end has none and stays as it is.
- The demo's `Init>>init2:` checks `db tableExists: 'users'` and, when it
  is absent, runs `demo/schema.sqlite` statement by statement. So the demo
  needs **no separate database step**: the SQLite ODBC driver makes
  `demo/DB.sqlite` on first connect and `Init` fills it. The one machine
  requirement — the SQLite ODBC driver (`sqliteodbc` on Fedora) — goes in
  `demo/README.md` and the manual; without it the server starts and every
  screen says what `lib/Database` already says, "no driver".
- `RestServerTest`: a back end with an `Init` whose `init:` writes a global
  the test reads; one whose `init2:` raises leaves the server not started.

### 3.4 An HTTP client and Ollama — the last phase, separable

`OllamaQuery` needs `GET /api/version`, `GET /api/tags` (`models[].model`),
`POST /api/generate {"model","prompt","stream":false}` → `response`. The
image has sockets and a server-side parser and no client.

- `lib/HTTP-Client`: `HttpClient` (`get:`, `post:body:contentType:`,
  `readTimeout:`) and `HttpClientResponse` (status, headers, body). Reuses
  `HttpCodec`. Reads `Content-Length` bodies, **chunked** bodies (Go's
  `net/http`, which Ollama is, chunks anything over its 2 KB buffer — a
  model list or a generation always is), and read-to-close for HTTP/1.0.
  `lib/HTTP-Client-Tests`: against `HttpServer` on the loopback interface
  (a static file, a block handler, a 404, a refused connection →
  `NetError`) and the chunked decoder on text, the way `HttpRequestTest`
  tests the parser.
- `lib/LLM`: `Ollama` — `url:` (default `http://localhost:11434/api/`),
  `isUp`, `models`, `model:`, `send:`, `timeoutSeconds:`, class-side
  `toHtml:` (Kiss's four regex replacements, by hand: drop `\boxed{…}`,
  drop backslashes, newline → `<br>`, `**x**` → `<b>x</b>`). The chat and
  streaming halves of `Ollama.java` (425 lines) are not needed by the demo
  and are not in this plan. No live test in any ratchet — it needs a
  running Ollama; `toHtml:` gets doctests.
- `services/OllamaQuery.class.st`: `ask` (timeout from `request environment`
  at `OllamaTimeoutSeconds`, as Kiss reads it from the ini), `isOllamaUp`,
  `listModels`. Kiss's `servlet.setTimeout(0)` has no counterpart: this
  server does not time out a handler, only an idle connection between
  requests.

### 3.5 Threads — decided 2026-08-26: keep the model as built

Blake described the server he wants in OS-thread terms: one thread waits
for connections and queues each; a pool of worker threads, sized by the
developer; a queued request is handed to a free worker, waits if none is
free, runs to completion, and the worker goes back to the pool. That is
Tomcat's NIO connector and Kiss's `QueueManager`, and it is what `-serve`
does, in these threads:

| the description | what runs under `st80 -serve demo.im -workers 8` |
|---|---|
| one thread waits for connections, queues each, loops back | the I/O thread, in `poll()` over the listening socket *and* every kept-alive connection; each arrival goes on the ready queue |
| a pool sized by the developer | `-workers n`; default four per CPU (Blake, 2026-08-26; was CPUs − 1) |
| a thread hands each request to a free worker, waiting if none is free | no hand-off thread: a free worker takes the next queued request itself; with none free the request waits in the queue |
| the worker runs the request and returns to the pool | the same; a database call holds the worker (blocked inside the ODBC driver, and the collector is told so it does not wait) |

Two things the description leaves out, and what is done about them:

- **Between a connection's requests** somebody has to watch it. If the
  worker did, blocking in `recv` for the next request, eight idle browser
  tabs would empty an eight-thread pool. So after the reply the connection
  goes back to `poll()` on the I/O thread and the worker is free.
- **A request that has to wait mid-way** for anything but the database —
  the rest of a slow upload, a lock another worker holds, an HTTP reply
  from Ollama — gives its worker back to the pool and is resumed by the
  next free worker when the wait ends, which may be a different thread.
  A request is therefore on one OS thread at a time but not always the
  same one from first byte to last. **Decision: keep this.** The
  alternative — hold the worker through every wait, so a request's thread
  is the same throughout — is a scheduler flag, and was declined because
  it spends a pool thread on a browser on a slow link or a thirty-second
  Ollama call, and because every gate in the VM was built against the
  model as it is. Nothing in the demo needs a fixed thread: no native
  library it uses is thread-affine.

The pool's default size is four workers per CPU (Blake, 2026-08-26): a
server's workers spend much of their time parked inside the database, and
a pool the size of the machine leaves cores idle while they wait. Done in
`do_serve` (`src/main.c`) — the general `WORKER_start(0, …)` rule, one
core reserved for the SDL pump, is for windowed runs and stays. The cap
is `ST_MAX_WORKERS - 1` = 63 — the worker table's and the interpreter
registry's size — so on a machine with sixteen or more CPUs the default
is 63 whatever the multiplier; raising that is a VM change, not planned.

**Documentation consequence** (phase 6): chapter 19 and `demo/README.md`
describe the threading in OS-thread terms — the table above — beside the
existing text, which speaks of processes and semaphores and was found
hard to map onto threads.

## 4. Phases, in order

Each phase ends green on its own gates with its ratchets moved; Blake
commits, on `main`.

| # | Phase | Touches | New tests | Ratchets move |
|---|---|---|---|---|
| 1 | Crypto (§3.2) | `src/crypto/`, prim 209, `Makefile`, `lib/Crypto`, `lib/Crypto-Tests`, `test_crypto.c`, `st2026.profile` | ~12 + C | classes, methods, categories, allSubclasses, allTests, SourceFiles, profiles ×5 |
| 2 | Init hook (§3.3) | `RestServer`, `RestServerTest`, `tests/rest-backend` | 2 | methods, allTests, profiles ×5 |
| 3 | The demo, back and front (§2, §2.1) | `demo/`, `lib/Web-Demo-Tests`, `Makefile` (`demo-image`), `.gitignore` (`demo/DB.sqlite`; `*.im` is covered) | see below | classes, methods, categories, allSubclasses, allTests, profiles ×5; database-live count |
| 4 | CORS (§3.1) | `HttpServer`, `HttpRequest`, `HttpServerTest` | 5 | methods, allTests, profiles ×5 |
| 5 | HTTP client + Ollama (§3.4) | `lib/HTTP-Client(-Tests)`, `lib/LLM`, `services/OllamaQuery.class.st`, `st2026.profile` | ~10 | as phase 1 |
| 6 | Documents (§5) | `manual/`, `doc/`, `README.md`, `PROVENANCE.md` | doctests | manual count |

The demo is usable after phase 3; phase 4 is for the two-server layout,
phase 5 for the Ollama screen.

**Phase 3 in detail.** First the copy: `cp -r` of `src/main/frontend` to
`demo/frontend`, the one-line stamp in `SystemInfo.js`, `PROVENANCE.md`
with the Kiss commit hash (`git -C ~/GitHub.blakemcbride/Kiss rev-parse
HEAD` on the day). Then the services, each a `RestService` subclass with a
class comment in the register the rest of `lib/` uses (what it shows, and
that it can be edited on the running server):

- `Login`: `login:password:outjson:request:` — `request db fetchOne:
  'select user_id, user_password from users where user_name = ? and
  user_active = ''Y''' with:`, the three-tier match, `request server
  userCache newUser:password:userId:`; `checkLogin:request:` the same
  query against `aUser password`.
- `Crud`: `getRecords` (`nodb` when `request server hasDatabase` is false,
  as `CRUD.js` expects; rows `id firstName lastName phoneNumber`),
  `addRecord` (`db newRecord: 'phone'`, `at:put:` ×3, `insert`),
  `updateRecord` (`fetchOne:with:`, `update`), `deleteRecord`
  (`execute:with:`). No `runReport`, no `runExport` (§7); the class comment
  says so, since the screen has the buttons.
- `Users`: the same four, never answering `user_password`, hashing on add,
  keeping the old password when the field is blank on update.
- `MyService`: `addNumbers` — `integerAt:` answers nil for a missing
  key, and Kiss answers 0 then, so `(a isNil or: [b isNil]) ifTrue: [0]`;
  `hasDatabase`. One service where Kiss has three (decision 3).
- `FileUpload`: `request uploadFileCount`, `uploadFileName: 0`,
  `uploadBytes: 0` — Kiss opens the stream and closes it; this answers the
  count, the name and the size in `outjson`, which the screen ignores, and
  logs a line, so a reader sees that the bytes arrived.
- `Init`: §3.3.

`lib/Web-Demo-Tests`, two classes:

- `WebDemoTest` (`st2026`, in the ratchet): a `RestServer` on port 0 with
  `backendDirectory: 'demo/backend'`, `documentRoot: 'demo/frontend'` and
  **no database** — `GET /` answers `index.html` as `text/html`, `GET
  /kiss/Server.js` as `application/javascript`, `GET /index.html?now=1` the
  same file, `GET /screens/CRUD/CRUD.html` 200, `GET /nothing.html` 404;
  `SystemInfo.js` contains `sameOriginBackend = true` (the stamp cannot be
  lost to a careless re-copy); `addNumbers` with both, one and no numbers;
  `hasDatabase` false; `Crud getRecords` answers `nodb`; a multipart upload
  answers its count, name and size; the two subclass names reach
  `addNumbers`; `runReport` answers *No such method* (pinning §7 down until
  it changes);
  `Login` with no database admits anybody (the dispatcher's rule).
- `WebDemoLiveTest` (`database-live` profile, **outside** the ratchet as
  that profile's comment demands): its own `st80-web-demo-test.db` filled
  by `Init`; `Login` as `smalltalk`/`password` against the hash copied verbatim
  from Kiss — the compatibility test; a wrong password refused; the phone
  CRUD round trip through `_uuid`; `Users addRecord` then a login as that
  user; a service file edited and the next call answering the new code.

And a **browser walk-through** as a gate, recorded in the commit message:
one server on 8080, `http://localhost:8080` in the browser, every screen in
§1's list exercised (login, CRUD add/edit/delete, Users, the three
`addNumbers` buttons, SQL Access, File Upload, and the mobile pages at a
narrow window), plus editing `Crud.class.st` while the server runs and
seeing the next `getRecords` change. After phase 4, the same once more the
two-server way.

## 5. The manual and the other documents

- **Chapter 19, "Serving HTTP"**: a new section *The demo application*
  after *Running a server image* — what it is, one server serving pages and
  services from one port, `smalltalk`/`password`, a table of screens against
  services, the database made on first start, the front end being Kiss's
  own with one flag stamped, that the Report and Export buttons are not
  served yet, and the thing to try: edit `Crud.class.st` and call again;
  then a paragraph on the two-server layout — the port + 1 convention, why
  a second port is another origin, and the same-host rule that admits it. A
  `stnote` on the SQLite ODBC driver, and a paragraph on the threads in
  OS-thread terms (§3.5's table). Subsections or lines in the same
  chapter for CORS (the rule, in the HTTP server section), the `Init` hook, and — with phase 5
  — *An HTTP client* and `Ollama`. Not a new chapter: the chapters are
  numbered files and the last renumbering was thirty `git mv`s.
- **Chapter 17, "The database"** or D-troubleshooting: one entry, *the demo
  says no driver*.
- **Appendix B** rows: `PasswordHash hash: verify:against:`, `Base64
  encode: decode:`, `HttpRequest origin hostName`, `HttpClient get:
  post:body:`, `Ollama`.
- **`ManualExamples>>serving`** gains doctests: `PasswordHash verify:
  'password' against: '<the schema's hash>'` (0.3 s, the compatibility
  proof in the book), a `Base64` round trip, `isHashed:` on plain text,
  `Ollama toHtml:`; `manual/README.md`'s count moves with them.
- **`doc/REST-SERVER.md`**: sections *CORS*, *Startup*, *The demo*;
  `lib/Rest-Server/PROVENANCE.md`: a paragraph on what of
  `src/main/backend` crossed and what did not (§7); **`README.md`**: one
  sentence after the web-server paragraph; **`demo/README.md`**: the
  running instructions in full, and that the front end is Kiss's own.

## 6. Decisions taken in this plan (say so if any is wrong)

1. **`demo/` directory, category `Web-Demo`**, not a `lib/Web-Demo` package —
   §2 says why. The tests are the `lib/` half.
2. **PBKDF2 in C, self-contained**, rather than plain-text demo passwords or
   OpenSSL. Kiss's `DB.sqlite` and its `Users` screen then work as they are.
3. **One `addNumbers` service, `MyService`, and one button.** The plan
   first kept Kiss's three names (`MyGroovyService`, `MyJavaService`,
   `MyLispService`) as three `RestService`s with the same twelve lines, so
   that the unchanged front end's three buttons all worked; Blake reversed
   that on 2026-08-26 — the buttons offer a choice of Groovy, Java or Lisp
   back end, which makes no sense where every service is Smalltalk. Now the
   `RestServices` screen has one button, *Call Service*, `RestServices.js`
   and `SQLAccess.js` call `services/MyService`, and the front end is
   Kiss's but for that screen too (`demo/frontend/PROVENANCE.md`).
   `WebDemoTest` checks the screen, so a re-copy from Kiss that brought
   the three back fails the suite.
4. **`Init` rather than `KissInit`** for the startup file's name — `Login`
   kept its name because it is the protocol's; this one is ours to choose.
   Confirmed by Blake, 2026-08-26.
5. **Ollama last and separable**: phases 1–3 and 6 are the demo; phase 4 is
   the two-server layout; phase 5 is two new packages that the Ollama
   screen alone needs.
6. **CKEditor stays in the copy** — Blake's call, 2026-08-26. `lib/ckeditor.js`
   is CKEditor 5 (GPL-2.0-or-later or commercial), 1.3 MB, loaded on every
   page by `kiss/bootstrap.js` and used only by the framework's `Editor`
   component; the copy is verbatim, and `PROVENANCE.md` records the licence
   beside the others.
7. **`sameOriginBackend` stamped `true`** in the copy (§2.1) rather than
   picking a port below 8000 to dodge `index.js`'s port + 1 rule: the stamp
   is what Kiss's own build does for a copy the back end serves, and it
   leaves the port free.

Decided by Blake, 2026-08-26: **no reports or exports in this pass**, and no
HTML stand-in for the PDF (§7); **CORS admits the same host on any port**,
nothing else, and is not configurable (§3.1); **the front end is copied in
and served by this server** (§2.1); **the threading model stays as built**
— a fixed worker pool, a request giving its worker back during
non-database waits (§3.5).

## 7. Not ported, and why

- **Cron** (`CronTasks/`): the demo's crontab has every line commented out,
  so the demo as shipped runs no task; `Cron.java` is 475 lines and a
  scheduler of its own. A `RestCron` is real work for another plan.
- **Reports and exports** (`Crud.runReport`, `runExport`): skipped, Blake's
  call. Kiss makes a PDF through `groff` (`Groff.java`, 571 lines, a
  subprocess) and a CSV, writes both under `<webapp>/temporary/` and serves
  the file back for `Utils.showReport` to open on the *back end's* origin.
  Doing it here needs a subprocess primitive, a way to serve files the
  back end makes when it has no document root, and a PDF — none of which
  exists — and an HTML substitute was not wanted. Until then the CRUD
  screen's Report and Export buttons answer *No such method:
  Crud>>runReport*, and `demo/README.md` and the manual say so.
- **`Ollama.chat`, streaming, tools**: §3.4.
- **`services/xxx.groovy`**: a one-off CSV import program left in the
  directory; it is not a service and nothing calls it.

### Found on the way (phase 3)

Driving the demo under a real `st80 -serve demo.im` — a loaded image, not
the bootstrap process every test runs in — found two things the suites
could not:

- **A selector sent before it was defined would not compile on the
  server.** 1983's `Parser>>makeNewSymbol:startingAt:` puts a selector no
  method has yet to the editor's user (*proceed / correct / abort*), and
  skips the menu only for an editor of nil. `TonelReader` answered no
  `editor` at all, so the Parser's `requestor editor` was a
  `doesNotUnderstand:` — which an unhandled handler answers with nil in
  the bootstrap process and with something else in a loaded image. Every
  test loaded `Login` and `Init`; `-serve` refused both with *"… is
  undeclared"*. Fixed in `lib/Tonel/Parser.extension.st` (a requestor
  that `internsNewSelectors` gets the selector interned) and
  `TonelReader>>editor`; two `TonelReaderTest`s pin it, and the probe that
  found it is the loaded-image check worth repeating for anything that
  compiles at run time.
- **An unhandled `Error` in a headless image is printed and resumed.**
  `RestServer>>start` raised on the failed `Init` load and went on to
  listen. `RestServer class>>serve` now handles the start itself: the
  error is logged, the startup process ends, and `-serve` exits 1 (*"the
  image stopped on its own"*).

### Found on the way (phase 5)

- **`connectTo:port:` tried one address.** On this machine `localhost`
  resolves to `::1` before `127.0.0.1`, Ollama listens on `127.0.0.1`
  alone, and `NET_connect` took the resolver's first address — so an Ollama
  that was up, and answered `curl`, was *connection refused* from the demo.
  `Socket class>>connectTo:port:` now asks how many addresses the name has
  (`NET_address_count`, command 20) and connects to each in turn
  (`NET_connect`'s third argument), raising the last one's error; the loop
  is in Smalltalk because a non-blocking connect learns of a refusal only
  from `connectResult`. `SocketTest>>testEveryAddressOfANameIsTried`
  listens on `127.0.0.1` alone and connects by name; `test_socket.c`
  checks the count and the index; `doc/NETWORK.md` and chapter 19 say so.

And one limitation to write into the manual (phase 6): **the image's own
compiler has no block-local temporaries** — `[:each | | row | …]` is 1983
syntax's absence, not a typo — so a service file, which that compiler
reads, declares its temporaries at the method. The bootstrap's closures
dialect accepts them, which is why the tests never noticed. Adding them to
the in-image parser is a follow-on.
- **The Kiss build machinery around the front end** (`make-frontend`, the
  WAR stamping of `app-mode` and `sameOriginBackend`, `SecurityHeadersFilter`'s
  CSP with the kernel's pinned hash): the copy is served as files, and the
  one stamp it needs is done by hand (§2.1). `index.html`'s `app-mode` stays
  `development`, so nothing is cached between reloads — right for a demo
  whose point is editing it while it runs.

## 8. Gates (Blake's standing ones, per phase)

`make test` exit 0 with every `profiles.expected` row moved; `make OM=bb`;
`make ASAN=1 unit-test` (phase 1 touches C); `make TSAN=1 unit-test`, zero
warnings (phases 1, 2, 4 and 5 touch C or the server's request and start paths, which
`test_parallel_rest` runs; start it early, it takes thirty minutes); `cd manual && python3 check.py && make && make verify`
(phase 6); `./st80 -bootstrap -profile profiles/database-live.profile
-tests` on this machine for the live half of phase 3; and the browser
walk-through of §4. Remember that `make` does not relink
`build/mt/tests/*` after a C change — `make test` before trusting a gate.

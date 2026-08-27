# Provenance

`lib/Rest-Server` and `lib/JSON-RPC-Server` are ours, BSD 2-Clause. What they
take from [Kiss](https://github.com/blakemcbride/Kiss)'s
`src/main/core/org/kissweb/restServer` — 14 files, 3,976 lines, by the same
author — is the **protocol**: the wire format a Kiss front end speaks, the
three framework methods, the session model, and one transaction per request.
No file was translated. The Java is servlet and JVM machinery from end to
end, and its shape does not survive the move to a system where loading code
is a message and a request is a green process on a native worker.

## What was kept, deliberately

- The envelope, name for name: `_class`, `_method`, `_uuid` in; `_Success`,
  `_ErrorCode`, `_ErrorMessage`, `_BootId` out; always HTTP 200; errors in the
  document. `Server.js` needs no change.
- The three core methods on the empty class, `LoginRequired`, `Login`,
  `Logout`, and the application-supplied `Login` with `login` and
  `checkLogin`, re-asked after two minutes.
- The multipart convention: `_file-N` parts, scalars prefixed with `S`.
- The binary reply: JSON, byte 3, bytes.
- The error tiers and their logging: user errors silent, log errors a line,
  server errors a line with everything; codes −1 and 2.
- Commit on return, rollback on raise, `closeConnection:` for a service that
  wants out early.
- `allowWithoutAuthentication`, `RequireAuthentication`, the environment map
  read from a configuration file, `UserInactiveSeconds`, a boot id.
- Authentication required by default when there is a database.

## What changed, and why

**A service method is unary, on a fresh instance.** Kiss's four parameters
exist because a Java method has no other way to be given its context. Here
`injson`, `outjson`, `db` and `request` are instance variables of a
`RestService` subclass, set on a new instance per request. Nothing is shared
between requests, which on this system is the property that matters.

**A service is a Tonel file, loaded on first use.** Kiss compiles `.groovy`,
`.java` and `.lisp` files in memory through three class loaders and a cache
keyed by path with `lastModified` checks. Here one class, `TonelSource`,
reads a file into the image and reloads it when its time or size changes,
and — which Kiss cannot do — writes it back when the class is edited in the
Browser.

**The server is an object, not statics.** `MainServlet`'s static fields —
an unsynchronized `HashSet` of exemptions read by every request among them —
are instance variables of `RestServer`, each under a lock or immutable after
start. `UserCache`'s statics are a `RestUserCache` the server owns.

**There is a connection pool of our own.** Kiss has C3P0; `lib/Database` has
no pool at all, by design. `RestConnectionPool` is a `SharedQueue` of open
connections, which is the idiom this system points to.

**Three faults were not ported.** A request without `_class` hung the client
(`_className.isEmpty()` on null, swallowed, no response); the exemption check
converted dots to slashes on registration and not on lookup, so nothing ever
matched; and when no login was required, a call that failed the login check
wrote a failure and then ran the service anyway. Eight more — an
unsynchronized lazy `QueueManager`, a static class loader nulled under
running threads, a one-shot init flag two threads could both take, a dead
`AtomicInteger` — are JVM matters with no counterpart here.

## What was deliberately not kept

- `GroovyService`, `JavaService`, `LispService`, `GroovyClass`,
  `CompiledJavaService`: JVM class loading. A Smalltalk class is loaded by
  compiling it.
- `QueueManager`: Tomcat's async context and a fixed `ExecutorService`. The
  worker pool and the scheduler are that.
- `StartupListener`, `SecurityHeadersFilter`, `web.xml`, `CorsFilter`:
  servlet container lifecycle. The headers are sent by `HttpServer`; CORS is
  a line of configuration when wanted.
- Server-sent events: not yet.
- TLS: a reverse proxy's job.
- The 120-second re-validation interval and the 60-second purge are kept as
  numbers; the hard-coded 600-second cache hold on loaded classes is not,
  because an image does not need to evict a class.

## Upstream

`org.kissweb.restServer`, in Kiss at the commit current on 2026-08-25:
`MainServlet.java` 937 lines, `ProcessServlet.java` 1,109, `GroovyService.java`
427, `JavaService.java` 292, `StartupListener.java` 245, `GroovyClass.java`
209, `LispService.java` 155, `UserCache.java` 140, `SecurityHeadersFilter.java`
130, `UserData.java` 106, `CompiledJavaService.java` 74, `QueueManager.java`
69, `RequestConnectionPreparer.java` 48, `LoginRequiredException.java` 21.

## The demo back end

`demo/backend` is Kiss's `src/main/backend` (592 lines: `Login.groovy`,
`KissInit.groovy`, `services/Crud`, `Users`, `MyGroovyService`,
`MyJavaService`, `MyLispService`, `FileUpload`, `OllamaQuery`,
`scripts/MyScript`) rewritten as Tonel files under category `Web-Demo`
(the three `My…Service`s as one, `MyService` — below),
method for method where the front end calls one: the same service and
method names, the same JSON in and out, the same three password tiers in
`Login`, the same `nodb` answer, the same `0` for a missing number. Kept
out: `runReport` and `runExport` (a PDF through `groff` and a CSV, served
back as files — nothing here serves a file it made), `CronTasks/` (the
demo's crontab is entirely commented out), and `services/xxx.groovy`, a
stray import program. `KissInit` is `Init`; `init` and `init2(db)` are
`init:` and `init2:`. Kiss's three `addNumbers` files — Groovy, Java, Lisp,
to show a service in each language — are one file, `MyService`, and the
front end's three buttons are one, *Call Service*: every service here is
Smalltalk, and three buttons doing the same thing offered a choice that was
not one (Blake, 2026-08-26). `demo/frontend` is otherwise Kiss's front end
verbatim — `demo/frontend/PROVENANCE.md`.

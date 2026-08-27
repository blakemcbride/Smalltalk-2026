# The demo application

Kiss's demo — a phone-list CRUD screen, a users screen, a file upload, an
"add two numbers" service, an SQL-access check, an Ollama chat — with its
back end written in Smalltalk under `backend/` and its front end, Kiss's own,
under `frontend/`, both served by one `RestServer` from one port. The back end
can be edited while the server runs: a service is a Tonel file read into the
image the first time a request names it and read again when it changes.

## Running it

You need the SQLite ODBC driver (`sqliteodbc` on Fedora, `libsqliteodbc` on
Debian) — `odbcinst -q -d` should list `[SQLITE3]`. Then, from the repository
root:

```
make demo-image                                 # bootstraps demo.im with `RestServer serve' as its startup
./st80 -serve demo.im demo/server.json          # four workers per CPU by default; -workers n to say
```

Open `http://localhost:8080` and log in as **smalltalk** with the password
**password**. Stop the server with Ctrl-C.

The first start makes `demo/DB.sqlite` (the driver creates the file on
connection) and fills it from `schema.sqlite` — Kiss's schema, its one user
renamed — through `backend/Init.class.st`. Delete `DB.sqlite` to start over.

The same server runs green, without the worker pool, from a workspace in the
desktop image:

```smalltalk
Smalltalk at: #Demo put: (RestServer fromConfigFile: 'demo/server.json').
Demo start                                     "and later, Demo stop"
```

A global rather than a temporary, so that a later doit can reach it to stop
it.

## Try this

With the server running, open `backend/services/Crud.class.st` in an editor,
change what `getRecords` answers — add a field, sort the other way — save,
and reload the CRUD screen. The next request runs the new code. The same
class is in the Browser, under `Web-Demo`, once it has been loaded, and a
method accepted there is written back to the file.

## The screens and their services

| screen | service (`backend/services/`) | needs the database |
|---|---|---|
| login | `Login.class.st` (in `backend/`) | yes |
| CRUD | `Crud` — `getRecords addRecord updateRecord deleteRecord` | yes |
| Users | `Users` — the same four; passwords stored as PBKDF2 hashes | yes |
| REST Services | `MyService addNumbers` | no |
| SQL Access | `MyService hasDatabase` | no |
| File Upload | `FileUpload upload` | no |
| Ollama | `OllamaQuery` — `isOllamaUp listModels ask`, against an Ollama at `localhost:11434` (`OllamaUrl` and `OllamaTimeoutSeconds` in `server.json` to change) | no |

The CRUD screen's **Report** and **Export** buttons answer *No such method*:
Kiss makes a PDF through `groff` and a CSV and serves the files back, and
this server does not yet serve files it made.

## Two servers instead of one

Kiss's own `serve` script runs a static server on port 8000 and its
`index.js` then expects the back end on port 8001. That works here: set
`"port": 8001` in `server.json`, leave out `documentRoot`, set
`SystemInfo.sameOriginBackend` back to `false` in the copy of the front end
the static server serves (or set `SystemInfo.backendUrl`), and the server
admits the second port's origin — same host, any port — on its own.

## The threads

In the operating system's terms, what runs under `./st80 -serve demo.im`:
one thread waits — in `poll()`, over the listening socket and every
kept-alive connection — and each arrival goes on a queue; a pool of worker
threads, four per CPU unless `-workers` says otherwise, each takes the next
queued request and runs it to its reply; a request that has to wait for
anything but the database — the rest of a slow upload, a lock another
worker holds, a model's answer — gives its worker back and is resumed by
the next free one when the wait ends. A database call holds its worker,
blocked inside the driver, and the collector is told so it does not wait.
So a request is on one thread at a time and not always the same one, idle
connections cost no thread at all, and a thousand of them are a thousand
parked processes.

## Where things are

`doc/WebDemo.md` is the plan this was built from and records the decisions;
`doc/REST-SERVER.md` is the server; `doc/HTTP-CLIENT.md` the client and
`Ollama`; `frontend/PROVENANCE.md` says which Kiss
commit the front end is and what one line was changed. The tests are
`lib/Web-Demo-Tests` (no database) and `lib/Web-Demo-Live-Tests` (SQLite,
in the `database-live` profile).

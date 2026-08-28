# The HTTP client: https, and a reply as it comes

The other direction: a program on this system calling a service somewhere
else.

```smalltalk
(HttpClient get: 'http://localhost:11434/api/tags') json.
(HttpClient post: url body: aJsonString contentType: 'application/json') body.
HttpClient new readTimeout: 60000; headerAt: 'Authorization' put: token; get: url.
(HttpClient get: 'https://api.anthropic.com/v1/models') status.
HttpClient new post: url body: json contentType: 'application/json' do: [:reply |
    reply bodyStream linesDo: [:line | Transcript show: line; cr]]
```

Four classes in `lib/HTTP-Client` — `HttpUrl`, `HttpClient`,
`HttpClientResponse`, `HttpBodyStream` — on the same `HttpCodec` and
`SocketStream` as the server, and no part of it. It is Kiss's `RestClient`
as far as `lib/LLM` needs it: a local Ollama, and the https APIs of the
language-model services, which stream their answers.

## What a reply is

An `HttpClientResponse`: `status`, `isSuccess` (200–299), `headerAt:` by
name in any case, `body` as a String of bytes, `json` as a `JSONObject`,
and `bodyStream`, the body as it arrives. A 404 is a reply, not an error —
the caller reads `status`. What *is* an error: a host that cannot be
reached or refuses (`NetError`, with the system's words), a reply that
takes longer than `readTimeout:` — thirty seconds unless told —
(`TimedOut`), a reply that is not HTTP (`NetError`), and a certificate
that does not check out (`NetError`, with OpenSSL's words — below).

## TLS

An `https` URL connects and then sends `startTls:` to the socket, which
makes it the client end of a TLS connection through OpenSSL: SNI, the
certificate checked against the system's store — what
`SSL_CTX_set_default_verify_paths` finds, which `SSL_CERT_FILE` and
`SSL_CERT_DIR` in the environment override — and its name checked against
the host asked for, an address literal as an address. Any of that failing
is a `NetError` with the reason: *hostname mismatch*, *certificate has
expired*, *self-signed certificate*, *IP address mismatch*, and *wrong
version number* from a peer that does not speak TLS at all. Neither check
can be turned off, on purpose: a client that can be asked to trust
anything will one day be asked to. The handshake is bounded by the same
`readTimeout:` as the reply, since a peer that accepts and then says
nothing would otherwise hold the process for ever.

OpenSSL is optional in the build the way ODBC is (`Makefile`, `make deps`,
`NOTLS=1`); a build without it refuses `https` by name, in the same
`NetError`, and `Socket isTlsAvailable` says which kind of build this is.
The server side of TLS is not here and is not going to be: a server on
this system sits behind a reverse proxy that terminates it, which is the
arrangement Kiss has with Tomcat and nginx. How TLS fits the non-blocking
socket — a read that must first write — is in `doc/NETWORK.md`.

## A reply as it comes

`get:do:` and `post:body:contentType:do:` read the status and the headers
and hand the reply to the block with its body still on the wire, as
`reply bodyStream` — an `HttpBodyStream` with `next`, `nextLine`,
`linesDo:` and `upToEnd`; the connection is closed when the block is done,
read or not, and the block's value is answered. `nextLine` answers a line
without its ending and **nil at the end** — not an empty String, which is
a blank line and means something in an event stream. The chunk boundaries
are invisible: a line may span two chunks and does, since a server chunks
by its buffer and not by the line. `reply body` in the block reads the
rest whole; the two do not mix. This is what a model's streamed answer is
read through — server-sent events from Anthropic and OpenAI, one JSON
document a line from Ollama — and the plain `get:` and `post:` are the
same call with a block that reads the body whole.

## One request per connection

Every request opens a connection, sends `Connection: close`, reads the whole
reply and closes. That costs a handshake per call and buys the simplest
correct reader there is, because the three ways a body can end are then all
safe:

| the reply says | the body is |
|---|---|
| a status that cannot have one (1xx, 204, 304) | empty |
| `Transfer-Encoding: chunked` | hex-sized chunks to a chunk of zero, then the trailers, dropped |
| `Content-Length` | that many bytes |
| none of those | everything up to the close |

**All four are bounded**, at 64 MB unless `maxBodyBytes:` says otherwise: the
reply comes from a server this program did not write, and a
`Content-Length: 100000000000` is a hundred gigabytes of `String`. A declared
length over the limit is refused before a byte is read; the two that declare
nothing are refused as their total passes it. A chunk size that cannot be one
— more than sixteen hex digits, or a size larger than the limit — is a
`NetError` naming it, where `ffffffffffffffff` used to reach `String new:` and
come back as "a primitive has failed".

Go's `net/http` — which is what Ollama is — chunks anything past its
buffer, so a model's answer always comes the second way; an HTTP/1.0 server
sends the fourth. `HttpClientTest` serves each kind from a listener of its
own on the loopback interface, so what is tested is the wire and not text;
and TLS as far as it can be without a TLS server, which this system has
not got: an `https` URL to a listener answering in plain text is refused
with OpenSSL's reason. Real certificates are in `lib/HTTP-Client-Live-Tests`,
in the `internet-live` profile: a good one, and three from badssl.com that
must be refused.

## What blocks

Nothing, on a worker: the socket layer's rule (`doc/NETWORK.md`) holds here
as on the server, so a request waiting a minute for a model to think parks
its process and costs the pool no thread. DNS is the one call that parks the
worker, inside `getaddrinfo`, as everywhere on this system.

**A name with two addresses.** `localhost` resolves to `::1` and then
`127.0.0.1` on many machines, and a server may listen on one of them —
Ollama, by default, on `127.0.0.1` alone. `Socket connectTo:port:` tries
every address the name resolves to, in the resolver's order, and raises the
last one's error. It did not always: it took the first address only, and an
Ollama that was up was *connection refused* from the demo until it did.

## The models

`lib/LLM` is what this client was built for: `Anthropic`, `OpenAI`,
`OpenRouter` and `Ollama`, each asked the same way — `send:`, `send:do:`,
a conversation, tools, embeddings — over this client, https and streaming
included. `doc/LLM.md` is that design; the demo's `OllamaQuery` service
(`demo/`) is one screen's end of it.

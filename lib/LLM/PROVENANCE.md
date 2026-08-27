# Provenance

`lib/LLM` is Kiss's `org.kissweb.llm` — `Anthropic.java`, `OpenAI.java`,
`OpenRouter.java`, `Ollama.java`, `QdrantClient.java`, 2,590 lines at Kiss
commit `946c777` (2026-08-18), BSD 2-Clause by the same author, the licence
of this tree — rewritten as ten Smalltalk classes. `doc/LLM.md` is the design
as built.

**What crossed.** Every public method's purpose: send, send with an image,
stream to a callback, temperature and top-p, max tokens, reasoning effort,
image detail, the model list, embeddings (OpenAI, Ollama), Ollama's chat
with a history, its tools, its `isOllamaUp` and `toHtml`, OpenRouter's
fallback models and attribution headers, Qdrant's collections, points,
search, get, delete and field index. The wire is each service's own and
unchanged.

**What changed.**

- Four classes with one face and four copies of the code became one
  abstract `LLM` and four subclasses that know only their wire.
- Only what was set is sent. Kiss sends `temperature` 0.7 and `top_p` 0.7
  by default; this sends nothing unless asked, so a model runs at its own
  defaults.
- Ollama's `send` goes to `/api/chat` with a messages array, not to
  `/api/generate` with a bare prompt — the endpoint Kiss's own comment on
  its `chat` recommends, and the one that takes tools.
- Tools, a conversation with a history and a streamed answer are on every
  service, not only where Kiss had them; the tool loop is one method.
- The endpoint URL is an instance's `url:`, not a class-wide static,
  because a class-wide setting is shared state and this system runs on
  every core.
- `send(query, imagePath)` is `send:image:`; `stream(query, onToken,
  onDone)` is `send:do:`, which answers the whole text when done rather than
  calling a second block.
- A failed call is an `LLMError` with the status and the service's body,
  not an `Exception` with a message.

**Not ported.** OpenAI's Responses endpoint and the 404 that switches to
it; the retry policy of Kiss's `RestClient`; `getLastFullResponse` as a
method (it is `lastResponse`).

**What was not in Kiss at all.** The client side of TLS (`src/net/st_socket.c`,
OpenSSL), without which none of the cloud services can be reached from
here; a reply read as it comes (`HttpBodyStream`); `Smalltalk
environmentAt:` for the keys.

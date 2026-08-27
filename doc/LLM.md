# Language models

A model behind an API, asked from Smalltalk: one prompt and its answer, the
answer as it arrives, a conversation that remembers, tools the model runs
through you, and the vector a text becomes.

```smalltalk
claude := Anthropic apiKey: (Smalltalk environmentAt: 'ANTHROPIC_API_KEY') model: 'claude-opus-5'.
claude send: 'What is the capital of France?'.
claude send: 'Write a limerick' do: [:piece | Transcript show: piece].
claude send: 'What is in this chart?' image: 'chart.png'.

talk := claude conversation.
talk ask: 'My name is Ada.'.
talk ask: 'What is my name?'.

talk tools: (Array with: (LLMTool name: 'weather' description: 'The weather in a city'
    parameters: schema do: [:args | Weather in: (args stringAt: 'city')])).
talk ask: 'Is it raining in Paris?'.

(OpenAI apiKey: key model: 'text-embedding-3-small') embed: 'a sentence'.
(Ollama new model: 'llama3.2'; yourself) send: 'Why is the sky blue?'.
```

Ten classes in `lib/LLM`: `LLM`, the abstract model, with `Anthropic`,
`OpenAI`, `OpenRouter` and `Ollama` under it; `LLMConversation`, `LLMTool`,
`LLMToolCall` and `LLMError` beside it; and `Qdrant`, a vector database for
the embeddings. Thirty-three tests in `lib/LLM-Tests` on a canned wire, and
`lib/LLM-Live-Tests` on the real one. It is Kiss's `org.kissweb.llm`
(`lib/LLM/PROVENANCE.md`), and it rests on two things this system did not
have until it was built: the client side of TLS, since every cloud service
is https only, and a reply read as it comes, since every one of them
streams.

## One class knows the common shape, four know the wires

Kiss has four classes with the same public face and four copies of the
same code. Here the face is `LLM`'s, once: the configuration (`url:`,
`model:`, `apiKey:`, `systemPrompt:`, `temperature:`, `topP:`,
`maxTokens:`, `timeoutSeconds:`), the asking (`send:`, `send:do:`,
`send:image:`, `conversation`, `embed:`, `models`, `isUp`), the HTTP, the
tool loop, and what the last call left behind (`lastStatus`,
`lastResponse`, `lastStopReason`). A subclass answers only what its
service's wire looks like:

| `LLM` asks | Anthropic | OpenAI, OpenRouter | Ollama |
|---|---|---|---|
| `chatUrl` | `/v1/messages` | `/v1/chat/completions` | `/api/chat` |
| `headers` | `x-api-key`, `anthropic-version` | `Authorization: Bearer` (+ `HTTP-Referer`, `X-Title`) | none |
| the system prompt | the `system` field | a first message of role `system` | a first message of role `system` |
| `userMessage:image:` | content blocks: `text`, `image` with a base64 `source` | content parts: `text`, `image_url` with a data URL | `content` and `images` |
| `textOf:` | the `text` blocks of `content`, joined | `choices[0].message.content` | `message.content` |
| `toolWireFor:` | `name`, `description`, `input_schema` | `{type: function, function: {name, description, parameters}}` | the same as OpenAI's |
| `toolCallsOf:` | `tool_use` blocks | `message.tool_calls`, arguments as a JSON **string** | `message.tool_calls`, arguments as JSON |
| `toolResultMessagesFor:results:` | one user message of `tool_result` blocks | one message of role `tool` per call | one message of role `tool` per call |
| `streamedTextOf:` | SSE: `content_block_delta` / `text_delta`; `message_stop` ends | SSE: `choices[0].delta.content`; `data: [DONE]` ends | one JSON document a line; `done: true` ends |
| `stopReasonOf:` | `stop_reason` | `choices[0].finish_reason` | `done_reason` |

The loop that uses them is `LLM>>answerFor:tools:do:`: post the messages;
if the reply asks for tools, append the reply's own message, run each
tool, append the results in the service's shape, and post again, at most
twenty times; when the reply is text, append it and answer it. A block
with no tools streams instead: `streamFor:do:` posts with `stream: true`
and hands each line of the body to `streamedTextOf:`, which answers the
piece of text the line carries, `''` for none, `nil` for the end, and
raises for a line that reports an error.

## What is sent

Only what was set. Kiss sends `temperature` 0.7 and `top_p` 0.7 unless
told otherwise; this sends neither unless asked, so that a model runs at
its own defaults — and Anthropic refuses a request naming both, so
`temperature:` forgets `topP:` and the other way round. `maxTokens:` is
sent when set, and always to Anthropic, which requires it: 8192 unless
told. Each service's own knobs are on its class: Anthropic's `effort:`
(`output_config.effort`), `fallbacks:` (the `fallbacks` array with the
beta header it wants) and `version:`; OpenAI's `reasoningEffort:` and
`imageDetail:`; OpenRouter's `fallbackModels:` (the `models` list the
gateway tries in order), `siteUrl:` and `siteName:`, with reasoning under
`reasoning.effort`, the gateway's one spelling; Ollama's `temperature:`,
`topP:` and `maxTokens:` go under `options`, the last as `num_predict`.

## What raises, and what does not

A reply that is not 2xx is an `LLMError` with `status` and `body` — the
service's own sentence, which is the useful part: a bad key is a 401
saying so. A service that cannot be reached is a `NetError`; one that takes
longer than `timeoutSeconds` (five minutes) is a `TimedOut`; a model that
asks for tools twenty times running is an `LLMError`. A refusal is not an
error: Anthropic answers it as an empty message with `stop_reason`
`refusal`, and it comes back as an empty String with `lastStopReason`
`'refusal'` and the reason in `lastResponse`.

A model that asks for a tool nobody gave it is told `no such tool: x` as
the tool's result and asked again, rather than failed: a model that invents
a tool reads the answer and gets over it.

## Tools

An `LLMTool` is a name, the description the model reads to decide when, a
JSON Schema for the arguments, and a block. The block gets the arguments as
a `JSONObject` and answers what the model should read: a String as it is,
JSON as JSON, anything else printed. `name:description:do:` is the tool
with no arguments. Tools belong to a conversation (`tools:`, `addTool:`),
not to the model, so one model answers plainly in one conversation and
runs tools in another.

Tool rounds are not streamed. A reply that turns out to be a tool call
has no text to stream and the request cannot know in advance, so a
conversation with tools answers whole and hands its text to the block in
one piece at the end.

## Conversations

`LLMConversation` keeps the messages in the service's own shape —
Anthropic's content blocks, OpenAI's `tool_calls` — because a tool
exchange must be echoed back exactly as the service wrote it, and sends
the whole of them with every question; the service remembers nothing.
`messages` is the history, `clear` forgets it. A conversation belongs to
one model.

## Embeddings, and Qdrant

`embed:` answers an Array of Floats — Floats and not the exact Fractions
the JSON reader answers for decimals, which are right for money and wrong
for fifteen hundred coordinates. OpenAI (and OpenRouter) post to
`/v1/embeddings`, Ollama to `/api/embed`; Anthropic has no embeddings API
and says so. `Qdrant` is where they go: a collection of vectors of one
length compared by cosine distance, records (`store:text:document:sequence:payload:`,
answering a UUID it made), a search (`search:limit:`, `search:limit:filter:`),
a record read and removed, an index on a payload field. It is Kiss's
`QdrantClient` on an instance with a `url:` rather than a class-wide one,
over `HttpClient`'s PUT and DELETE, which were added for it.

## TLS

The client side only, through OpenSSL, optional in the build the way ODBC
is: `pkg-config openssl`, then the compiler, `NOTLS=1` to mean it, and
`make deps` to report it. `Socket>>startTls:` turns a connected socket into
the client end — SNI, the certificate checked against the system's store
(`SSL_CTX_set_default_verify_paths`, which `SSL_CERT_FILE` and
`SSL_CERT_DIR` override), its name checked against the host, an address
literal checked as an address — and raises a `NetError` carrying OpenSSL's
reason when any of that fails: *hostname mismatch*, *certificate has
expired*, *self-signed certificate*, *wrong version number* from a peer
that does not speak TLS at all. Neither check can be turned off, on
purpose. `HttpClient` sends it for an `https` URL, bounded by its
`readTimeout:`, and a build without OpenSSL refuses `https` by name.

The socket stays non-blocking and the arm-wait-retry contract
(`doc/NETWORK.md`) stays the whole contract. What TLS adds is one thing: a
read may have to *write* first and a write may have to *read* first — the
handshake, a key update — so the primitive has a fourth answer beside a
count, `nil` and `false`: `-1` for *wait readable* and `-2` for *wait
writable*, whichever call was made. A plain socket never answers them.

The server side is not here and is not going to be: a server on this
system sits behind a reverse proxy that terminates TLS, which is the
arrangement Kiss has with Tomcat and nginx.

## Testing

`LLMTestCase` is a listener of the test's own that answers canned replies
— one per connection, in order — and keeps every request parsed: the
request line, the headers by lowercase name, the body. So what the
thirty-three tests check is the wire: the header each service wants, the
body a request carries and does not carry, a streamed reply cut into
pieces with the chunk boundary falling inside an event, a tool call and
its answer going back in the second request, an embedding, and what a
refusal, a rate limit and an error mid-stream become. No service is
reached. `profiles/llm-live.profile` reaches the real four with keys from
the environment, and fails naming the key that is missing;
`profiles/internet-live.profile` reaches real certificates — Anthropic's
without a key, for a 401 over a good one, and three from badssl.com that
must be refused. Neither is in `tests/profiles.expected`, for the reason
`database-live` is not.

### What the building found

- **A tool loop that asks a service is an unbounded loop.** Twenty rounds
  is the cap, tested with a listener that answers a tool call twenty-two
  times.
- **An event stream's lines cross chunk boundaries.** A server chunks by
  its buffer, not by the line; `HttpBodyStream>>nextLine` reads through
  the boundary, and the test puts one inside an event to prove it.
- **The JSON reader's exact decimals are the wrong thing for a vector.**
  `0.25` reads as `1/4`, correctly; `embed:` converts.
- **`#(true false)` is two Symbols in this dialect.** A test that wrote it
  to mean two Booleans failed, and now says so.
- **Sixty-three literals is a method's limit** in the 1983 compiler; one
  test split in two.
- **A peer that accepts and says nothing holds a handshake for ever**, and
  a client's read timeout did not cover it: `startTls:timeout:`.

## Not here

The OpenAI Responses endpoint, which some models are served by alone and
which Kiss falls back to on a 404 naming it: such a model answers here
with that 404 and its sentence. Kiss's retry policy (three tries, half a
second apart). Anthropic's thinking blocks in a conversation are echoed
back as part of the assistant message, which the API asks for, and
nothing here reads them. Server-side TLS, above.

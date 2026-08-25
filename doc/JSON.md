# JSON

RFC 8259, read and written, with numbers that are still exact when they come
out the other side.

```smalltalk
| reply |
reply := '{"name": "Ada", "born": 1815, "rates": [0.1, 0.2]}' asJson.
reply stringAt: 'name'.                     "'Ada'"
reply integerAt: 'born'.                    "1815"
(reply arrayAt: 'rates') numberAt: 1.       "1/10, and not 0.1"
reply asJsonString                          "the document, unchanged"
```

Five classes in `lib/Json` — `JSONObject`, `JSONArray`, `JSONParser`,
`JSONWriter` and `JSONError` — and one extension file that teaches every
existing class how to become a JSON value. 156 methods, and 101 tests in
`lib/Json-Tests`.

## Why not a port

The obvious source is `org.kissweb.json` in
[Kiss](https://github.com/blakemcbride/Kiss), which is where the rest of this
system's Kiss-derived code comes from. It is a fork of
[JSON-java](https://github.com/stleary/JSON-java) and every file carries
JSON.org's 2002 licence, which is MIT with one sentence added:

> The Software shall be used for Good, not Evil.

That sentence is why the OSI never approved it, why Debian will not ship it and
why Google's and Apache's policies both name it. A BSD 2-Clause tree cannot take
a line, and neither can anybody who redistributes this one. So nothing was
taken: what crossed is the shape of the API, which is not copyrightable and is
anyway the shape every JSON library in every language has had since 2002.

`lib/Json/PROVENANCE.md` records what was kept of that shape and what was
dropped.

## Numbers are exact

A JSON number with a fraction or an exponent reads as a `Fraction`, not a
`Float`. `'0.1' asJson * 10` is `1`, exactly.

This is the decision `lib/Database` made for `DECIMAL` columns, made again here
and with more force, because JSON is what money travels in now. A tenth read
through a Float is already not a tenth; the loss shows up as a penny three
statements downstream, in code that has nothing to do with reading, and the
document it came from looks correct in every log.

The parser builds the number by reading the digits as an Integer and dividing by
a power of ten, so the answer is exact by construction. An exponent multiplies
or divides by an exact power of ten. And a value that is whole answers an
`Integer` however it was written, because Smalltalk's `Fraction` reduces: `3.0`
is `3` and `1e2` is `100`, which is what every `isKindOf: Integer` downstream
depends on.

The writer is the other half. A `Fraction` prints as `(3/2)`, which is not JSON,
so `JSONWriter` writes the exact decimal itself: a fraction in lowest terms has
a finite decimal expansion exactly when its denominator is a product of twos and
fives, and the number of places needed is the larger of the two counts. Every
number a document can contain qualifies, because the parser built it by dividing
by a power of ten. The one lossy line in the package is the fallback for a
fraction a *caller* computed — a third has to be written as
`0.3333333333333333` or not at all, and not at all is worse.

`floatAt:` is how a caller asks for the lossy answer. It is spelled out at every
call site, which is the point: converting is then a decision somebody made
rather than something the reader did to them.

A JSON integer also has no width, and this one does not either. An identifier
from a database sequence past 2^53 survives the trip; a C or a JavaScript
reader, which reads every number through a double, loses its low digits.

## Strict on purpose

The grammar is RFC 8259 and nothing else. org.json's is not, and neither is
what most people have met: there a name may be unquoted, a string may be in
single quotes, a comma may trail the last member, and any bare word becomes a
String. All of those are refused here, and so are comments, a leading zero, a
lone surrogate, a raw control character inside a string, and a second document
after the first.

Leniency is not a kindness. It accepts a document that the next program in the
pipeline will reject, and the complaint then arrives from somewhere that has
never heard of this system. The sender of a malformed document is the party who
can fix it, and refusing it is how they are told.

Strictness is also invisible to every test of a valid document, which is why
over a third of `lib/Json-Tests` is documents the parser must refuse. A reader
that quietly accepted all six of the above would pass every other test in the
suite.

Two limits are refusals of a different kind, and both exist because the document
usually arrives from somewhere else:

- **512 levels of nesting.** Forty thousand open brackets is a valid document
  and a recursive descent parser meets it with forty thousand activations. This
  system heap-allocates contexts, so there is no stack to overflow first — it
  allocates until the machine stops. `maxDepth:` raises the limit deliberately.
- **An exponent of 4096.** `1e1000000000` is a twelve-character document asking
  this process to compute a billion digits, and exact arithmetic will try.

The writer has the same 512-level guard, where it catches the other version of
the same accident: a `JSONObject` put inside itself.

## Absent, and null

`at:` answers nil for a name the document does not have. That is Kiss's
divergence from org.json, where `get` throws and only `opt` answers a default,
and it is right: in JSON an absent field is how *optional* is spelled, and a
reader that raised for one would turn every read into `at:ifAbsent:`.

The cost is that a missing name and a name whose value is `null` both answer
nil. `includesKey:` and `isNullAt:` are the only things that tell them apart,
and the difference is real: `{"middleName": null}` says the sender knows about
middle names and this person has none, while a document without the name says
the sender never considered the question.

There is no `JSONObject.NULL` singleton. org.json needs one because a Java
`HashMap` cannot tell a null value from an absent key; a Smalltalk `Dictionary`
stores nil as a value perfectly well.

## Safe from more than one process at once

`doc/CONCURRENCY.md` is explicit that the base collections are unsynchronized on
purpose: paying for a lock on every access to serve the rare shared case is the
wrong default, which is Java's post-`Vector` lesson. A JSON document is not the
rare shared case. It is the object a request arrives as and is then handed to
whatever answers the request, and on this system that is genuinely another core
rather than another slot in a run queue. So `JSONObject` and `JSONArray` each
hold a `Mutex`, and every message that touches their contents takes it.

**What the lock promises.** Two processes putting two names cannot lose one of
them, and cannot both add the same name to the order list — which is the race
that matters most, because the value is still reachable by `at:` and only the
enumeration and the writer can see that anything is wrong. A process reading
while another writes sees the value before or the value after, never a
`Dictionary` caught half-way through growing.

**What it does not promise.** A *sequence* of messages is not atomic.
`object at: 'n' put: (object at: 'n') + 1` from two processes loses an
increment, exactly as it would on any other object. A caller that needs several
operations to happen together holds its own lock around them; `at:ifAbsentPut:`
is provided because that particular sequence is common enough to be worth doing
properly.

**No block of the caller's runs inside the lock.** `do:`, `keysDo:`,
`keysAndValuesDo:`, `collect:` and `select:` take a snapshot under the lock and
enumerate outside it, so the block sees the members as they were when it
started. Two reasons, either sufficient: `Mutex` is not re-entrant and says so
by raising, so `json do: [:each | json at: ...]` would be a trap laid for the
most natural code somebody could write; and a lock held across a block that is
not ours is a lock held for as long as somebody else's code takes. The same rule
puts `asJsonValue` outside the lock in `at:put:` — a caller's class may
implement it, and running a stranger's code inside our lock is how a library
ends up holding one across a database call.

**Two locks are never held at once.** `=` snapshots both sides before comparing
anything, so `a = b` and `b = a` on two cores cannot deadlock. `postCopy` gives
the copy a *new* `Mutex` after taking the original's to copy the collections:
one lock for two objects is over-strict rather than wrong, but it would make a
process working on the copy block one working on the original, forever, for no
reason either could see.

`tests/unit/test_parallel_json.c` is the gate: 31 threads put 500 times each
into one `JSONObject` over the same ten names — so the answer afterwards is
exactly ten in both directions — 31 threads add 500 elements each to one
`JSONArray`, and 31 threads write and re-read one shared document 100 times each
with no torn document among the 3,100. Checked against a build with the locks
taken out, it does not answer a wrong number: it *hangs*, because a
`HashedCollection` whose invariants two writers have broken can have no nil slot
for the scan to stop at.

## Names keep their order

A `JSONObject` writes its names in the order they were first put in, and keeps
its own `OrderedCollection` of them beside the `Dictionary` to do it.

org.json uses a `HashMap` *on purpose*, so that nothing can depend on the order.
1983's `Dictionary` would have gone further and iterated in hash order, which
means a document read and written back would come out shuffled and two runs of
one program would produce two different files. JSON says the order is
insignificant; `diff` says otherwise, and so does anybody comparing two
generated documents by eye.

Order is kept for writing and ignored for comparing. `=` answers true for two
objects with the same names and values whatever order they were put in, because
a test that had to predict the order of a document it did not write would be a
test of the parser's bookkeeping rather than of the document.

## The wrong type raises

`stringAt:` of the number 42 does not answer `'42'`. org.json and Kiss both
convert — `getString` of a number, `getLong` of a string — and it is the kind of
helpfulness that hides the sender's bug in the reader's code. A document that
says `42` and one that says `"42"` are different documents, and the day one
turns into the other is the day to be told.

The one conversion kept is upward and lossless: a number that is whole answers
as an `Integer` from `integerAt:` even when it was written `3.0`, because a
sender that writes a count that way is wasteful rather than wrong. `3.7` raises,
because truncating it in silence is how a quantity becomes wrong by one.

## Text is UTF-8

A String in this system is bytes, so an escape has to become bytes: `\u0041`
answers one byte and `\u00e9` answers the two that are e-acute in UTF-8. The
answer is a String this system can hold, compare and write to a file, rather
than one whose size is the number of characters the sender thinks it sent.

A surrogate pair — which is how JSON, having inherited UTF-16 from JavaScript,
spells every emoji and a great deal of Asian text — is read as the one character
it encodes. Decoding the halves separately would produce six bytes that are not
valid UTF-8 and that no other program will read back. A lone surrogate is
refused, because there is no encoding of one.

Going the other way, a byte above 126 is written as itself. Escaping it again
would double the size of every document not written in English, to no purpose:
the two spellings are the same text to every reader.

`\n` is a line feed, 10, and stays one. This image's `Paragraph` breaks lines on
the carriage return, 13, because the Alto did — but translating would be
indefensible in both directions, since the document said 10 and a document
written back after translation would differ from the one that came in.
`lib/Files-Fixes` translates where text meets a file, which is where it belongs.

The pretty printer breaks its own lines with `Character nl` for the same reason,
following `DbSchemaGraph>>storeOn:`. JSON does not care — whitespace between
tokens is insignificant — so the only party affected is the person reading the
output, and they are not on an Alto.

## Conversion is a message

Everything reaches `JSONWriter` through `asJsonValue` first, so by the time a
value is written it is a `JSONObject`, a `JSONArray`, a String, a Number, true,
false or nil, and the writer's switch is total.

That is the whole reason the conversion is a separate step and a message rather
than a switch inside the writer. An `Array` of `Rectangle`s fails where somebody
put a Rectangle in an Array, naming the Rectangle — not later, at the write,
where all that can be said is that something in the tree could not be written.
In a web application those two moments are a request apart, and only the first
has the code that was wrong on the stack.

It also means a class this package has never heard of joins in by implementing
`asJsonValue`, which is how a `Person` becomes a document without `lib/Json`
knowing what a Person is.

`Dictionary>>asJsonValue` answers a `JSONObject` and is not merely a
convenience: `Collection>>do:` over a Dictionary answers its *values*, so
without the override a Dictionary of three names would have become a
three-element array with the names thrown away, in hash order, silently.

## The classes

| | |
|---|---|
| `JSONObject` | names and values, in the order they were put; the seven typed accessors |
| `JSONArray` | values in order; the same seven by index; enough collection protocol to be useful |
| `JSONParser` | RFC 8259, recursive descent over a stream, bounded in depth and exponent |
| `JSONWriter` | compact and indented, exact decimals, the escaping |
| `JSONError` | every refusal, carrying the character position |

`JSONArray` is not an `OrderedCollection` subclass and `JSONObject` is not a
`Dictionary` subclass, for one reason: everything that goes in goes through
`asJsonValue`, and inheriting `add:` and `at:put:` from a collection that takes
anything would put a hole in that at the two places it matters most.

Two places `JSONArray` therefore does not behave like the collection it
resembles, both deliberate and both tested:

- `collect:` answers an `OrderedCollection`. The block may answer anything at
  all and a JSONArray may hold only JSON values, so answering a JSONArray would
  make `collect: [:each | each class]` raise on a perfectly reasonable block.
  `select:` *does* answer a JSONArray, because every element of its answer was
  already an element of the receiver.
- An index past the end raises. A name that is not in an object is ordinary; an
  array has a length, and reading past it is a mistake in the reader's
  arithmetic — exactly the case where answering nil buries the fault a level
  down. `at:ifAbsent:` is there for the reader who really does not know.

## A limit worth knowing: many names are quadratic

A JSON object with a few dozen names is what almost every document has, and this
is not about those. An object used as a *map*, with thousands of names, is
quadratic to build and to read — and the reason is 1983's `String>>hash`:

```smalltalk
hash
    | l m |
    (l _ m _ self size) <= 2 ifTrue: [ ... ].
    ^(self at: 1) asciiValue * 48 + ((self at: (m - 1)) asciiValue + l)
```

The first character, the second-to-last character, and the length. Nothing else.
Two hundred names of the form `key1`..`key200` produce **eleven** distinct hash
values, so the `Dictionary` degenerates to linear probing along one chain:

| names | distinct hashes | to build |
|---|---|---|
| 124 | 13 | 7 ms |
| 248 | 13 | 34 ms |
| 496 | 26 | 137 ms |
| 992 | 46 | 697 ms |

Four times the time for twice the names, which is the signature. This is a
property of the 1983 library rather than of this package — every `Dictionary`
and `Set` keyed by long similar Strings has it, and `lib/Database` does not
because a table has a few dozen columns — but a JSON reader is the most likely
thing in the image to meet it, so it is recorded here. It found this package's
own parallel test first: 15,500 names made that test a benchmark of a hash
function rather than a test of a lock, at 143 seconds for sixteen workers.

Fixing it means giving `String` a hash that reads the whole string, in `lib/`,
which changes the iteration order of every hashed collection in the image and
therefore belongs to its own piece of work with its own ratchets.

## Testing

101 tests in `lib/Json-Tests`, in the `st2026` profile and therefore in every
profile that requires it. They need nothing this machine may not have, because
a JSON document is a String — unlike `lib/Database`, half of whose tests need a
driver and live in a profile of their own.

The 43 examples in Chapter 18 of the manual are doctests in
`manual/examples/ManualExamples.class.st`, run by `make verify` there, so every
answer the book prints is checked against a live image.

`tests/unit/test_parallel_json.c` is the parallel gate described above, and runs
under ThreadSanitizer with the rest.

### What the tests found

**`OrderedCollection>>removeIndex:` is not a public index.** 1983 keeps an
OrderedCollection's elements in the middle of a larger Array, and `removeIndex:`
takes an index into *that* Array — it is private, and both of its callers walk
from `firstIndex`. Handed a public index it removes an element from somewhere
else entirely: `removeAt: 2` on `[1,2,3]` answered 2, which is right, and left
`[2,3]` behind, which is not. `JSONArray>>removeAt:` now uses `removeFirst` and
`removeLast` at the ends and rebuilds for the middle.

**A JSON document with many names is quadratic**, and the parallel test found
it by being slow rather than by being wrong. See the section above.

**An extension method is a protocol, and the Browser shows it.** Adding
`Boolean>>asJsonValue` moved a check in `tests/unit/test_image.c` that counts
Boolean's protocols from five to six. That is the ratchet working: an extension
the Browser does *not* show is a method nobody can find.

## Provenance

Ours, BSD 2-Clause. See `lib/Json/PROVENANCE.md` for the licence argument above
in full, and for the list of what org.json has that this deliberately does not:
`JSONPointer`, `JSONML`, `XML`, `CDL`, `Cookie`, `HTTP` and `JsonPath`. None of
them are JSON.

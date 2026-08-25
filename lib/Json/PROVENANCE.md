# lib/Json

Ours, BSD 2-Clause, written for this system. **No code was copied from
anywhere**, and for this package that is not only the usual claim.

## Why nothing could be copied

The obvious place to port from is `org.kissweb.json` in
[Kiss](https://github.com/blakemcbride/Kiss), which is a fork of
[JSON-java](https://github.com/stleary/JSON-java) and carries JSON.org's 2002
licence in every file. That licence is MIT with one sentence added:

> The Software shall be used for Good, not Evil.

which is why the OSI has never approved it, Debian will not ship it, and
Google's and Apache's policies both name it. A tree that is BSD 2-Clause
throughout cannot take a line of it, and neither can anybody who redistributes
this one.

So what crossed over is the *shape of the API* — which is not copyrightable and
is anyway the shape every JSON library in every language has had since 2002 —
and nothing else. Every line here was written against RFC 8259.

## What was kept from Kiss's shape

**The two containers and the typed accessors.** `JSONObject` and `JSONArray`,
each with an accessor per type: `getString`/`getInt`/`getBoolean`/
`getJSONObject`/`getJSONArray` became `stringAt:`, `integerAt:`, `booleanAt:`,
`objectAt:`, `arrayAt:`, plus `numberAt:` and `floatAt:`. That breadth is the
reason to port at all, and it is what makes reading a document a line at a time
instead of a type test at a time.

**An absent name answers nil rather than raising.** Kiss's own divergence from
org.json, where `get` throws and only `opt` answers a default. It is right: in
JSON an absent field is how *optional* is spelled, and a reader that raises for
one turns every read into `at:ifAbsent:`.

**`at:put:` fills a gap in an array with nulls.** org.json's behaviour, kept
because an array built by index from a source with holes has to be filled with
something and null is the only thing a JSON array can be filled with.

## What was deliberately not kept

**The lenient grammar.** org.json accepts an unquoted name, a single-quoted
string, a trailing comma, and any bare word as a String. Every one of those
accepts a document that the next program in the pipeline will reject, and the
complaint then arrives from somewhere that has never heard of us. `JSONParser`
is RFC 8259 and nothing else. Over a third of `lib/Json-Tests` is documents it
must refuse, because strictness is invisible to every test of a valid document.

**Type coercion in the accessors.** Kiss's `getString` of the number 42 answers
`'42'` and its `getLong` of the string `"42"` answers 42. A document that says
`42` and one that says `"42"` are different documents; the wrong type raises
here, naming the key, the type found and the type wanted.

**Numbers as doubles.** This is the one respect in which the port is better than
its source, and it is the same divergence `lib/Database` made for `DECIMAL`
columns: `1.5` reads as the exact `Fraction` 3/2, and `0.1` multiplied by ten is
1 rather than 1.0000000000000002. JSON is what money travels in now. `floatAt:`
is how a caller asks for the lossy answer, and asking is then a decision
somebody made rather than something the reader did to them.

**Single-threaded containers.** org.json's are not synchronized and org.json is
right not to synchronize them: a Java `HashMap` behind a servlet is per-request.
Here a document is handed between processes that are on different cores, so
`JSONObject` and `JSONArray` each hold a `Mutex` and every message that touches
their contents takes it. Enumeration works from a snapshot so that no block of
the caller's runs inside the lock — `Mutex` is not re-entrant and detects
re-entry by raising, and `json do: [:each | json at: ...]` is the most natural
thing somebody could write. `doc/JSON.md` has what the lock promises and what it
does not.

**`JSONObject.NULL`.** org.json needs a null singleton because a Java `HashMap`
cannot tell a null value from an absent key. A Smalltalk `Dictionary` stores nil
as a value perfectly well, so `{"a":null}` round trips as one name whose value
is nil and `includesKey:` still answers true for it. `isNullAt:` is the rest of
the distinction.

**The insertion order of names is kept.** org.json uses a `HashMap` *on purpose*
so that nothing can depend on the order. 1983's `Dictionary` would have gone
further and iterated in hash order, so a document read and written back would
come out shuffled and two runs of one program would produce two different files.
JSON says the order is insignificant; `diff` says otherwise.

**`JSONML`, `XML`, `CDL`, `Cookie`, `HTTP`, `JSONPointer`, `JSONStringer` and
`org.kissweb.JsonPath`.** None of them are JSON. They are conversions to and
from other formats, and a query language, and they belong in packages of their
own if they are ever wanted.

## Upstream, for reference only

`Kiss/src/main/core/org/kissweb/json`, 7,552 lines across nineteen files, of
which `JSONObject` (2,080), `JSONArray` (1,511) and `JSONTokener` (540) have
counterparts here. This package is 156 methods across five classes and one
extension file.

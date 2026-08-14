# Collections-Unordered

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/Collections-Unordered` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-13 |

## Files taken

All 21 `.st` files, byte for byte.

## Local edits

None.

## Status: a working substitution

`profiles/pharo-collections.profile` builds an image whose `Set`, `Dictionary`,
`IdentityDictionary`, `IdentitySet`, `Bag` and `MethodDictionary` are Pharo's —
272 classes, 5263 methods, 43 initializers run, 0 unfinished, no
doesNotUnderstand at all — and the `st2026` suites pass on it, 12 of 12. That
number is the point: it is the same score the unsubstituted profile gets, so
the substitution is invisible to everything that was already working.

The package's **own** tests are loaded now, from `Collections-Abstract-Tests`
and `Collections-Unordered-Tests`, and **469 of 469 pass**. They were at 115
when they were first wired in, and the grind from there found four faults, two
of them nowhere near a collection:

- a block with a trailing period — `[:x | x. ]` — answered **nil** rather than
  x, in the compiler;
- `Object>>copy` answered a bare `shallowCopy` and never sent `postCopy`, so
  every copy of a Pharo collection **shared its storage** with the original;
- `becomeForward:` did not exist, so `MethodDictionary>>grow` could not grow;
- the loader refused ephemerons, so `WeakKeyAssociation` could not be built.

So what is proved here is both halves: the 1983 library runs on Pharo's
collections, and Pharo's collections pass their own suites.

## Why it is harder than Announcements or Chronology

Those were Tier 1: they added classes the 1983 library had never heard of, so
nothing existing could be broken by their arrival. This replaces the classes
the whole 1983 library and the C bootstrap stand on.

**The shapes differ.** 1983's `Set` is `Collection variableSubclass:` and keeps
its elements in the *indexed* part. Pharo's keeps them in an `array` instance
variable. 1983's `Dictionary` is a `Set` subclass holding Associations in the
indexed part; Pharo's is a `HashedCollection` subclass holding them in `array`.
Anything reaching inside — and the C bootstrap does, at four sites — is
reaching for a different place afterwards.

**The hierarchy differs.** 1983 has no `HashedCollection` at all:
`Collection → Set → Dictionary → IdentityDictionary → MethodDictionary`.
Pharo has `HashedCollection` under `Collection`, with `Set` and `Dictionary`
as siblings.

**`MethodDictionary` is the exception that makes the turn thinkable.** The VM
reads method dictionaries from C — tally at instance variable 0, the value
array at 1, keys in the indexed part from word 2 — and Pharo's
`MethodDictionary` has exactly that shape, for exactly the reason 1983's did:
one array of selectors and one of methods rather than thousands of
Associations. The one class the interpreter cannot afford to have change is
the one that does not.

## The root cause, once it was found

Every symptom — 179715 `nil generality`, 6024 `nil at:ifAbsent:`, eleven
unfinished initializers, `Smalltalk at: #Object` **dumping core** — was one
fault with a very long reach.

`HashTableSizes class>>initialize` searches for primes with good hashing
properties (Valloud's criteria: not close to dividing the hashMultiply
constant, not dividing 256**k ± a). Under this interpreter that search does not
finish in two hundred million bytecodes. So `sizes` stayed nil, `sizeFor:`
answered nil, and **every collection created through `new:` was built with a
nil capacity** — including the `SystemDictionary` holding every global in the
image. A half-made dictionary is what dumped core, and the nil storm was that
same nil arriving somewhere far away and being sent something.

`lib/Collections-Compat` writes the table down as a literal instead. A constant
that takes forty seconds to recompute on every image build is a constant, not a
computation, and the class initializer is now on the loader's skip list with
that reason.

Three smaller faults were real and are fixed:

- `Object>>asCollectionElement` and `Object>>enclosedElement` are **one
  mechanism** — Pharo's `Set` wraps on the way in and unwraps on the way out —
  and adding either without the other is worse than adding neither, because a
  Set whose elements can be stored and not read answers nil for all of them. I
  added the first alone and made things quietly worse for a round.
- `SystemDictionary>>at:put:` is the one 1983 method that reaches inside a
  Dictionary, and is rewritten in `lib/` against Pharo's protocol. It must keep
  *reusing* an existing Association rather than replacing it: a compiled method
  holds the Association, not the dictionary, which is what makes reading a
  global lock-free.
- `Integer>>isPrime` and `SequenceableCollection>>at:ifAbsent:`, both simply
  absent.

## Two predictions, both wrong

Recorded because the predictions were plausible and cost real time.

**Predicted:** the C bootstrap builds `Dictionary` instances at four sites, and
the two dialects keep their Associations in different places, so those sites
would build malformed objects. **Measured:** all four go through the image's own
`new:` and `add:` and are layout-agnostic by construction.

**Predicted:** `MethodDictionary` would block it. **Measured:** importing
Pharo's changed the method count and nothing else; the errors were identical
before and after. It is kept because it has exactly the shape the VM requires
and is the right long-term answer, but it was not what was wrong.

## What actually found it

Making the VM say more, rather than reasoning harder. The loader now names
*every* unfinished initializer instead of only the first, and
`MessageNotUnderstood` now names the receiver's class as well as the selector.
`Message not understood: at:ifAbsent:` became `(sent to a UndefinedObject)`,
and that one word turned a wall of DNUs into a single question — *what is
answering nil?* Both improvements are in `lib/` and `src/` and apply to
everything, not just this turn.

## Excluded

`ManifestCollectionsUnordered` is lint metadata for a browser we do not have.
`OrderedDictionary`, `OrderedIdentityDictionary` and `KeyedTree` extensions name
classes this system does not have; the classes themselves live in other
packages.

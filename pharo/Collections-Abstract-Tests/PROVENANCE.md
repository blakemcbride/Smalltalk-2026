# Collections-Abstract-Tests and Collections-Unordered-Tests

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/Collections-Abstract-Tests`, `src/Collections-Unordered-Tests` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-13 |

## Files taken

All 55 and all 30, byte for byte. 52 of the 55 are **traits**: Pharo's
collection tests are a lattice of shared suites — `TAddTest`, `TCloneTest`,
`TSizeTest` — that each concrete collection composes.

## Status

**297 tests run, 115 pass**, and the number is in `tests/profiles.expected` so
`make test` holds it. The substitution underneath is sound — the `st2026`
suites pass on this image unchanged — and what the remaining 182 measure is
Collection and Stream protocol Pharo assumes and 1983 has no name for. That is
the same grind that took Chronology from 275 to 633.

## What loading them required

Three things in the loader and the compiler, none of them about collections:

**Trait exclusion.** `BagTest` composes
`(TCreationWithTest - {#testOfSize})` — take that suite except those
assertions, which do not hold for a Bag. The loader implemented `+` and
refused `-` and `@`. `-` is now implemented: it narrows what a class takes and
can be honoured exactly. `@` is still refused, because aliasing *invents* a
selector, and a class that loaded cleanly with a method under the wrong name is
worse than one that refused to load. Without `-`, `BagTest` could not load, and
without `BagTest` neither could `IdentityBagTest` — one operator cost the whole
package.

**`#classTraits` comparison.** The loader checks that `#classTraits` is the
mechanical mirror of `#traits` and refuses anything else. An instance-side
exclusion has no class-side counterpart — Pharo writes
`(T - {#x})` on one and `T classTrait` on the other — so the comparison now
normalises exclusions away first.

**Three buffers, all 256.** `st_ston_pair.value`, `boot_class.traits`, and a
staging buffer inside the STON reader. `BagTest` writes 329 characters of
`#traits` and 471 of `#classTraits`. Widening two of the three changed nothing,
which is exactly how it looked from outside: the value was already cut before
it reached them.

**A negative literal at the start of a method body.** `testAtLeast` opens with
`-1000 to: 1000 do: [...]`, and the lexer decides `-` from the preceding token,
which here is the method pattern's own identifier — indistinguishable from
`foo - 1000`. The parser knows, so it now says so: `LEX_begin_statement`. This
only arises in a method with no temporaries; after `| a b |` the predecessor is
a bar, which the rule already accepted.

## Excluded

`HashTableSizesTest` tests the computed prime table that
`lib/Collections-Compat` deliberately replaces with a literal — see the
`Collections-Unordered` provenance for why the search does not finish here. Its
tests are about an implementation this system does not have.
`ManifestCollectionsUnordered` is lint metadata. `KeyedTree`,
`OrderedDictionary` and `OrderedIdentityDictionary` name classes that live in
other packages.

# Collections-Support

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/Collections-Support` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-14 |

## Files taken

One: `WeakValueAssociation.class.st`, byte for byte.

## Local edits

None.

## Why one file and not the package

`Collections-Support` is where Pharo keeps `Association`, `LookupKey`,
`Link`, `Slot` and their kin — the classes 1983 already has and the C
bootstrap reads directly. Importing it whole is a Tier 2 substitution of
`Association`, which is a ratchet turn of its own and is not this one.

`WeakValueAssociation` is here because `Collections-Weak` is incomplete
without it: `WeakValueDictionary>>at:put:` sends
`WeakValueAssociation key:value:`, and the class lives in a different Pharo
package from the dictionary that needs it. So this is not a new dependency —
it is the rest of a dependency already taken. Without it,
`WeakValueDictionary new at: 1 put: 2` answers a `doesNotUnderstand:` on nil.

It is a `LookupKey` subclass declared `#type : 'weak'` with one indexed
field, and 1983's `LookupKey` has the same shape it expects: a `Magnitude`
with a `key`. Nothing in it reaches for anything else Pharo-specific.

## Status

Loaded by `profiles/pharo-collections.profile`, which needs it because
`DictionaryTest>>testOtherDictionaryEquality` compares `Dictionary` against
every other dictionary class in the image and names `WeakValueDictionary` as
one of them.

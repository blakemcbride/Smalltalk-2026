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

## Status: imported and loading, NOT yet a working substitution

This is the first **Tier 2** turn of the ratchet, and it does not yet produce
an image that runs. It is checked in because the import and the measurement
are the work; what remains is named below rather than guessed at.

`profiles/pharo-collections.profile` loads it — 272 classes, 5230 methods —
and the resulting image is broken: nine class initializers do not finish,
and the interpreter reports `nil is not a boolean` and a
`does not understand #=` during them.

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

## What blocks it, measured rather than predicted

I predicted the blockers before measuring them and was wrong about both, which
is recorded here because the prediction was plausible and cost an hour.

**Predicted:** the C bootstrap builds `Dictionary` instances at four sites, and
1983's Dictionary keeps Associations in the indexed part where Pharo's keeps
them in `array` — so those sites would build malformed objects. **Measured:**
all four go through the image's own `new:` and `add:` and are layout-agnostic
by construction. Nothing to fix.

**Predicted:** `MethodDictionary` would block it, needing Pharo's from
`Kernel-CodeModel`. **Measured:** importing that changed the method count and
nothing else — the errors were identical before and after. It is imported here
because it is the right long-term answer and it loads cleanly, but it was not
what was wrong.

**What is actually wrong is missing protocol**, the same shape as the
Chronology round and not an architectural problem at all. Counted from a load:

```
179715  generality          1983's numeric coercion, reached with a
                            non-Number operand -- one root cause, looping
  6346  at:ifAbsent:
  6345  activeController
   206  enclosedElement     Pharo's Set unwraps elements through this
```

Three are now fixed in `lib/` and are gone from the count: `Integer>>isPrime`,
which `HashedCollection class>>sizeFor:` reaches through `HashTableSizes`;
`Object>>asCollectionElement`, which Pharo's `Set` asks of every element on the
way in; and `SequenceableCollection>>at:ifAbsent:`, which 1983 answers on
Dictionary and not on indexed collections.

The last of those is worth a note, because it did **not** move the count. The
method is present and works — `#(1 2 3) at: 9 ifAbsent: ['none']` answers
`'none'` in the loaded image — and the bootstrap still reports 6346
`at:ifAbsent:` failures, byte-identically to the run before. So that receiver
is not a `SequenceableCollection`, and identifying it is the next concrete
step rather than adding more protocol on a guess.

The eleven unfinished initializers are now all named rather than just the
first (`BitEditor FormEditor ChangeListController NotifierController
ProjectController ScreenController StandardSystemController ParagraphEditor
StringHolderController VariableNode HashTableSizes`). Nine are MVC
controllers and probably share one cause; `HashTableSizes` is Pharo's own and
is in the critical path, since `sizeFor:` consults it on every collection
created.

The `generality` count is identical across runs, which says one bounded loop
rather than many scattered failures — that is where the next session should
start, and `<16r3A> does not understand #=` in the same run is probably the
same object.

## Where this leaves the turn

Groundwork, honestly labelled. The package is imported and loads, two protocol
gaps are closed, the blocker list is measured rather than guessed, and nothing
else regressed — `st2026`, `pharo-announcements`, `pharo-time` and
`pharo-weak` are all still at their recorded scores.

`pharo-collections` is deliberately **not** in `tests/profiles.expected`: it
does not produce a working image, and recording a score for it would be
recording a fiction.

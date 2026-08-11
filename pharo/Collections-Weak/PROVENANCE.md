# Collections-Weak

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/Collections-Weak` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-10 |

## Files taken

`package.st`, `WeakArray`, `WeakIdentityKeyDictionary`, `WeakIdentitySet`,
`WeakIdentityValueDictionary`, `WeakKeyAssociation`, `WeakKeyDictionary`,
`WeakOrderedCollection`, `WeakSet`, `WeakValueDictionary`.

## Files deliberately not taken

`ManifestCollectionsWeak.class.st` — a `PackageManifest` subclass carrying Pharo's own
code-critic annotations. It is tooling metadata about the package rather than part of
it, and `PackageManifest` is not a class this system has.

## Local edits

**None.** The files are byte-for-byte as upstream.

That is the point of the directory: this package is the first real test of whether
Pharo's source runs here *unmodified*. Anything that has to change to make it load is
a defect in this system, recorded and fixed here rather than patched there — and if a
local edit ever does become necessary, it is listed in this section before it is made.

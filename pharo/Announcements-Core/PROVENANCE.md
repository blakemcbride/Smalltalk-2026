# Announcements-Core

| | |
|---|---|
| upstream | https://github.com/pharo-project/pharo |
| commit | `490f37c591f644c78cbe22c6b6a5845fc0f81fc7` |
| path | `src/Announcements-Core` |
| licence | MIT, with parts under Apache-2.0 — see `../LICENSE.pharo` |
| imported | 2026-08-12 |

## Files taken

All 13 `.st` files, byte for byte.

## Local edits

None yet.

Every edit recorded here is one that must be made again at the next
re-import, for ever; the VM or the loader is the cheaper place to
accommodate Pharo wherever that is possible.  See the discipline note in
`doc/PLAN-PHARO.md`.

## Why this package first

The plan's Tier 1 -- no VM contract, nothing that needs the object model
work of Phase F.  It has its own SUnit suite in `Announcements-Core-Tests`,
which is the point: the ratchet's unit of progress is a package whose own
tests pass, and those tests were written by somebody else.

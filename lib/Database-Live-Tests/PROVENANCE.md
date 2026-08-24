# lib/Database-Live-Tests

Ours, BSD 2-Clause. Nothing imported.

Kept out of the `st2026` profile on purpose: these need an ODBC driver manager
and a driver, which a machine building this system cannot be assumed to have.
See the class comment on `DbLiveTest` and the note in
[`tests/profiles.expected`](../../tests/profiles.expected) on why a suite that
cannot run where it is checked must not be in the ratchet.

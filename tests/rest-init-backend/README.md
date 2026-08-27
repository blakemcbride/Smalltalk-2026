A back end with an `Init` class, for the tests of the server's startup hook:
`init:` and `init2:` record what they were given, and with a database `init2:`
writes a row -- or writes it and then raises, so that its absence afterwards
is the proof of the rollback.  `services/Marks.class.st` counts the rows.
The main test back end, `tests/rest-backend`, deliberately has no `Init`.

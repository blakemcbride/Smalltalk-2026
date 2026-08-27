"
The Database package against a real database.

Separate from st2026 because these tests need something a machine building
this system cannot be assumed to have: an ODBC driver manager, a driver, and
somewhere to write a file.  `make test' must run everywhere, so it runs the
offline suites -- the SQL generator, the join graph, the exact decimals --
and this profile runs the half that needs a database.

    ./st80 -bootstrap -profile profiles/database-live.profile -tests

SQLite through ODBC, because it is the one database that needs no server: the
whole fixture is a file the test makes and deletes.  On Fedora that is
`sudo dnf install unixODBC-devel sqliteodbc'; elsewhere the packages are
named unixodbc-dev and libsqliteodbc, or ask `make deps'.

lib/Rest-Server-Live-Tests is here for the same reason: the REST server's
one promise about a database -- a request that returns is committed, one
that raises is rolled back -- can only be checked against a database.

There is no line for this profile in tests/profiles.expected, and there must
not be one.  The ratchet's whole purpose is that a suite which stops being
found reports a perfect score; a suite that cannot run on the machine
checking it would report exactly that, every time, and teach a reader to
ignore the file.
"
Profile {
	#name     : 'database-live',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#packages : [ '../lib/Database-Live-Tests', '../lib/Rest-Server-Live-Tests', '../lib/Web-Demo-Live-Tests' ]
}

"
The HTTP client's TLS against real certificates, which only the internet
has.

Separate from st2026 because a machine building this system cannot be
assumed to be on the internet, and `make test' must run everywhere; the
same reason database-live is separate.  These tests reach api.anthropic.com
without a key -- a 401 over a good certificate is the whole point -- and
badssl.com for the certificates that must be refused: another name,
expired, self-signed.

    ./st80 -bootstrap -profile profiles/internet-live.profile -tests

There is no line for this profile in tests/profiles.expected, and there
must not be one: a suite that cannot run where it is checked reports a
perfect score every time.
"
Profile {
	#name     : 'internet-live',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#packages : [ '../lib/HTTP-Client-Live-Tests' ]
}

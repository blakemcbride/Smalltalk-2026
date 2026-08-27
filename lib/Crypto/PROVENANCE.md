# Provenance

`lib/Crypto` and `src/crypto/` are ours, BSD 2-Clause.

**The stored password format** is [Kiss](https://github.com/blakemcbride/Kiss)'s
`org.kissweb.PasswordHash` (146 lines, same author): `pbkdf2$<iterations>$<Base64
salt>$<Base64 key>`, PBKDF2-HMAC-SHA256, a 16-byte salt, a 256-bit key, 600,000
rounds, Base64 without padding, a constant-time compare, `isHashed` and
`needsRehash`. `PasswordHash.class.st` keeps the format and the four operations
name for name, so that a `users` table Kiss wrote logs in on this server without
conversion; `tests/unit/test_crypto.c` and `PasswordHashTest` both verify the
hash Kiss's own `schema.sqlite` stores for the demo user. `hash:iterations:` is
an addition.

**The arithmetic** — `src/crypto/st_crypto.c` — is written from the standards
and not from any library: SHA-256 from FIPS 180-4, HMAC from RFC 2104, PBKDF2
from RFC 8018. Its answers are checked in `tests/unit/test_crypto.c` against
FIPS 180-4's examples, NIST's SHA-256 vectors, RFC 4231's HMAC cases 1, 2, 3
and 6, the PBKDF2-HMAC-SHA256 values every implementation quotes, RFC 7914's
64-byte example, and the Kiss hash above; every one was recomputed with an
independent implementation (Python's `hashlib`, which is OpenSSL) before being
written down. The one departure from a plain transcription — precomputing the
two HMAC states so an iteration is two compressions rather than four — is
the optimisation every library makes.

**Base64** is RFC 4648 section 4, written here; it accepts the unpadded form
Java's `Base64.getEncoder().withoutPadding()` writes and the padded form
everybody else does.

Kiss's `Hash.java`, `Hmac.java`, `Crypto.java` (reversible encryption) and
`Base64.java` were not ported: the first three reach `java.security` and
`javax.crypto`, and this system takes nothing with a key that must stay secret
from the other end. A reverse proxy does TLS.

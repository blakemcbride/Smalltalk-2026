"
The arithmetic under a stored password: SHA-256, HMAC-SHA256, PBKDF2 and
the bytes of entropy a salt is made of, each one message on the class side
and each one call of primitive 209.

    Crypto sha256: 'abc'                              a 32-byte String
    Crypto hmacSha256: key message: text              a 32-byte String
    Crypto pbkdf2: password salt: salt iterations: n  a 32-byte String
    Crypto randomBytes: 16                            a 16-byte String
    Crypto constantTimeEqual: a to: b                 true or false
    Crypto hex: bytes                                 '616263'

WHY A PRIMITIVE.  The password format this exists for (see PasswordHash)
asks for six hundred thousand HMAC rounds per login, which in C is a fifth
of a second and in this interpreter would be minutes.  So the three
algorithms are src/crypto/st_crypto.c -- written from FIPS 180-4, RFC 2104
and RFC 8018, checked in tests/unit/test_crypto.c against their published
answers -- and this class is the Smalltalk face of them.  Nothing here has
a key that must stay secret from the other end: no cipher, no TLS.  A
reverse proxy does that.

WHAT THE PRIMITIVE PROMISES.  Every argument is copied out of the object
memory before the arithmetic starts and the answer is made after it ends,
because the fifth of a second is spent inside a WORKER_enter_native region
so that a collection on another worker does not wait for a login.  The
region is the same one a database call parks in; doc/DATABASE.md, `It does
not stop the world', is the reason.

Bytes are Strings, as they are everywhere else on this system -- the
socket layer, the file layer, the database's bytesAt: -- and a String
whose bytes are a digest is not for printing.  hex: is for that.

An iteration count above maxIterations is refused.  A stored hash names
its own count, and a stored hash from somewhere untrusted naming ten
billion rounds would hold a worker for hours; ten million is seventeen
times the format's own figure and a few seconds.

Class-side and stateless, on purpose: every login on every worker uses
these at once, and a method with no state to share is one with nothing
to lock.
"
Class {
	#name : 'Crypto',
	#superclass : 'Object',
	#category : 'Crypto'
}

{ #category : 'private' }
Crypto class >> primCommand: commandInteger with: a with: b with: c [
	"The one primitive.  See the class comment.

	 Failing here means an argument was not what the command wanted --
	 an Integer where bytes were expected, a count out of range -- which
	 is a fault in the caller and not in the arithmetic, so it raises."

	<primitive: 209>
	^CryptoError new signal: 'the crypto primitive was called wrongly: command ',
		commandInteger printString
]

{ #category : 'hashing' }
Crypto class >> sha256: aByteString [
	"The SHA-256 digest of the bytes, as a 32-byte String."

	^self primCommand: 1 with: aByteString with: nil with: nil
]

{ #category : 'hashing' }
Crypto class >> hmacSha256: aKey message: aByteString [
	"The HMAC-SHA256 of the message under the key (RFC 2104), 32 bytes."

	^self primCommand: 2 with: aKey with: aByteString with: nil
]

{ #category : 'hashing' }
Crypto class >> pbkdf2: aPassword salt: aSalt iterations: anInteger [
	"A 32-byte key derived from the password and salt by PBKDF2-HMAC-SHA256
	 (RFC 8018) over anInteger rounds.  Raises for a count that is not an
	 Integer between 1 and maxIterations."

	| key |
	(anInteger isKindOf: Integer)
		ifFalse: [^CryptoError new signal: 'iterations must be an Integer'].
	(anInteger < 1 or: [anInteger > self maxIterations])
		ifTrue: [^CryptoError new signal: 'iterations out of range: ', anInteger printString].
	key := self primCommand: 3 with: aPassword with: aSalt with: anInteger.
	key isNil ifTrue: [^CryptoError new signal: 'could not derive a key'].
	^key
]

{ #category : 'hashing' }
Crypto class >> maxIterations [
	"The most rounds pbkdf2:salt:iterations: will run: see the class comment."

	^10000000
]

{ #category : 'entropy' }
Crypto class >> randomBytes: anInteger [
	"A String of anInteger bytes from the operating system's entropy source
	 -- getrandom, BCryptGenRandom -- the source Socket's session ids come
	 from.  Not for printing."

	| bytes |
	((anInteger isKindOf: Integer) and: [anInteger >= 0])
		ifFalse: [^CryptoError new signal: 'not a byte count: ', anInteger printString].
	bytes := String new: anInteger.
	(self primCommand: 4 with: bytes with: nil with: nil) isNil
		ifTrue: [^CryptoError new signal: 'the system has no random bytes to give'].
	^bytes
]

{ #category : 'comparing' }
Crypto class >> constantTimeEqual: aByteString to: anotherByteString [
	"Whether the two are the same bytes, in time that depends on their
	 length and not on where they first differ.  Strings of different
	 lengths are unequal at once: a length is not a secret."

	aByteString size = anotherByteString size ifFalse: [^false].
	^self primCommand: 5 with: aByteString with: anotherByteString with: nil
]

{ #category : 'text' }
Crypto class >> hex: aByteString [
	"The bytes as lowercase hexadecimal text, two characters each."

	| digits out |
	digits := '0123456789abcdef'.
	out := WriteStream on: (String new: aByteString size * 2).
	aByteString do: [:each | | v |
		v := each asInteger.
		out nextPut: (digits at: (v bitShift: -4) + 1); nextPut: (digits at: (v bitAnd: 15) + 1)].
	^out contents
]

{ #category : 'text' }
Crypto class >> bytesFromHex: aString [
	"The bytes hexadecimal text names, either case.  Raises on an odd length
	 or a character that is not a digit."

	| out |
	aString size odd ifTrue: [^CryptoError new signal: 'odd length hex'].
	out := WriteStream on: (String new: aString size // 2).
	1 to: aString size by: 2 do: [:i |
		out nextPut: (Character value:
			((self hexValueOf: (aString at: i)) bitShift: 4) + (self hexValueOf: (aString at: i + 1)))].
	^out contents
]

{ #category : 'private' }
Crypto class >> hexValueOf: aCharacter [
	| v |
	v := aCharacter asInteger.
	(v >= 48 and: [v <= 57]) ifTrue: [^v - 48].
	(v >= 97 and: [v <= 102]) ifTrue: [^v - 87].
	(v >= 65 and: [v <= 70]) ifTrue: [^v - 55].
	^CryptoError new signal: 'not a hex digit: ', aCharacter printString
]

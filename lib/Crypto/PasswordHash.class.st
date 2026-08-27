"
A password stored so that nobody, this system included, can read it back:
Kiss's format, byte for byte, so that a users table Kiss made logs in here
unchanged.

    PasswordHash hash: 'password'
        'pbkdf2$600000$XXxhRHyyeLvk3AfmOhTYhA$ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI'
    PasswordHash verify: 'password' against: stored      true
    PasswordHash isHashed: stored                        true; false for plain text
    PasswordHash needsRehash: stored                     true when its rounds are below today's

THE FORMAT is the one org.kissweb.PasswordHash writes:

    pbkdf2$<iterations>$<Base64 salt>$<Base64 key>

PBKDF2-HMAC-SHA256 over the password with a 16-byte salt drawn fresh for
every hash, a 256-bit key, 600,000 rounds -- the OWASP floor for this
function as Kiss records it -- and both Base64 fields without padding.
The count and the salt are in the value itself, so the work factor can be
raised later without touching what is stored: verify: reads the count it
finds, and needsRehash: says when a stored value was made with fewer
rounds than hash: uses today, for the usual pattern of re-hashing a
password at the moment somebody logs in with it.

hash:iterations: exists for two reasons: a site that wants a different
work factor, and the tests, for which six hundred thousand rounds a time
is a fifth of a second the suite need not spend more than once.

VERIFY NEVER RAISES.  A stored value in the wrong shape -- not four
fields, a count that is not a number, Base64 that is not, a key of the
wrong length, a count above Crypto maxIterations -- is a value nothing on
this system wrote, and the only right answer to a password checked against
it is false.  The comparison of the two keys is constant-time, in C,
because a comparison that stops at the first differing byte tells a
patient caller how much of a guess was right.

Class-side and stateless, like Crypto and Base64 under it.
"
Class {
	#name : 'PasswordHash',
	#superclass : 'Object',
	#category : 'Crypto'
}

{ #category : 'constants' }
PasswordHash class >> prefix [
	^'pbkdf2$'
]

{ #category : 'constants' }
PasswordHash class >> iterations [
	"The rounds hash: uses: 600,000, Kiss's figure."

	^600000
]

{ #category : 'constants' }
PasswordHash class >> saltBytes [
	^16
]

{ #category : 'constants' }
PasswordHash class >> keyBytes [
	^32
]

{ #category : 'hashing' }
PasswordHash class >> hash: aPassword [
	"The stored form of aPassword, with a fresh salt."

	^self hash: aPassword iterations: self iterations
]

{ #category : 'hashing' }
PasswordHash class >> hash: aPassword iterations: anInteger [
	"The stored form of aPassword over anInteger rounds, with a fresh salt."

	| salt key |
	salt := Crypto randomBytes: self saltBytes.
	key := Crypto pbkdf2: aPassword salt: salt iterations: anInteger.
	^self prefix, anInteger printString, '$',
		(Base64 encodeWithoutPadding: salt), '$',
		(Base64 encodeWithoutPadding: key)
]

{ #category : 'verifying' }
PasswordHash class >> verify: aPassword against: aStoredString [
	"Whether aPassword is the one aStoredString was made from.  False, and
	 never an error, for a stored value in any shape but the one hash:
	 writes."

	| parts salt expected actual |
	(aPassword isNil or: [aStoredString isNil]) ifTrue: [^false].
	parts := self partsOf: aStoredString.
	parts isNil ifTrue: [^false].
	(parts at: 1) > Crypto maxIterations ifTrue: [^false].
	salt := Base64 decode: (parts at: 2) ifInvalid: [^false].
	expected := Base64 decode: (parts at: 3) ifInvalid: [^false].
	expected size = self keyBytes ifFalse: [^false].
	actual := Crypto pbkdf2: aPassword salt: salt iterations: (parts at: 1).
	^Crypto constantTimeEqual: actual to: expected
]

{ #category : 'testing' }
PasswordHash class >> isHashed: aStoredString [
	"Whether the stored value is in this format at all, as opposed to a
	 plain-text password from before hashing was introduced."

	^aStoredString notNil and: [aStoredString startsWith: self prefix]
]

{ #category : 'testing' }
PasswordHash class >> needsRehash: aStoredString [
	"Whether the stored value was made with fewer rounds than hash: uses
	 now -- or is not in this format at all -- so that a caller who has
	 just verified a password can store it again at today's strength."

	| parts |
	parts := self partsOf: aStoredString.
	parts isNil ifTrue: [^true].
	^(parts at: 1) < self iterations
]

{ #category : 'private' }
PasswordHash class >> partsOf: aStoredString [
	"An Array of the iteration count, the salt text and the key text, or
	 nil when the value is not four $-separated fields beginning with the
	 prefix and a count of digits."

	| parts count |
	(self isHashed: aStoredString) ifFalse: [^nil].
	parts := aStoredString substrings: '$'.
	parts size = 4 ifFalse: [^nil].
	count := parts at: 2.
	(count isEmpty or: [(count allSatisfy: [:c | c isDigit]) not]) ifTrue: [^nil].
	count := count asNumber.
	count < 1 ifTrue: [^nil].
	^Array with: count with: (parts at: 3) with: (parts at: 4)
]

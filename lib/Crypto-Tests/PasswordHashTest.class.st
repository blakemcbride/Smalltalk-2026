"
The stored format against the value Kiss stored, and everything verify:
must answer false to without raising.

testKissHashVerifies is the test the package exists for: the hash in
Kiss's schema.sqlite for user `kiss', made by Java's
PBKDF2WithHmacSHA256, accepted here.  It is the one place the suite runs
six hundred thousand rounds -- a fifth of a second -- and the other tests
use a thousand, through hash:iterations:, because a round trip proves the
same thing at any count.
"
Class {
	#name : 'PasswordHashTest',
	#superclass : 'TestCase',
	#category : 'Crypto-Tests'
}

{ #category : 'fixture' }
PasswordHashTest >> kissHash [
	"schema.sqlite, in the Kiss tree: the password is `password'."
	^'pbkdf2$600000$XXxhRHyyeLvk3AfmOhTYhA$ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI'
]

{ #category : 'tests - kiss' }
PasswordHashTest >> testKissHashVerifies [
	self assert: (PasswordHash verify: 'password' against: self kissHash)
]

{ #category : 'tests - kiss' }
PasswordHashTest >> testKissHashIsCurrent [
	self assert: (PasswordHash isHashed: self kissHash).
	self deny: (PasswordHash needsRehash: self kissHash)
]

{ #category : 'tests - round trip' }
PasswordHashTest >> testRoundTrip [
	| stored |
	stored := PasswordHash hash: 'correct horse battery staple' iterations: 1000.
	self assert: (stored startsWith: 'pbkdf2$1000$').
	self assert: (stored substrings: '$') size equals: 4.
	self assert: (PasswordHash verify: 'correct horse battery staple' against: stored).
	self deny: (PasswordHash verify: 'correct horse battery stapl' against: stored).
	self deny: (PasswordHash verify: 'Correct horse battery staple' against: stored).
	self deny: (PasswordHash verify: '' against: stored)
]

{ #category : 'tests - round trip' }
PasswordHashTest >> testSaltIsFresh [
	"The same password twice is two different stored values, and both verify."
	| a b |
	a := PasswordHash hash: 'same' iterations: 1000.
	b := PasswordHash hash: 'same' iterations: 1000.
	self deny: a = b.
	self assert: (PasswordHash verify: 'same' against: a).
	self assert: (PasswordHash verify: 'same' against: b)
]

{ #category : 'tests - round trip' }
PasswordHashTest >> testDefaultRounds [
	"hash: names its own count in what it writes."
	self assert: ((PasswordHash hash: 'x' iterations: 3) startsWith: 'pbkdf2$3$').
	self assert: PasswordHash iterations equals: 600000
]

{ #category : 'tests - round trip' }
PasswordHashTest >> testEmptyAndBinaryPasswords [
	| stored |
	stored := PasswordHash hash: '' iterations: 100.
	self assert: (PasswordHash verify: '' against: stored).
	self deny: (PasswordHash verify: 'a' against: stored).
	stored := PasswordHash hash: (String with: (Character value: 0) with: (Character value: 255)) iterations: 100.
	self assert: (PasswordHash verify: (String with: (Character value: 0) with: (Character value: 255)) against: stored)
]

{ #category : 'tests - shape' }
PasswordHashTest >> testIsHashed [
	self assert: (PasswordHash isHashed: 'pbkdf2$1$a$b').
	self deny: (PasswordHash isHashed: 'password').
	self deny: (PasswordHash isHashed: '').
	self deny: (PasswordHash isHashed: nil).
	self deny: (PasswordHash isHashed: 'PBKDF2$1$a$b')
]

{ #category : 'tests - shape' }
PasswordHashTest >> testNeedsRehash [
	self assert: (PasswordHash needsRehash: 'password').
	self assert: (PasswordHash needsRehash: nil).
	self assert: (PasswordHash needsRehash: (PasswordHash hash: 'x' iterations: 1000)).
	self assert: (PasswordHash needsRehash: 'pbkdf2$599999$YQ$YQ').
	self deny: (PasswordHash needsRehash: 'pbkdf2$600000$YQ$YQ').
	self deny: (PasswordHash needsRehash: 'pbkdf2$700000$YQ$YQ').
	self assert: (PasswordHash needsRehash: 'pbkdf2$abc$YQ$YQ').
	self assert: (PasswordHash needsRehash: 'pbkdf2$600000$YQ')
]

{ #category : 'tests - refusals' }
PasswordHashTest >> testMalformedStoredValuesAreFalse [
	"Never an error: a stored value nothing here wrote is a wrong password."
	self deny: (PasswordHash verify: 'password' against: 'password').
	self deny: (PasswordHash verify: 'password' against: '').
	self deny: (PasswordHash verify: 'password' against: nil).
	self deny: (PasswordHash verify: nil against: self kissHash).
	self deny: (PasswordHash verify: 'password' against: 'pbkdf2$600000$XXxhRHyyeLvk3AfmOhTYhA').
	self deny: (PasswordHash verify: 'password' against: 'pbkdf2$abc$XXxhRHyyeLvk3AfmOhTYhA$ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI').
	self deny: (PasswordHash verify: 'password' against: 'pbkdf2$0$XXxhRHyyeLvk3AfmOhTYhA$ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI').
	self deny: (PasswordHash verify: 'password' against: 'pbkdf2$600000$@@@@$ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI').
	self deny: (PasswordHash verify: 'password' against: 'pbkdf2$600000$XXxhRHyyeLvk3AfmOhTYhA$@@@@').
	self deny: (PasswordHash verify: 'password' against: 'pbkdf2$1$YQ$YQ')
]

{ #category : 'tests - refusals' }
PasswordHashTest >> testAbsurdRoundCountIsFalseAtOnce [
	"A stored value naming more rounds than Crypto maxIterations is refused
	 before a single round runs: it took less than a second to answer."
	| started |
	started := Time millisecondClockValue.
	self deny: (PasswordHash verify: 'password'
		against: 'pbkdf2$99999999999$XXxhRHyyeLvk3AfmOhTYhA$ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI').
	self assert: Time millisecondClockValue - started < 1000
]

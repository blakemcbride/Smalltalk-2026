"
Primitive 209 from the Smalltalk side: the same published answers
tests/unit/test_crypto.c checks the C against, reached through Crypto's
messages, so that what is tested here is the marshalling -- bytes in a
String out, an Integer count through, a wrong argument refused -- rather
than the arithmetic again.

The six-hundred-thousand-round check that proves a Kiss database logs in
unchanged is in PasswordHashTest, once.  Everything here runs in
milliseconds.
"
Class {
	#name : 'CryptoTest',
	#superclass : 'TestCase',
	#category : 'Crypto-Tests'
}

{ #category : 'tests - sha256' }
CryptoTest >> testSha256OfAbc [
	self assert: (Crypto hex: (Crypto sha256: 'abc'))
		equals: 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
]

{ #category : 'tests - sha256' }
CryptoTest >> testSha256OfNothing [
	self assert: (Crypto hex: (Crypto sha256: ''))
		equals: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
]

{ #category : 'tests - sha256' }
CryptoTest >> testSha256IsThirtyTwoBytes [
	self assert: (Crypto sha256: 'anything at all') size equals: 32.
	self assert: (Crypto sha256: (String new: 1000 withAll: $a)) size equals: 32
]

{ #category : 'tests - sha256' }
CryptoTest >> testSha256OfBytesAboveAscii [
	"A String is bytes: every value 0 to 255 goes through unchanged."
	| all |
	all := String new: 256.
	0 to: 255 do: [:i | all at: i + 1 put: (Character value: i)].
	self assert: (Crypto hex: (Crypto sha256: all))
		equals: '40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880'
]

{ #category : 'tests - hmac' }
CryptoTest >> testHmacRfc4231Case2 [
	self assert: (Crypto hex: (Crypto hmacSha256: 'Jefe' message: 'what do ya want for nothing?'))
		equals: '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843'
]

{ #category : 'tests - hmac' }
CryptoTest >> testHmacWithLongKey [
	"A key longer than a block is hashed first: RFC 4231 case 6."
	self assert: (Crypto hex: (Crypto hmacSha256: (String new: 131 withAll: (Character value: 170))
			message: 'Test Using Larger Than Block-Size Key - Hash Key First'))
		equals: '60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54'
]

{ #category : 'tests - pbkdf2' }
CryptoTest >> testPbkdf2OneRound [
	self assert: (Crypto hex: (Crypto pbkdf2: 'password' salt: 'salt' iterations: 1))
		equals: '120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b'
]

{ #category : 'tests - pbkdf2' }
CryptoTest >> testPbkdf2FourThousandRounds [
	self assert: (Crypto hex: (Crypto pbkdf2: 'password' salt: 'salt' iterations: 4096))
		equals: 'c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a'
]

{ #category : 'tests - pbkdf2' }
CryptoTest >> testPbkdf2RefusesBadCounts [
	self should: [Crypto pbkdf2: 'p' salt: 's' iterations: 0] raise: CryptoError.
	self should: [Crypto pbkdf2: 'p' salt: 's' iterations: -1] raise: CryptoError.
	self should: [Crypto pbkdf2: 'p' salt: 's' iterations: Crypto maxIterations + 1] raise: CryptoError.
	self should: [Crypto pbkdf2: 'p' salt: 's' iterations: 'many'] raise: CryptoError
]

{ #category : 'tests - arguments' }
CryptoTest >> testWrongArgumentsRaise [
	"The primitive fails for an argument of the wrong kind and the fallback
	 raises: a fault in the caller, not a nil to be found later."
	self should: [Crypto sha256: 42] raise: CryptoError.
	self should: [Crypto sha256: nil] raise: CryptoError.
	self should: [Crypto hmacSha256: 'k' message: #(1 2 3)] raise: CryptoError.
	self should: [Crypto randomBytes: 'sixteen'] raise: CryptoError
]

{ #category : 'tests - entropy' }
CryptoTest >> testRandomBytes [
	| a b |
	a := Crypto randomBytes: 16.
	b := Crypto randomBytes: 16.
	self assert: a size equals: 16.
	self assert: b size equals: 16.
	self deny: a = b.
	self assert: (Crypto randomBytes: 0) equals: ''.
	self assert: (Crypto randomBytes: 1000) size equals: 1000
]

{ #category : 'tests - comparing' }
CryptoTest >> testConstantTimeEqual [
	self assert: (Crypto constantTimeEqual: 'abc' to: 'abc').
	self deny: (Crypto constantTimeEqual: 'abc' to: 'abd').
	self deny: (Crypto constantTimeEqual: 'abc' to: 'bbc').
	self deny: (Crypto constantTimeEqual: 'abc' to: 'abcd').
	self assert: (Crypto constantTimeEqual: '' to: '')
]

{ #category : 'tests - text' }
CryptoTest >> testHexRoundTrip [
	| all |
	all := String new: 256.
	0 to: 255 do: [:i | all at: i + 1 put: (Character value: i)].
	self assert: (Crypto hex: 'abc') equals: '616263'.
	self assert: (Crypto bytesFromHex: '616263') equals: 'abc'.
	self assert: (Crypto bytesFromHex: '616263' asUppercase) equals: 'abc'.
	self assert: (Crypto bytesFromHex: (Crypto hex: all)) equals: all.
	self should: [Crypto bytesFromHex: 'abc'] raise: CryptoError.
	self should: [Crypto bytesFromHex: 'zz'] raise: CryptoError
]

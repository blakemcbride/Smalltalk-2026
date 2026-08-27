"
RFC 4648's own examples, both directions, both paddings; every byte value
round-tripped; and what is refused.  The last group is the one with
anything to catch: a decoder that took `Zg' as `f' and `Z' as something
would read a truncated stored hash as a shorter key and compare against it.
"
Class {
	#name : 'Base64Test',
	#superclass : 'TestCase',
	#category : 'Crypto-Tests'
}

{ #category : 'tests - encoding' }
Base64Test >> testRfc4648Examples [
	self assert: (Base64 encode: '') equals: ''.
	self assert: (Base64 encode: 'f') equals: 'Zg=='.
	self assert: (Base64 encode: 'fo') equals: 'Zm8='.
	self assert: (Base64 encode: 'foo') equals: 'Zm9v'.
	self assert: (Base64 encode: 'foob') equals: 'Zm9vYg=='.
	self assert: (Base64 encode: 'fooba') equals: 'Zm9vYmE='.
	self assert: (Base64 encode: 'foobar') equals: 'Zm9vYmFy'
]

{ #category : 'tests - encoding' }
Base64Test >> testWithoutPadding [
	self assert: (Base64 encodeWithoutPadding: '') equals: ''.
	self assert: (Base64 encodeWithoutPadding: 'f') equals: 'Zg'.
	self assert: (Base64 encodeWithoutPadding: 'fo') equals: 'Zm8'.
	self assert: (Base64 encodeWithoutPadding: 'foo') equals: 'Zm9v'.
	self assert: (Base64 encodeWithoutPadding: 'foob') equals: 'Zm9vYg'
]

{ #category : 'tests - decoding' }
Base64Test >> testDecodePaddedAndNot [
	self assert: (Base64 decode: '') equals: ''.
	self assert: (Base64 decode: 'Zg==') equals: 'f'.
	self assert: (Base64 decode: 'Zg') equals: 'f'.
	self assert: (Base64 decode: 'Zm8=') equals: 'fo'.
	self assert: (Base64 decode: 'Zm8') equals: 'fo'.
	self assert: (Base64 decode: 'Zm9v') equals: 'foo'.
	self assert: (Base64 decode: 'Zm9vYmFy') equals: 'foobar'.
	self assert: (Base64 decode: 'Zm9vYmE=') equals: 'fooba'.
	self assert: (Base64 decode: 'Zm9vYmE') equals: 'fooba'
]

{ #category : 'tests - decoding' }
Base64Test >> testEveryByteRoundTrips [
	| all |
	all := String new: 256.
	0 to: 255 do: [:i | all at: i + 1 put: (Character value: i)].
	self assert: (Base64 decode: (Base64 encode: all)) equals: all.
	self assert: (Base64 decode: (Base64 encodeWithoutPadding: all)) equals: all.
	self assert: (Base64 decode: (Base64 encode: (all copyFrom: 1 to: 255))) equals: (all copyFrom: 1 to: 255).
	self assert: (Base64 decode: (Base64 encode: (all copyFrom: 1 to: 254))) equals: (all copyFrom: 1 to: 254)
]

{ #category : 'tests - decoding' }
Base64Test >> testWhatKissWrote [
	"The salt field of the hash Kiss's demo database stores: sixteen bytes,
	 written by Java without padding."
	self assert: (Crypto hex: (Base64 decode: 'XXxhRHyyeLvk3AfmOhTYhA'))
		equals: '5d7c61447cb278bbe4dc07e63a14d884'.
	self assert: (Base64 decode: 'ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI') size equals: 32
]

{ #category : 'tests - refusals' }
Base64Test >> testRefusals [
	"A length no encoding produces, a character outside the alphabet, the
	 URL-safe alphabet, = in the middle."
	self should: [Base64 decode: 'Z'] raise: CryptoError.
	self should: [Base64 decode: 'Zm9vY'] raise: CryptoError.
	self should: [Base64 decode: 'Zm9v?'] raise: CryptoError.
	self should: [Base64 decode: 'Zm9v-_'] raise: CryptoError.
	self should: [Base64 decode: 'Zg==Zg=='] raise: CryptoError.
	self should: [Base64 decode: 'Zm 9v'] raise: CryptoError.
	self assert: (Base64 decode: 'Z' ifInvalid: ['no']) equals: 'no'.
	self assert: (Base64 decode: 'Zm9v' ifInvalid: ['no']) equals: 'foo'
]

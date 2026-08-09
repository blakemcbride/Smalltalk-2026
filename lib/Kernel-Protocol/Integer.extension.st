Extension { #name : 'Integer' }

{ #category : '*Kernel-Protocol' }
Integer >> isInteger [
	^true
]

{ #category : '*Kernel-Protocol' }
Integer >> hashMultiply [
	"The scrambling step the Smalltalk hashes are built on, in Squeak's
	 exact arithmetic -- 28 bits, times 1664525 -- because ported code
	 compares hashes against values computed elsewhere.

	 The fallback is the same arithmetic written out, so a receiver the
	 primitive declines still answers rather than failing."
	<primitive: 159>
	^(self * 1664525) bitAnd: 16r0FFFFFFF
]

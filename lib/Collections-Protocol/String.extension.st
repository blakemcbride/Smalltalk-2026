"
String>>hash, over every character.

1983's read three: the first character, the second-to-last, and the length.
It was written for a machine where a hash had to stay a 16-bit SmallInteger,
and on the strings a program actually keys a Dictionary by -- names that
share a prefix and differ at the end -- it is not a hash at all.  'key1'
through 'key200' produce eleven distinct values.  A Dictionary of 992 such
keys took 1.7 seconds to fill on this machine and four times as long for
each doubling, because every key probed along the same chain.  It was found
by lib/Json's parallel test, which was meant to exercise a lock and
measured this instead (doc/JSON.md).

This one is primitive 223, and the body below is the same function in
Smalltalk, run only if the primitive fails.  The bootstrap computes it a
third time, in C, to place every symbol in the library's table without
interpreting a send per symbol; tests/unit/test_image.c holds the three
equal, for strings that reach every branch and for Symbols, whose
stringhash is `super hash'.

The function is FNV-1a: start from a constant, and for each byte xor it in
and multiply by a prime, all in 32 bits.  Then the high sixteen bits are
folded into the low sixteen and the top four dropped, for 28 bits.  Two
things about that are deliberate.

The arithmetic never leaves SmallInteger, so the fallback and the primitive
cannot disagree by a LargePositiveInteger's rounding: a 32-bit value times a
24-bit prime is 56 bits, and a SmallInteger here holds 62.

The fold is for where the answer goes.  Set and Dictionary choose a bucket
by `hash \\ basicSize', and basicSize is 32 doubling -- so only the LOW bits
of a hash ever choose a bucket, and a multiply moves information upward and
never down.  Without the fold, 'a' and 'A', which differ in bit 5 alone,
would share a bucket in every 32-slot Set, and so would any two strings
whose last characters differ only in case.

Symbol>>hash is `^super hash' and so answers this too, which is what keeps
`#foo hash = 'foo' hash' true; Symbol>>= says they are equal, and a hashed
collection needs both to say the same thing.
"
Extension { #name : 'String' }

{ #category : '*Collections-Protocol' }
String >> hash [
	"Over every character; see the class comment above.  The primitive and
	 this body are one function, and both are checked against the C the
	 bootstrap uses to file symbols."
	<primitive: 223>
	| h |
	h := 2166136261.
	1 to: self size do: [:i |
		h := ((h bitXor: (self at: i) asciiValue) * 16777619) bitAnd: 16rFFFFFFFF].
	^(h bitXor: (h bitShift: -16)) bitAnd: 16rFFFFFFF
]

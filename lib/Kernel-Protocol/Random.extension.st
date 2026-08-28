"
A random number generator with 48 bits of state, seeded so that no two are
alike, and a seed: for a run that has to repeat.

1983's Random is the sixteen-bit linear congruential generator

	seed _ 13849 + (27181 * seed) bitAnd: 65535

seeded from the low sixteen bits of the millisecond clock.  Sixteen bits of
state is 65,536 possible streams, and `next' is `seed / 65536.0', so every
number it answers is a multiple of 1/65536 and there are at most 65,535
distinct ones before the sequence repeats.  Seeding from the clock's low
bits means two generators made in the same millisecond are the same stream,
and on a machine that makes a thousand of them in a loop most of them are:
a thousand `Random new next' gave twenty distinct values (Bugs3 B50).  And
there was no way to ask for a particular stream, so a run that found a
fault could not be repeated.

This is the 48-bit generator java.util.Random has used since 1995 --

	seed <- seed * 25214903917 + 11 mod 2^48

-- with its output taken from the high bits, where a linear congruential
generator's quality is, and its Float built from two draws so that all 53
bits of the mantissa are random.  It is chosen because it is simple, its
period is 2^48, its weaknesses are documented, and its outputs are
published: a Random seeded with 42 answers 0.7275636800328681 here and in
Java, which is a test that can be written down.

The multiply is done in halves.  A SmallInteger here holds 61 bits and the
48-bit state times the 35-bit multiplier is 83, so the state is split at 24
bits; the three partial products that survive the modulus are each under
2^49 and the whole step stays in SmallInteger arithmetic -- exact, and with
no LargeInteger allocated per number.

The seed is the microsecond clock folded with the new generator's own
identityHash.  The clock alone would make two generators made in the same
microsecond the same stream, which eight workers can manage; the hash is
different for every live object, costs no lock and touches no shared state,
so `Random new' may be sent from any number of workers at once.  A generator
SHARED between workers is not made safe by this -- next writes the seed --
and is the caller's lock to take, as it was in 1983.
"
Extension { #name : 'Random' }

{ #category : '*Kernel-Protocol' }
Random class >> seed: anInteger [
	"A generator that answers the same numbers every time it is made with
	 that seed."
	^self basicNew seed: anInteger
]

{ #category : '*Kernel-Protocol' }
Random >> seed: anInteger [
	"Start my sequence from that seed.

	 Scrambled with the multiplier first, as Java does, so that the small
	 seeds people actually type do not all start in the corner of the state
	 space where the first outputs look alike; masked to 48 bits because
	 the step below assumes the state fits them."
	seed := (anInteger bitXor: 25214903917) bitAnd: 281474976710655
]

{ #category : '*Kernel-Protocol' }
Random >> setSeed [
	"Seed from the clock and from my own identity: the comment at the top
	 of this file says why both, and why no lock."
	self seed: (self microsecondClock bitXor: self identityHash * 2654435761)
]

{ #category : '*Kernel-Protocol' }
Random >> microsecondClock [
	"The microsecond clock, or the millisecond one scaled up on a memory
	 whose SmallIntegers cannot hold microseconds since 1901."
	<primitive: 240>
	^Time millisecondClockValue * 1000
]

{ #category : '*Kernel-Protocol' }
Random >> nextBits: bitCount [
	"Advance once and answer the top bitCount bits of the new state, for a
	 bitCount up to 48.

	 The TOP bits.  In a linear congruential generator modulo a power of
	 two the lowest bit alternates and the next few are barely better; the
	 high ones are where the randomness is.  This is the step Java calls
	 next(int), written with the multiply split at 24 bits so that it
	 never leaves SmallInteger arithmetic: 15525485 and 1502 are the low
	 and high halves of 25214903917, and the high-times-high product is
	 dropped because it is a multiple of 2^48."
	| low high |
	low := seed bitAnd: 16777215.
	high := seed bitShift: -24.
	seed := (low * 15525485
		+ ((high * 15525485 + (low * 1502) bitAnd: 16777215) bitShift: 24)
		+ 11) bitAnd: 281474976710655.
	^seed bitShift: bitCount - 48
]

{ #category : '*Kernel-Protocol' }
Random >> next [
	"A Float in [0, 1) with all 53 bits of its mantissa random: 26 bits
	 from one step and 27 from the next, as Java's nextDouble does, because
	 one step has only 48 and its low bits are the weak ones.  Zero is a
	 possible answer, one in 2^53; 1983 looped to avoid it and nothing
	 needs that."
	^(((self nextBits: 26) bitShift: 27) + (self nextBits: 27)) / 9007199254740992.0
]

{ #category : '*Kernel-Protocol' }
Random >> between: low and: high [
	"An Integer from low to high inclusive, each as likely as 53 bits can
	 make it."
	^low + (self next * (high - low + 1)) truncated
]

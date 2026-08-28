"
LiteralDictionary>>findKeyOrNil:, in step with the Set probe it grows by.

A LiteralDictionary is the compiler's literal frame in the making: a
Dictionary whose keys must match by class as well as by value, so that 0
and 0.0 get two slots.  1983 gives it a findKeyOrNil: of its own, the
Dictionary loop with that class check added, and leaves grow and rehash
inherited -- which place each association through Set>>noCheckAdd: and so
through Set>>findElementOrNil:.  That was consistent in 1983 because both
loops began at `hash \\ length + 1'.

Bugs3 B33 changed where Set>>findElementOrNil: and Dictionary>>findKeyOrNil:
begin -- the hash is mixed first, so that round numbers spread over a
power-of-two table -- and a LiteralDictionary that kept 1983's loop would
then look for a key at one slot after growing had put it at another.  The
miss is not an error, only a second slot for a literal already in the
frame, which is exactly the kind of fault that never fails a test.  So this
is 1983's method with the same mixing written in, and the class check kept.
"
Extension { #name : 'LiteralDictionary' }

{ #category : '*Compiler-Fixes' }
LiteralDictionary >> findKeyOrNil: key [
	"The slot key's association is in, or the empty slot it would go in:
	 Dictionary>>findKeyOrNil: with 1983's class check.  The mixing must
	 stay the arithmetic Set>>findElementOrNil: uses; see the comment at
	 the top of this file."
	| location length probe pass hash |
	length := self basicSize.
	length = 0 ifTrue: [^super findKeyOrNil: key].
	pass := 1.
	hash := key hash.
	location := (((hash + (hash bitShift: -30) bitAnd: 16r3FFFFFFF) * 16r9E3779B1)
				bitShift: -16) \\ length + 1.
	[(probe := self basicAt: location) == nil
		or: [probe key class == key class and: [probe key = key]]]
		whileFalse: [
			(location := location + 1) > length
				ifTrue: [
					location := 1.
					pass := pass + 1.
					pass > 2 ifTrue: [^self grow findKeyOrNil: key]]].
	^location
]

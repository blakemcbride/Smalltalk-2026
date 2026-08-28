"
MethodDescription>>hash, to go with the = it already had.

MethodDescription>>= says two descriptions are equal when they name the
same class and the same selector (or are both conflicting, or both not
implemented), and 1983 left hash as Object's identity hash, so two equal
descriptions hashed differently and a Set given the same description
twice held two of them (Bugs3 B35).  The census of every class with an =
and no hash found eight; the other six are consistent on reading, this
one is not.

The class's hash and the selector's, combined with xor.  A description
whose status is conflicting or not implemented has nil in both fields
and so hashes to nil's hash xor itself, which is the same value for every
such description -- as it must be, since = says they are all equal.
"
Extension { #name : 'MethodDescription' }

{ #category : '*Kernel-Methods-Fixes' }
MethodDescription >> hash [
	"whichClass and selector are what = compares."
	^whichClass hash bitXor: selector hash
]

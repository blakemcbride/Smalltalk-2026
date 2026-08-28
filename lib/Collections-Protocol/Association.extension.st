"
Two Associations are equal when both halves are.

1983's Association inherits LookupKey>>=, which compares the keys and
nothing else, because a LookupKey is the thing a Dictionary looks up BY and
the value is what it finds.  Inside a Dictionary that is the right question.
Outside one it is wrong: `(1->2) = (1->3)' answered true, and so
`(Set with: 1->2) includes: 1->3' did, and a Set of Associations -- which is
what code that collects pairs builds -- silently lost every pair whose key
it had already seen (Bugs3 B51).  Pharo's Association compares both halves,
and the pharo-collections profile's 884 tests are written against that.

The hash is NOT changed, and must not be, by anybody.  It stays LookupKey's
`key hash', and that is consistent with the new = -- two equal Associations
have equal keys -- but it is also load-bearing in a way a stronger hash
would break.  A 1983 Dictionary is a Set of Associations: findKeyOrNil:
probes by the KEY's hash, while Set>>fixCollisionsFrom: -- run by
removeKey: -- re-probes each displaced entry by the ELEMENT's hash, that is
the Association's.  The two only find the same slot while the Association
hashes as its key does.  Mix the value in and every removeKey: can strand
a neighbour where at: cannot find it.
"
Extension { #name : 'Association' }

{ #category : '*Collections-Protocol' }
Association >> = anAssociation [
	"Same kind, same key, same value.

	 Identity is answered first, and not only as a shortcut: a value that
	 is not equal to itself -- a NaN -- would otherwise make an Association
	 unequal to itself, and Set>>fixCollisionsFrom: asks exactly that
	 question of every entry it moves."
	self == anAssociation ifTrue: [^true].
	^(super = anAssociation) and: [value = anAssociation value]
]

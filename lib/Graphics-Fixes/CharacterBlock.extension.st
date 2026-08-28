"
CharacterBlock>>hash, from the one field = compares.

CharacterBlock is a Rectangle that also knows which character of a string
it stands for, and its = compares the stringIndex ALONE -- two blocks for
the same character are equal wherever they are drawn.  Its hash was
Rectangle's, from the origin and corner, so two equal blocks at different
places hashed differently (Bugs3 B35), which is the one pair a hashed
collection cannot hold.  stringIndex is what = reads, so stringIndex is
what hash reads.
"
Extension { #name : 'CharacterBlock' }

{ #category : '*Graphics-Fixes' }
CharacterBlock >> hash [
	"stringIndex is what = compares."
	^stringIndex hash
]

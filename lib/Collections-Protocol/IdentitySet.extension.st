"
An IdentitySet that can hold nil, which needs one method that Set's does not.

Set's probe loop compares with = and so recognises a CollectionElement
standing in for the nil it encloses -- see Set.extension.st.  IdentitySet's
compares with ==, on purpose, and a wrapper is never identical to what it
wraps.  Left alone it would have accumulated a fresh wrapper on every
`add: nil', reporting a growing size for a Set of one element.

So the identity test is tried first, exactly as before, and only a probe
that fails it is asked whether it is a stand-in.  The cost falls entirely on
collisions, where a second comparison was already happening.
"
Extension { #name : 'IdentitySet' }

{ #category : '*Collections-Protocol' }
IdentitySet >> findElementOrNil: anObject [
	"1983's, with the identity check extended to see through a wrapper."
	| index length probe pass |
	length := self basicSize.
	"
	 An IdentitySet with no indexed part is not one of 1983's.
	 profiles/pharo-collections supersedes Set and IdentitySet with Pharo's,
	 which keep their elements in an `array' instance variable, and the walk
	 below would divide by a basicSize of zero -- seven of that profile's
	 867 tests said so, in the ZeroDivide the Bugs2 work had just given
	 Float and LargeInteger.  Handing such a receiver back to its own
	 superclass is not a special case grudgingly admitted: this method
	 exists to teach 1983's probe loop about the nil wrapper, and a Set that
	 is not 1983's does not need teaching -- Pharo's already unwraps at
	 every site that compares.
	"
	length = 0 ifTrue: [^super findElementOrNil: anObject].
	pass := 1.
	index := anObject hash \\ length + 1.
	[(probe := self basicAt: index) == nil
		or: [probe == anObject
			or: [probe class == CollectionElement
				and: [probe enclosedElement == anObject]]]]
		whileFalse: [
			(index := index + 1) > length
				ifTrue: [
					index := 1.
					pass := pass + 1.
					pass > 2 ifTrue: [^self grow findElementOrNil: anObject]]].
	^index
]

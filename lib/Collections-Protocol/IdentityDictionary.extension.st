"
An IdentityDictionary that copies its values and refuses nil as a key.

1983's IdentityDictionary is the one Dictionary that is not a Set of
Associations: its keys live in the indexed slots, as a Set's elements do,
and its values in a parallel `valueArray'.  Two things follow from that
shape, and both of them went wrong.

A shallow copy shares the valueArray.  1983 knew, and gave the class its
own `copy' that copies it; but Object>>copy in this system is `shallowCopy
postCopy', and Dictionary>>postCopy -- which walks the slots replacing
Associations -- is inherited here, where the slots hold KEYS.  So
IdentityDictionary answers postCopy for itself: copy the valueArray, and
nothing else.

And a nil key looks exactly like an empty slot.  `at: nil put:' found the
first empty slot, counted a new entry, and stored nil into a slot that
already held nil -- so `size' said 1, `keys' said none, `at: nil ifAbsent:'
said absent, and a second `at: nil put:' found a DIFFERENT empty slot and
counted again, until fullCheck grew the table and rehashed the ghosts away
to a size of 0 (Bugs3 B44).  Bugs2's nil work gave Set and Dictionary a
wrapper for exactly this case; it did not reach here because this class
overrides at:put: and findKeyOrNil: and never saw the wrapper.

Both methods begin by asking `self basicSize = 0', and hand a receiver with
no indexed part to super.  That is the same test IdentitySet>>findElementOrNil:
makes, for the same reason: profiles/pharo-collections supersedes this class
with Pharo's, whose IdentityDictionary is a Dictionary of Associations kept in
an `array' variable and defines neither of these selectors -- so an extension
on IdentityDictionary is the most specific method there is for it, and it
would run 1983's body against Pharo's shape.  Pharo's Dictionary already
answers both questions correctly for Pharo's shape; super is it.

And both reach the valueArray as `self instVarAt: 2' rather than by name.
The name would be right, and it would be compiled once against the Pharo
class too, where no such variable exists; the loader then reports a
lower-case name nothing defines on every pharo-collections build, as a
block argument escaping its block, which it is not.  The second named
variable of a 1983 IdentityDictionary is valueArray -- tally is Set's and
comes first -- and the class definition it comes from is in sources/, which
does not change.
"
Extension { #name : 'IdentityDictionary' }

{ #category : '*Collections-Protocol' }
IdentityDictionary >> postCopy [
	"Copy the valueArray, and only that.

	 Not `super postCopy' for a 1983 receiver: Dictionary's walks the
	 indexed slots replacing each Association with a copy of itself, and
	 my slots hold keys, which have no `copy' worth making and must stay
	 identical -- identity is what this class looks them up by.  The slot
	 Array itself was already copied by shallowCopy; the values were not.
	 1983's own copy method, which is still here and still used by `copy',
	 does the same thing in its own way; this is for the callers that
	 reach postCopy directly, which is how Pharo code copies (Bugs3 B42)."
	self basicSize = 0 ifTrue: [^super postCopy].
	self instVarAt: 2 put: (self instVarAt: 2) copy
]

{ #category : '*Collections-Protocol' }
IdentityDictionary >> at: key put: value [
	"1983's, refusing nil as a key.

	 Refused rather than wrapped, and the choice was made on purpose.
	 Set and Dictionary wrap nil in a CollectionElement (Bugs2) because
	 their probe loop compares with =, which a wrapper can answer.  This
	 class probes with ==, and a wrapper is never identical to the nil it
	 wraps -- IdentitySet paid for that with a second comparison on every
	 collision, and here the same price would have to be paid in
	 findKeyOrNil:, associationsDo:, keyAtValue:ifAbsent:,
	 removeDangerouslyKey:ifAbsent: and noCheckAdd:with:, the whole of the
	 class's private machinery, for one key.  And it is a key nobody
	 needs here: nil is one object, so a Dictionary looks it up by
	 identity already.  A clear refusal, and `use a Dictionary', is the
	 whole of what the caller has to know (Bugs3 B44)."
	| index |
	self basicSize = 0 ifTrue: [^super at: key put: value].
	key == nil ifTrue: [
		^self error: 'an IdentityDictionary cannot hold nil as a key; a Dictionary can'].
	index := self findKeyOrNil: key.
	(self basicAt: index) == nil
		ifTrue:
			[tally := tally + 1.
			self basicAt: index put: key].
	(self instVarAt: 2) at: index put: value.
	self fullCheck.
	^value
]

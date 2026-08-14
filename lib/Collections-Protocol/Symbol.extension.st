"
Symbol as an announcement identifier.

Pharo lets a Symbol stand in for an Announcement class -- `announcer
when: #FOO send: #bar to: nil' -- and the matching test for that lives on
Symbol itself, in Collections-Strings, a package we will not import because
Symbol is ours.  So it belongs here.

It was found the way these things should be found: AnnouncerTest's one
remaining FAILURE was still failing after the class it appeared to need
had been added, and printing the selector the test's own handler caught --
rather than comparing it -- named this in one line.  The test expects
#bar not to be understood; it was catching #handlesAnnouncement: instead.
"
Extension { #name : 'Symbol' }

{ #category : '*Collections-Protocol' }
Symbol >> handlesAnnouncement: anAnnouncement [
	"Only the identical symbol.  #FOO subscribes to #FOO and to nothing
	 else."
	^anAnnouncement == self
]

{ #category : '*Collections-Protocol' }
Symbol >> prepareForDelivery [
	"Nothing to prepare.  An Announcement gets a chance to fill itself in
	 before subscribers see it; a Symbol standing in for one has nothing
	 to fill in, and Pharo's own comment on this method says only 'somehow
	 symbol can be announcement'."
]

{ #category : '*Collections-Protocol' }
Symbol >> = anObject [
	"Identical, or a String spelling the same characters.

	 1983 answers identity alone, which is right for two Symbols -- they are
	 interned, so identity IS equality and the test is one instruction.  It
	 is wrong for a Symbol against a String, and wrong asymmetrically: our
	 String>>= already answers true for a Symbol with the same characters,
	 so `'Friday' = #Friday' and `#Friday = 'Friday'' disagreed, and which
	 answer you got depended on which side of the comparison you happened to
	 write first.

	 Two Chronology tests turn on it, both through indexOf:, which compares
	 element-first: `Week indexOfDay: 'Friday'` searches a table of Symbols
	 with a String and answered 0.

	 What this does NOT do is make hash agree.  Symbol>>hash is derived from
	 the object pointer and String>>hash from the characters, so a Set or a
	 Dictionary holding both kinds can still fail to find one by the other.
	 That inconsistency is not introduced here -- String>>= has had this
	 behaviour all along -- and fixing it means changing the hash the image's
	 own Symbol table is built on, which is a larger and separate thing."
	self == anObject ifTrue: [^true].
	anObject isSymbol ifTrue: [^false].
	^(anObject isKindOf: String) and: [anObject = self]
]

{ #category : '*Collections-Protocol' }
Symbol >> hash [
	"The same hash a String with my characters answers.

	 = and hash have to agree: a hashed collection puts an object in the
	 bucket its hash names and then compares with =, so two objects that are
	 equal and hash differently are two objects one of which cannot be
	 found.  Since Symbol>>= answers true for a String spelling the same
	 characters, this must answer what that String answers, and 1983's
	 primitive 75 -- half the object pointer -- does not.

	 The symptom was a Set that contained #foo answering false to
	 (includes: 'foo'), and a Dictionary at: #foo put: 1 that could not be
	 read with 'foo'.  Both are the collection working exactly as designed
	 on a contract the elements were breaking.

	 What this does NOT disturb, each checked rather than assumed:

	 The image's Symbol table is keyed by `aString stringhash', which is
	 this same text hash under another name -- Symbol>>stringhash is
	 `^super hash' -- so interning was never using primitive 75.

	 Method dictionaries are IdentityDictionaries, and
	 IdentityDictionary>>findKeyOrNil: probes by `key asOop', not by hash.
	 That is also what the VM's own lookup_method uses, so the C and
	 Smalltalk sides still agree about where a selector lives.

	 The C bootstrap finds globals by a linear scan over the association
	 array, comparing text, so it has no opinion about this at all."
	^super hash
]

{ #category : '*Collections-Protocol' }
Symbol >> value: anObject [
	"Me, sent to that object."
	^anObject perform: self
]

{ #category : '*Collections-Protocol' }
Symbol >> value: a value: b [
	^a perform: self with: b
]


{ #category : '*Collections-Protocol' }
Symbol >> valueWithArguments: anArray [
	^(anArray first) perform: self
		withArguments: (anArray copyFrom: 2 to: anArray size)
]

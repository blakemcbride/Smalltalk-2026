Extension { #name : 'MethodDictionary' }

{ #category : '*Collections-Compat' }
MethodDictionary >> methodArray [
	"My values -- the CompiledMethods -- as an Array, positionally matched
	 with my keys.

	 1983 answers its `valueArray' instance variable; Pharo's holds the same
	 thing in `array', with the selectors in the indexed part.  The two
	 layouts agree exactly where it matters, which is why this is one line
	 rather than a copy: both keep one array of selectors and one of
	 methods, for the same reason, and it is the one class the interpreter
	 could not afford to have change.

	 The live sender is Behavior>>whichSelectorsReferTo:special:byte:, which
	 is how the Browser answers `senders of'.  Without this it is a
	 doesNotUnderstand the first time anyone asks."
	^array
]

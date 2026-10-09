Extension { #name : 'Symbol' }

{ #category : '*Kernel-Protocol' }
Symbol >> displayString [
	^self asString
]

{ #category : '*Kernel-Protocol' }
Symbol >> isSymbol [
	^true
]

{ #category : '*Kernel-Protocol' }
Symbol class >> lookup: aString [
	"The Symbol whose characters those are, or nil when there is none.

	 asSymbol MAKES one, and a Symbol is never collected: it is put in
	 USTable and stays in the image for the life of the image.  That is
	 exactly what a symbol is for when the name comes from the program, and
	 exactly wrong when the name comes off a network.  Bugs3 B55: a REST
	 request's `_class' and `_method' were interned before anything checked
	 whether there was such a class or such a method, so eight thousand
	 requests naming eight thousand nonexistent methods added eight thousand
	 Symbols that two full garbage collections did not touch.  A caller
	 that wants to LOOK a name up asks for it this way and fails on nil;
	 only a name that turns out to be real is interned.

	 hasInterned:ifTrue: is the unlocked read the Concurrency extension
	 describes: a bucket is replaced by a copy and never edited in place, so
	 a reader sees the old bucket or the new one and never a torn one.  This
	 method therefore needs no lock and takes none.

	 A one-character string always answers, because the 128 single-character
	 Symbols are made once at build time and are not interned on demand.
	 That is not a leak -- there is nothing to create -- and a caller
	 checking a name still has to check what the name NAMES."

	self hasInterned: aString ifTrue: [:existing | ^existing].
	^nil
]

{ #category : '*Kernel-Protocol' }
Symbol >> isLiteral [
	"Whether I can be stored as # followed by my characters and read back
	 as me by the compiler this system has.  Two spellings read: a binary
	 selector made only of the characters its lexer calls binary, and a
	 name -- an ASCII letter, then ASCII letters, digits and colons, with
	 no two colons together.  Everything else takes the quoted form,
	 `'a::' asSymbol', which reads back as itself.

	 1983 asked its own Scanner's TypeTable, which calls every byte above
	 127 and every control byte binary; the compiler here calls the high
	 bytes letters and the control bytes errors.  So `('>' , 'ü') asSymbol
	 storeString' was `#>ü', read back as #> and a send of ü; a control
	 byte was a syntax error; #'a::' was `#a::', unexpected token (Bugs6
	 KERNA-7).  An underscore takes the quoted form too: it is a letter in
	 one dialect and an assignment in the other."
	| previous |
	self size = 0 ifTrue: [^false].
	(self first isLetter and: [self first asciiValue < 128]) ifTrue: [
		previous := nil.
		self do: [:c |
			((c isLetter and: [c asciiValue < 128])
				or: [(c isDigit and: [c asciiValue < 128]) or: [c = $:]])
					ifFalse: [^false].
			(c = $: and: [previous = $:]) ifTrue: [^false].
			previous := c].
		^true].
	self do: [:c | ('+-*/~<>=&|@%,?!\' includes: c) ifFalse: [^false]].
	^true
]

{ #category : '*Kernel-Protocol' }
Symbol >> basicAt: anInteger put: anObject [
	"Symbols are not modified: at:put: has always said so, and the
	 primitives refuse a Symbol since Bugs5 INTERP-7; the fallback then
	 explained the refusal as a bad index (Bugs6 FIXES-5)."
	^self errorNoModification
]

{ #category : '*Kernel-Protocol' }
Symbol >> primReplaceFrom: start to: stop with: replacement startingAt: repStart [
	"As basicAt:put:: the refusal, in the Symbol's own words."
	^self errorNoModification
]

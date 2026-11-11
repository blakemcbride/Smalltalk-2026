"
A Text takes a String where it asked for a Text.

The manual presents a Text as a String with emphasis -- the thing a view
hands you, to which `,' and copyReplaceAll:with: apply as they do to any
collection -- and 1983's Text>>replaceFrom:to:with: assumed its argument
was a Text and sent it `runs', a doesNotUnderstand on a String (Bugs6
KERNA-13).  The String is given the emphasis of plain text, which is what
asText does.

A Text is equal to a Text or a String with its characters and unequal to
anything else, without raising (Bugs07 KERNA-10).
"
Extension { #name : 'Text' }

{ #category : '*Strings-Protocol' }
Text >> replaceFrom: start to: stop with: aTextOrString [
	"1983's, with the argument taken asText first: `aView text , 'more''
	 reached here with a String and died sending it runs (Bugs6
	 KERNA-13).  A Text answers asText with itself."
	| aText |
	aText := aTextOrString asText.
	string := string copyReplaceFrom: start to: stop with: aText string.
	runs := runs copyReplaceFrom: start to: stop with: aText runs
]

{ #category : '*Strings-Protocol' }
Text >> = anObject [
	"Whether anObject is a Text or a String with my characters.

	 1983's is `^string = anotherText string', which sends string to
	 whatever it is given: `aText = 3', `aText = nil' and `aText = #(1 2)'
	 raised doesNotUnderstand, so `(OrderedCollection with: aText) indexOf:
	 nil' failed, and so did any hashed collection whose probe compared a
	 Text with something that is not text (Bugs07 KERNA-10).  Equality
	 must never raise -- it is what Set, Dictionary and every includes: are
	 built on -- so anything that is not text is simply unequal.  A Text is
	 compared by its characters, emphasis aside, as 1983 compared it, and a
	 String, a Symbol included, by its characters too, which String>>= and
	 Symbol>>= answer the same way from the other side."
	(anObject isKindOf: Text) ifTrue: [^string = anObject string].
	^(anObject isKindOf: String) and: [string = anObject]
]

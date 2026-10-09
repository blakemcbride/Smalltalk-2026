"
A Text takes a String where it asked for a Text.

The manual presents a Text as a String with emphasis -- the thing a view
hands you, to which `,' and copyReplaceAll:with: apply as they do to any
collection -- and 1983's Text>>replaceFrom:to:with: assumed its argument
was a Text and sent it `runs', a doesNotUnderstand on a String (Bugs6
KERNA-13).  The String is given the emphasis of plain text, which is what
asText does.
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

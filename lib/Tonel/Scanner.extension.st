"
The image's own compiler learns `:=' for assignment.

Every file in lib/ writes assignment as `:=', and every one of them was
compiled by the C bootstrap, which has always accepted it.  The compiler INSIDE
the image is 1983's, and 1983 spelt assignment with the left arrow -- the
underscore, in this character set -- and nothing else: its scanner maps `:' to
a colon token and `=' to a binary selector, so `a := 1' is `a : = 1', which is
a syntax error.  Nothing noticed for as long as the only code the image
compiled was typed into a Browser by somebody who knew that.  A Tonel file
loaded at run time is written in the spelling every other file in this tree
uses, and the first one compiled here failed on its first assignment -- and,
because a syntax error with nobody to tell opens a notifier and suspends the
process, failed by hanging a server.

So scanToken is 1983's, with one line after it: a colon followed by an equals
sign is the left-arrow token.  The colon on its own still scans as a colon,
which is what a block argument needs, and keywords never reach here -- they are
read whole by xLetter.
"
Extension { #name : 'Scanner' }

{ #category : '*Tonel' }
Scanner >> scanToken [
	"1983's scanToken, and then `:=' read as the assignment arrow."

	[(tokenType := typeTable at: hereChar asciiValue) == #xDelimiter]
		whileTrue: [self step].  "Skip delimiters fast, there almost always is one."
	mark := source position - 1.
	(tokenType at: 1) = $x "x as first letter"
		ifTrue: [self perform: tokenType "means perform to compute token & type"]
		ifFalse: [token := self step asSymbol "else just unique the first char"].
	(tokenType == #colon and: [hereChar = $=]) ifTrue: [
		self step.
		tokenType := #leftArrow.
		token := #'_'].
	^token
]
